/*
 * mkfs.c - Utility to create an overlay filesystem
 *
 * Copyright (c) 2018 Huawei.  All Rights Reserved.
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

#include "common.h"
#include "config.h"
#include "ovl.h"
#include "lib.h"
#include "mount.h"
#include "feature.h"
#include "overlayfs.h"

/* mkfs flags */
#define FL_VERBOSE	(0x00000001)	/* verbose */


char *program_name = NULL;
struct ovl_fs ofs = {};
unsigned int flags = 0;		/* user input option flags */


static void usage(void)
{
	print_info(_("Usage:\n\t%s "
		     "[-o lowerdir=<lowers>,upperdir=<upper>,workdir=<work>]\n\t\t"
		     "[-o features[...]][-vV]\n\n"), program_name);
	print_info(_("Options:\n"
		     "-o,                       specify underlying directories of overlayfs and\n"
		     "                          overlay filesystem features, multiple lower\n"
		     "                          directories use ':' as separator\n"
		     "                          multiple lower directories use ':' as separator\n"
		     "-v, --verbose             print more messages of overlayfs\n"
		     "-V, --version             display version information\n"));
	exit(1);
}

/* Parse options from user and check correctness */
static void parse_options(int argc, char *argv[])
{
	char *ovl_opts = NULL;
	int i, c;
	char **lowerdir = NULL;

	struct option long_options[] = {
		{"verbose", no_argument, NULL, 'v'},
		{"version", no_argument, NULL, 'V'},
		{NULL, 0, NULL, 0}
	};

	while ((c = getopt_long(argc, argv, "o:vV",
		long_options, NULL)) != -1) {

		switch (c) {
		case 'o':
			ovl_opts = sstrdup(optarg);
			ovl_parse_opt(ovl_opts, &ofs.config);
			free(ovl_opts);
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

	ofs.lower_layer = (struct ovl_layer *)smalloc(ofs.lower_num * sizeof(struct ovl_layer));
	for (i = 0; i < ofs.lower_num; i++) {
		ofs.lower_layer[i].path = lowerdir[i];
		ofs.lower_layer[i].type = OVL_LOWER;
		ofs.lower_layer[i].stack = i;
	}
	if (ofs.upper_layer.path) {
		ofs.upper_layer.type = OVL_UPPER;
		flags |= FL_UPPER;
	}
	if (ofs.workdir.path)
		ofs.workdir.type = OVL_WORK;

	if (!ofs.lower_num ||
	    (!(flags & FL_UPPER) && ofs.lower_num == 1)) {
		print_info(_("Please specify correct lowerdirs and upperdir!\n\n"));
		goto err_out2;
	}

	if (ofs.upper_layer.path && !ofs.workdir.path) {
		print_info(_("Please specify correct workdir!\n\n"));
		goto err_out2;
	}

	if (!ofs.config.index && ofs.config.nfs_export) {
		print_info(_("Cannot set nfs_export feature when index feature is off!\n\n"));
		goto err_out2;
	}

	free(lowerdir);
	return;

err_out2:
	ovl_free_opt(&ofs.config);
	ovl_clean_dirs(&ofs);
	free(lowerdir);
err_out:
	usage();
	exit(1);
}

enum ovl_status {
	OVL_SCAN_OK,
	OVL_SCAN_UPPER_RO,		/* upper layer is read-only */
	OVL_SCAN_UPPER_NOXATTR,		/* Upper layer not support xattr */
	OVL_SCAN_MOUNTED,		/* The filesystem is mounted */
	OVL_SCAN_NO_EMPTY,		/* The filesystem is not empty */
};

/* Count whiteouts in this scan process */
int ovl_count_whiteouts(struct scan_ctx *sctx)
{
	if (is_whiteout(sctx->st))
		sctx->result.t_whiteouts++;
	return 0;
}

/* Count overlayfs related xattrs in this scan process */
int ovl_count_xattrs(struct scan_ctx *sctx)
{
	struct ovl_layer *layer = sctx->layer;
	int ret = 0;
	char *xattrs = NULL;
	char *p;

	ret = list_xattr(layer->fd, sctx->pathname, &xattrs);
	if (ret <= 0)
		return ret;

	while ((p = strsep(&xattrs, "")) != NULL) {
		if (!strncmp(p, OVL_XATTR_PREFIX,
			     strlen(OVL_XATTR_PREFIX))) {
			sctx->result.xattrs++;
			break;
		}
	}

	return 0;
}

/*
 * Scan feature set and overlay related xattrs/whiteouts on this layer,
 * If anyone of them exists that means this layer may be mounted by
 * overlayfs, we cannot overwrite it simply because it may lead to
 * inconsistency.
 */
static int ovl_scan_layer(struct ovl_layer *layer, bool *empty)
{
	struct ovl_d_feature *odf = NULL;
	struct scan_ctx sctx = {};
	struct scan_operations ops = {};
	int ret;

	/* Get basic layer feature */
	ret = ovl_basic_check_layer(layer);
	if (ret)
		return ret;

	/* Check feature set is empty or not */
	if (layer->flag & FS_LAYER_XATTR) {
		ret = ovl_get_feature(layer, &odf);
		if (ret < 0)
			return ret;
		if (ret > 0) {
			free(odf);
			*empty = false;
			return 0;
		}
	}

	/* Check overlay related xattrs in this layer */
	sctx.layer = layer;
	ops.xattr = ovl_count_xattrs;
	ops.whiteout = ovl_count_whiteouts;
	ret = scan_dir(&sctx, &ops);
	if (ret)
		return ret;

	*empty = !(sctx.result.xattrs || sctx.result.t_whiteouts);
	return 0;
}

/*
 * Scan the whole filesystem to check the specified layers could be
 * make a new overlayfs appropriately. Do the following check:
 * 1. Mount check: treat mounted if any one layer is mounted,
 * 2. Empty check: check the existence of overlay related xattrs/whiteouts,
 *                 feature set and index/work dir,
 * 3. Writeable check: the upper layer should read-write,
 * 4. Xattr check: the upper layer should support xattr.
 */
static int ovl_scan_filesystem(struct ovl_fs *ofs, enum ovl_status *ost)
{
	int i;
	bool mounted;
	bool empty;
	int ret;

	/* Check the filesystem is mounted or not */
	ret = ovl_check_mount(ofs, &mounted);
	if (ret)
		return ret;

	if (mounted) {
		*ost = OVL_SCAN_MOUNTED;
		return 0;
	}

	/* Check "work/index" dir exists or not */
	if (flags & FL_UPPER) {
		struct stat st;

		ret = fstatat(ofs->workdir.fd, "work", &st, AT_SYMLINK_NOFOLLOW);
		if (ret && errno != ENOENT) {
			print_err(_("Failed to fstatat work: %s\n"), strerror(errno));
			return ret;
		}
		if (!ret) {
			*ost = OVL_SCAN_NO_EMPTY;
			return 0;
		}

		ret = fstatat(ofs->workdir.fd, "index", &st, AT_SYMLINK_NOFOLLOW);
		if (ret && errno != ENOENT) {
			print_err(_("Failed to fstatat work: %s\n"), strerror(errno));
			return ret;
		}
		if (!ret) {
			*ost = OVL_SCAN_NO_EMPTY;
			return 0;
		}
	}

	/* Check each layer's basic feature */
	if (flags & FL_UPPER) {
		struct ovl_layer *upper_layer = &ofs->upper_layer;

		ret = ovl_scan_layer(upper_layer, &empty);
		if (ret)
			return ret;

		if (!empty) {
			*ost = OVL_SCAN_NO_EMPTY;
			return 0;
		}

		if (upper_layer->flag & FS_LAYER_RO) {
			*ost = OVL_SCAN_UPPER_RO;
			return 0;
		}
		if (!(upper_layer->flag & FS_LAYER_XATTR)) {
			*ost = OVL_SCAN_UPPER_NOXATTR;
			return 0;
		}
	}

	for (i = 0; i < ofs->lower_num; i++) {
		struct ovl_layer *lower_layer = &ofs->lower_layer[i];

		ret = ovl_scan_layer(lower_layer, &empty);
		if (ret)
			return ret;

		if (!empty) {
			*ost = OVL_SCAN_NO_EMPTY;
			return 0;
		}
	}

	*ost = OVL_SCAN_OK;
	return 0;
}

/*
 * Scan the whole filesystem, we will refuse to make a new overlayfs if
 * 1. The upper layer is read-only or does not support xattr,
 * 2. Any one layer is mounted by overlayfs.
 * 3. Any one layer is not empty(contains overlayfs related features).
 */
static int ovl_check_filesystem(struct ovl_fs *ofs)
{
	enum ovl_status ost = OVL_SCAN_OK;
	int ret;

	ret = ovl_scan_filesystem(ofs, &ost);
	if (ret)
		return ret;

	print_info(_("%s %s\n"), program_name, PACKAGE_VERSION);
	switch (ost) {
	case OVL_SCAN_OK:
		print_info(_("Upper layer: %d\n"), !!(flags & FL_UPPER));
		print_info(_("Lower layers: %d\n"), ofs->lower_num);
		return 0;
	case OVL_SCAN_MOUNTED:
		print_info(_("This overlay filesystem is mounted, "
			     "will not make a filesystem here!\n"));
		break;
	case OVL_SCAN_NO_EMPTY:
		print_info(_("These directories contains an existing "
			     "overlay filesystem, will not make a "
			     "filesystem here!\n"));
		print_info(_("Please run fsck.overlay to check "
			     "this filesystem!\n"));
		break;
	case OVL_SCAN_UPPER_RO:
		print_info(_("The upper layer is read-only!\n"));
		break;
	case OVL_SCAN_UPPER_NOXATTR:
		print_info(_("The upper layer does not support xattr!\n"));
		break;
	default:
		break;
	}

	return -1;
}

/*
 * Make overlayfs, it will set an empty feature set to each layer's root
 * directory. Note that we may not get a fully initialized filesystem
 * when some lower layers are read-only or not support xattr, but this
 * filesystem can still mount by overlayfs.
 */
static int ovl_make_filesystem(struct ovl_fs *ofs)
{
	int i;
	int ret = 0;

	if (flags & FL_UPPER) {
		/* Init an empty feature set for upper layer */
		ret = ovl_init_empty_feature(&ofs->upper_layer);
		if (ret)
			goto out;

		/* Set specified feature set */
		if (ofs->config.redirect_dir) {
			ret = ovl_set_feature_redirect_dir(&ofs->upper_layer);
			if (ret)
				goto out;
		}

		/*
		 * TODO: check an refuse to turn on "index" and "nfs_export"
		 * feature if the underlying base file system does not
		 * support file handle.
		 */
		if (ofs->config.index) {
			ret = ovl_set_feature_index(&ofs->upper_layer);
			if (ret)
				goto out;
		}
		if (ofs->config.nfs_export) {
			ret = ovl_set_feature_nfs_export(&ofs->upper_layer);
			if (ret)
				goto out;
		}
	}

	/* Init an empty feature set for each lower layer */
	for (i = 0; i < ofs->lower_num; i++) {
		if (ofs->lower_layer[i].flag & FS_LAYER_RO) {
			print_info(_("Warning: lower layer %d is read-only\n"), i);
			continue;
		}
		if (!(ofs->lower_layer[i].flag & FS_LAYER_XATTR)) {
			print_info(_("Warning: lower layer %d does not support xattr\n"), i);
			continue;
		}

		ret = ovl_init_empty_feature(&ofs->lower_layer[i]);
		if (ret)
			break;
	}

	print_info(_("Init feature set: done\n"));
out:
	return ret;
}

int main(int argc, char *argv[])
{
	int ret;

	program_name = basename(argv[0]);
	parse_options(argc, argv);

	ret = ovl_open_dirs(&ofs);
	if (ret)
		goto out;

	ret = ovl_check_filesystem(&ofs);
	if (ret)
		goto out;

	ret = ovl_make_filesystem(&ofs);
	if (ret)
		goto out;
out:
	ovl_free_opt(&ofs.config);
	ovl_clean_dirs(&ofs);
	return ret ? 1 : 0;
}
