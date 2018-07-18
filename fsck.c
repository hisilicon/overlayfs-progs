/*
 * fsck.c - Utility to fsck overlay
 *
 * Copyright (c) 2017 Huawei.  All Rights Reserved.
 * Author: zhangyi (F) <yi.zhang@huawei.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <fcntl.h>
#include <unistd.h>
#include <libgen.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <sys/resource.h>
#include <sys/vfs.h>
#include <sys/statvfs.h>
#include <linux/limits.h>

#include "common.h"
#include "config.h"
#include "ovl.h"
#include "lib.h"
#include "fsck.h"
#include "check.h"
#include "mount.h"
#include "overlayfs.h"
#include "feature.h"

char *program_name;

struct ovl_fs ofs = {};
int flags = 0;		/* user input option flags */
int status = 0;		/* fsck scan status */

/* Do some basic check for the workdir, not iterate the dir */
static int ovl_basic_check_workdir(struct ovl_fs *ofs)
{
	struct statfs upperfs, workfs;
	struct stat st;
	int ret;

	ret = fstatfs(ofs->upper_layer.fd, &upperfs);
	if (ret) {
		print_err(_("fstatfs failed:%s\n"), strerror(errno));
		return -1;
	}

	ret = fstatfs(ofs->workdir.fd, &workfs);
	if (ret) {
		print_err(_("fstatfs failed:%s\n"), strerror(errno));
		return -1;
	}

	/* Workdir should not be subdir of upperdir and vice versa */
	if (strstr(ofs->upper_layer.path, ofs->workdir.path) ||
	    strstr(ofs->workdir.path, ofs->upper_layer.path)) {
		print_info(_("Workdir should not be subdir of "
			     "upperdir and vice versa\n"));
		return -1;
	}

	/* Upperdir and workdir should belongs to one file system */
	if (memcmp(&upperfs.f_fsid, &workfs.f_fsid, sizeof(fsid_t))) {
		print_info(_("Upper dir and lower dir should "
			     "belongs to one file system\n"));
		return -1;
	}

	/* workdir should not be read-only */
	if ((workfs.f_flags & ST_RDONLY) && !(flags & FL_OPT_NO)) {
		print_info(_("Workdir is read-only\n"));
		return -1;
	}


	ret = fstatat(ofs->workdir.fd, OVL_INDEXDIR_NAME, &st, AT_SYMLINK_NOFOLLOW);
	if (ret) {
		if (errno != ENOENT) {
			print_err(_("Cannot stat %s: %s\n"), OVL_INDEXDIR_NAME,
				    strerror(errno));
			return -1;
		}

		/* Index object not exists, do nothing */
		return 0;
	}

	/* Invalid index file exists, remove it */
	if (!is_dir(&st)) {
		if (!ovl_ask_question("Remove invalid index non-directory",
				      ofs->workdir.path, ofs->workdir.type,
				      ofs->workdir.stack, 1)) {
			set_inconsistency(&status);
		} else {
			ret = unlinkat(ofs->workdir.fd, ofs->workdir.path, 0);
			if (ret) {
				print_err(_("Cannot unlink %s: %s\n"),
					    ofs->workdir.path, strerror(errno));
				return -1;
			}
		}
	} else {
		/* Index dir exists, set index flag */
		ofs->workdir.flag |= FS_LAYER_INDEX;

		/* Todo: we do not support index feature yet */
		print_info(_("Sorry: index feature is not support yet\n"));
		return -1;
	}

	return 0;
}

/*
 * Check and fix layer feature set, return 0 if it pass checking,
 * errno if error.
 */
int ovl_check_feature_set(struct ovl_layer *layer)
{
	struct ovl_d_feature *odf = NULL;
	int err = 0;

	/* Read layer feature xattr */
	err = ovl_get_check_feature(layer, &odf);
	if (err < 0)
		return err;

	/*
	 * Feature set becomes untrusted if it was corrupted. Do not fix
	 * it automaticily because this layer may contain unsupported
	 * features
	 */
	if (err == EINVAL) {
		/* Layer becomes v2 if feature set is not empty */
		layer->format = OVL_LAYER_V2;

		if (layer->flag & FS_LAYER_RO)
			goto inconsistency;

		if (ovl_ask_action("Bad feature set found", layer->path,
				   layer->type, layer->stack,
				   "Recreate an empty one", 0)) {

			err = ovl_init_empty_feature(layer);
			if (err)
				goto inconsistency;

			set_changed(&status);
			goto out;
		}

		goto inconsistency;
	}

	/*
	 * No feature set found on this layer, try to init an
	 * empty one
	 */
	if (!odf) {
		/*
		 * Do not init feature set if this layer is read-only and
		 * feature set is not necessary.
		 */
		if (layer->flag & FS_LAYER_RO) {
			if (layer->format == OVL_LAYER_V1)
				goto out;

			print_info(_("Cannot init feature set because layer "
				     "is read-only\n"));

			goto inconsistency;
		}

		/*
		 * Init an empty one is not necessary if feature set is not
		 * necessary when user say "no".
		 */
		if (ovl_ask_action("No feature set found", layer->path,
				   layer->type, layer->stack,
				   "Create an empty one",
				   (layer->format == OVL_LAYER_V2))) {

			err = ovl_init_empty_feature(layer);
			if (!err) {
				set_changed(&status);
				goto out;
			}
		}
		if (layer->format == OVL_LAYER_V1)
			goto out;

		goto inconsistency;
	}

	/* Check feature set support or not */
	layer->format = OVL_LAYER_V2;
	layer->compat = be64_to_cpu(odf->compat);
	layer->ro_compat = be64_to_cpu(odf->ro_compat);
	layer->incompat = be64_to_cpu(odf->incompat);

	if (!ovl_check_feature_support(layer)) {
		print_info(_("Unknown features found in %s layer root: %s\n"
			     "Get a newer version of %s!\n"),
			     (layer->type == OVL_UPPER) ? "upper" : "lower",
			     layer->path, program_name);
		goto fail;
	}

	print_debug(_("Get feature in %s root: %s: magic=%x, "
		      "compat=%llx, ro_compat=%llx, incompat=%llx\n"),
		      layer->type == OVL_UPPER ? "upper" : "lower",
		      layer->path, odf->magic, layer->compat,
		      layer->ro_compat, layer->incompat);
out:
	free(odf);
	return err;

inconsistency:
	set_inconsistency(&status);
fail:
	err = -1;
	goto out;
}

/*
 * Do basic check for the underlying filesystem, refuse to do futher check
 * if something wrong.
 */
static int ovl_basic_check(struct ovl_fs *ofs)
{
	int ret;
	int i;

	if (flags & FL_UPPER) {
		/* Check work root dir */
		ret = ovl_basic_check_workdir(ofs);
		if (ret)
			return ret;

		ret = ovl_basic_check_layer(&ofs->upper_layer);
		if (ret)
			return ret;

		/* Upper layer should read-write */
		if ((ofs->upper_layer.flag & FS_LAYER_RO) &&
		    !(flags & FL_OPT_NO)) {
			print_info(_("Upper base filesystem is read-only, "
				     "should be read-write\n"));
			return -1;
		}

		/* Upper layer must support xattr when OVL_LAYER_V2 */
		if ((ofs->upper_layer.format == OVL_LAYER_V2) &&
		    !(ofs->upper_layer.flag & FS_LAYER_XATTR)) {
			print_info(_("Upper should support xattr in V2\n"));
			return -1;
		}

		/* Check layer feature */
		if (ofs->upper_layer.flag & FS_LAYER_XATTR) {
			ret = ovl_check_feature_set(&ofs->upper_layer);
			if (ret)
				return ret;
		}

		/*
		 * Fix index feature when index dir detected. Note that
		 * this is not necessary now if user say 'n' for backward
		 * compatibility
		 */
		if ((ofs->workdir.format == OVL_LAYER_V2) &&
		    (ofs->workdir.flag & FS_LAYER_INDEX) &&
		    !ovl_has_feature_index(&ofs->upper_layer)) {

			if (ovl_ask_action("Missing index feature",
					   ofs->upper_layer.path,
					   ofs->upper_layer.type,
					   ofs->upper_layer.stack,
					   "Fix", 1)) {

				ret = ovl_set_feature_index(&ofs->upper_layer);
				if (!ret) {
					set_changed(&status);
					goto lower;
				}
			}

			if (ovl_feature_set_necessary(ofs, &ofs->upper_layer))
				set_inconsistency(&status);
		}
	}

lower:
	for (i = 0; i < ofs->lower_num; i++) {
		ret = ovl_basic_check_layer(&ofs->lower_layer[i]);
		if (ret)
			return ret;

		/* Lower layer must support xattr when OVL_LAYER_V2 */
		if ((ofs->upper_layer.format == OVL_LAYER_V2) &&
		    !(ofs->upper_layer.flag & FS_LAYER_XATTR)) {
			print_info(_("Lower %d should support xattr in V2\n"), i);
			return -1;
		}

		if (ofs->lower_layer[i].flag & FS_LAYER_XATTR) {
			ret = ovl_check_feature_set(&ofs->lower_layer[i]);
			if (ret)
				return ret;
		}
	}

	return 0;
}

void ovl_display_feature_set(struct ovl_fs *ofs)
{
	int i;

	print_info(_("%s %s\n"), program_name, PACKAGE_VERSION);

	if (flags & FL_UPPER)
		ovl_print_feature_set(&ofs->upper_layer);

	for (i = 0; i < ofs->lower_num; i++)
		ovl_print_feature_set(&ofs->lower_layer[i]);

	print_info(_("\n"));
}

static void usage(void)
{
	print_info(_("Usage:\n\t%s [-o lowerdir=<lowers>,upperdir=<upper>,workdir=<work>]\n"
		    "\t\t[-o options[,...]][-pnyhvV]\n\n"), program_name);
	print_info(_("Options:\n"
		    "-o,                       specify underlying directories of overlayfs\n"
		    "                          and fs check options, multiple lower directories\n"
		    "                          use ':' as separator\n"
		    "-p,                       automatic repair (no questions)\n"
		    "-n,                       make no changes to the filesystem\n"
		    "-y,                       assume \"yes\" to all questions\n"
		    "-h,                       display the features information on each layer\n"
		    "-v, --verbose             print more messages of overlayfs\n"
		    "-V, --version             display version information\n"));
	exit(FSCK_USAGE);
}

/* Parse options from user and check correctness */
static void parse_options(int argc, char *argv[])
{
	char *ovl_opts = NULL;
	int i, c;
	char **lowerdir = NULL;
	bool conflict = false;

	struct option long_options[] = {
		{"verbose", no_argument, NULL, 'v'},
		{"version", no_argument, NULL, 'V'},
		{NULL, 0, NULL, 0}
	};

	while ((c = getopt_long(argc, argv, "o:apnyhvV",
		long_options, NULL)) != -1) {

		switch (c) {
		case 'o':
			ovl_opts = sstrdup(optarg);
			ovl_parse_opt(ovl_opts, &ofs.config);
			free(ovl_opts);
			break;
		case 'p':
			if (flags & (FL_OPT_YES | FL_OPT_NO))
				conflict = true;
			else
				flags |= FL_OPT_AUTO;
			break;
		case 'n':
			if (flags & (FL_OPT_YES | FL_OPT_AUTO))
				conflict = true;
			else
				flags |= FL_OPT_NO;
			break;
		case 'y':
			if (flags & (FL_OPT_NO | FL_OPT_AUTO))
				conflict = true;
			else
				flags |= FL_OPT_YES;
			break;
		case 'h':
			flags |= FL_DSP_FEATURE;
			break;
		case 'v':
			flags |= FL_VERBOSE;
			break;
		case 'V':
			version();
			exit(0);
		default:
			usage();
			return;
		}
	}

	/* Resolve and get each underlying directory of overlay filesystem */
	if (ovl_get_dirs(&ofs.config, &lowerdir, &ofs.lower_num,
			 &ofs.upper_layer.path, &ofs.workdir.path))
		goto err_out;

	ofs.lower_layer = smalloc(ofs.lower_num * sizeof(struct ovl_layer));
	for (i = 0; i < ofs.lower_num; i++) {
		ofs.lower_layer[i].format = (ofs.config.format == OVL_FS_V2) ?
					      OVL_LAYER_V2 : OVL_LAYER_V1;
		ofs.lower_layer[i].path = lowerdir[i];
		ofs.lower_layer[i].type = OVL_LOWER;
		ofs.lower_layer[i].stack = i;
	}
	if (ofs.upper_layer.path) {
		ofs.upper_layer.format = (ofs.config.format != OVL_FS_V1) ?
					   OVL_LAYER_V2 : OVL_LAYER_V1;
		ofs.upper_layer.type = OVL_UPPER;
		flags |= FL_UPPER;
	}
	if (ofs.workdir.path) {
		ofs.workdir.format = ofs.upper_layer.format;
		ofs.workdir.type = OVL_WORK;
	}

	if (!ofs.lower_num ||
	    (!(flags & FL_UPPER) && ofs.lower_num == 1)) {
		print_info(_("Please specify correct lowerdirs and upperdir!\n\n"));
		goto usage_out;
	}

	if (ofs.upper_layer.path && !ofs.workdir.path) {
		print_info(_("Please specify correct workdir!\n\n"));
		goto usage_out;
	}

	if (conflict) {
		print_info(_("Only one of the options -p/-a, -n or -y "
			     "can be specified!\n\n"));
		goto usage_out;
	}

	free(lowerdir);
	return;

usage_out:
	ovl_free_opt(&ofs.config);
	ovl_clean_dirs(&ofs);
	free(lowerdir);
	usage();
err_out:
	exit(FSCK_ERROR);
}

/* Check file system status after fsck and return the exit value */
static void fsck_exit(void)
{
	int exit_value = FSCK_OK;

	if (status & OVL_ST_CHANGED) {
		exit_value |= FSCK_NONDESTRUCT;
		print_info(_("File system was modified!\n"));
	}

	if (status & OVL_ST_INCONSISTNECY) {
		exit_value |= FSCK_UNCORRECTED;
		exit_value &= ~FSCK_NONDESTRUCT;
		print_info(_("Still have unexpected inconsistency!\n"));
	}

	if (status & OVL_ST_ABORT) {
		exit_value |= FSCK_ERROR;
		print_info(_("Cannot continue, aborting!\n"));
		print_info(_("Filesystem check failed, may not clean!\n"));
	}

	if ((exit_value == FSCK_OK) ||
	    (!(exit_value & FSCK_ERROR) && !(exit_value & FSCK_UNCORRECTED)))
		print_info(_("Filesystem clean\n"));

	exit(exit_value);
}

int main(int argc, char *argv[])
{
	bool mounted = false;

	program_name = basename(argv[0]);

	parse_options(argc, argv);

	/* Open all specified base dirs */
	if (ovl_open_dirs(&ofs))
		goto err;

	/* Display feature on each layers */
	if (flags & FL_DSP_FEATURE) {
		ovl_display_feature_set(&ofs);
		ovl_free_opt(&ofs.config);
		ovl_clean_dirs(&ofs);
		return 0;
	}

	/* Ensure overlay filesystem not mounted */
	if (ovl_check_mount(&ofs, &mounted))
		goto err;

	if (mounted && !(flags & FL_OPT_NO)) {
		set_abort(&status);
		goto out;
	}

	/* Do basic check */
	if (ovl_basic_check(&ofs))
		goto err;

	/* Scan and fix */
	if (ovl_scan_fix(&ofs))
		goto err;

out:
	ovl_free_opt(&ofs.config);
	ovl_clean_dirs(&ofs);
	fsck_exit();
	return 0;
err:
	set_abort(&status);
	goto out;
}
