/*
 * mount.c - Parse underlying dirs and check mounted overlay file system
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


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <stdbool.h>
#include <mntent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/limits.h>

#include "common.h"
#include "lib.h"
#include "config.h"
#include "check.h"
#include "mount.h"
#include "overlayfs.h"

struct ovl_mnt_entry {
	char **lowerdir;
	int lowernum;
	char *upperdir;
	char *workdir;
};

/* Mount buf allocate a time */
#define ALLOC_NUM	16

/* Resolve each lower directories and check the validity */
static int ovl_resolve_lowerdirs(char *loweropt, char ***lowerdir,
				 int *lowernum)
{
	char temp[PATH_MAX] = {0};
	int num;
	char **dirs;
	char *p;
	int i;

	num = ovl_split_lowerdirs(loweropt);
	if (num > OVL_MAX_STACK) {
		print_err(_("Too many lower directories:%u, max:%u\n"),
			    num, OVL_MAX_STACK);
		return -1;
	}

	dirs = smalloc(sizeof(char *) * num);

	p = loweropt;
	for (i = 0; i < num; i++) {
		if (!realpath(p, temp)) {
			print_err(_("Failed to resolve lowerdir:%s:%s\n"),
				    p, strerror(errno));
			goto err;
		}
		dirs[i] = sstrdup(temp);
		print_debug(_("Lowerdir %u:%s\n"), i, dirs[i]);
		p = strchr(p, '\0') + 1;
	}

	*lowerdir = dirs;
	*lowernum = num;

	return 0;
err:
	for (i--; i >= 0; i--)
		free(dirs[i]);
	free(dirs);
	*lowerdir = NULL;
	*lowernum = 0;
	return -1;
}

static inline char *ovl_match_dump(const char *opt, const char *type)
{
	int len = strlen(opt) - strlen(type) + 1;

	return sstrndup(opt+strlen(type), len);
}

/*
 * Resolve and get each underlying directory of overlay filesystem
 *
 * @config: underlying dirs user specified
 * @lowerdir: absolute path of lowerdirs
 * @lowernum: stack number of lowerdirs
 * @upperdir: absolute path of upperdir
 * @workdir: absolute path of workdir
 */
int ovl_get_dirs(struct ovl_config *config, char ***lowerdir,
		 int *lowernum, char **upperdir, char **workdir)
{
	char temp[PATH_MAX] = {0};

	/* Resolve upperdir */
	if (config->upperdir) {
		if (!realpath(config->upperdir, temp)) {
			print_err(_("Faile to resolve upperdir:%s:%s\n"),
				    config->upperdir, strerror(errno));
			goto err_out;
		}
		*upperdir = sstrdup(temp);
		print_debug(_("Upperdir: %s\n"), *upperdir);
	}

	/* Resolve workdir */
	if (config->workdir) {
		if (!realpath(config->workdir, temp)) {
			print_err(_("Faile to resolve workdir:%s:%s\n"),
				    config->workdir, strerror(errno));
			goto err_work;
		}
		*workdir = sstrdup(temp);
		print_debug(_("Workdir: %s\n"), *workdir);
	}

	/* Resolve lowerdir */
	if (config->lowerdir) {
		if (ovl_resolve_lowerdirs(config->lowerdir, lowerdir, lowernum))
			goto err_lower;
	}

	return 0;

err_lower:
	if (*workdir)
		free(*workdir);
	*workdir = NULL;
err_work:
	if (*upperdir)
		free(*upperdir);
	*upperdir = NULL;
err_out:
	return -1;
}

void ovl_free_opt(struct ovl_config *config)
{
	free(config->upperdir);
	config->upperdir = NULL;
	free(config->lowerdir);
	config->lowerdir = NULL;
	free(config->workdir);
	config->workdir = NULL;
}

/*
 * Split and parse opt to each underlying directories.
 */
void ovl_parse_opt(char *opt, struct ovl_config *config)
{
	char *p;

	while ((p = ovl_next_opt(&opt)) != NULL) {
		if (!*p)
			continue;

		if (!strncmp(p, OPT_UPPERDIR, strlen(OPT_UPPERDIR))) {
			free(config->upperdir);
			config->upperdir = ovl_match_dump(p, OPT_UPPERDIR);
		} else if (!strncmp(p, OPT_LOWERDIR, strlen(OPT_LOWERDIR))) {
			free(config->lowerdir);
			config->lowerdir = ovl_match_dump(p, OPT_LOWERDIR);
		} else if (!strncmp(p, OPT_WORKDIR, strlen(OPT_WORKDIR))) {
			free(config->workdir);
			config->workdir = ovl_match_dump(p, OPT_WORKDIR);
		}
	}
}

/*
 * Scan current mounted overlayfs and get used underlying directories,
 * (do not scan overlayfs mounted with relative path)
 */
static int ovl_scan_mount_init(struct ovl_mnt_entry **ovl_mnt_entries,
			       int *ovl_mnt_count)
{
	struct ovl_mnt_entry *entries = NULL;
	struct mntent *mnt = NULL;
	struct ovl_config config = {0};
	FILE *fp;
	char *opt;
	int allocated, i = 0;

	fp = setmntent(MOUNT_TAB, "r");
	if (!fp) {
		print_err(_("Fail to setmntent %s:%s\n"),
			    MOUNT_TAB, strerror(errno));
		return -1;
	}

	allocated = sizeof(struct ovl_mnt_entry) * ALLOC_NUM;
	entries = smalloc(allocated);

	while ((mnt = getmntent(fp))) {
		if (strcmp(mnt->mnt_type, OVERLAY_NAME))
			continue;

		opt = sstrdup(mnt->mnt_opts);
		ovl_parse_opt(opt, &config);

		if ((config.lowerdir && config.lowerdir[0] != '/') ||
		    (config.upperdir && config.upperdir[0] != '/') ||
		    (config.workdir && config.workdir[0] != '/'))
			goto next;

		if (!ovl_get_dirs(&config, &entries[i].lowerdir,
				  &entries[i].lowernum,
				  &entries[i].upperdir,
				  &entries[i].workdir)) {
			i++;
			if (i % ALLOC_NUM == 0) {
				allocated += sizeof(struct ovl_mnt_entry) * ALLOC_NUM;
				entries = srealloc(entries, allocated);
			}
		}
next:
		ovl_free_opt(&config);
		free(opt);
	}

	*ovl_mnt_entries = entries;
	*ovl_mnt_count = i;

	endmntent(fp);
	return 0;
}

static void ovl_scan_mount_exit(struct ovl_mnt_entry *ovl_mnt_entries,
				int ovl_mnt_count)
{
	int i,j;

	for (i = 0; i < ovl_mnt_count; i++) {
		for (j = 0; j < ovl_mnt_entries[i].lowernum; j++)
			free(ovl_mnt_entries[i].lowerdir[j]);
		free(ovl_mnt_entries[i].lowerdir);
		free(ovl_mnt_entries[i].upperdir);
		free(ovl_mnt_entries[i].workdir);
	}
	free(ovl_mnt_entries);
}

/*
 * Scan every mounted filesystem, check the overlay directories want
 * to check is already mounted. Check and fix an online overlay is not
 * allowed.
 *
 * Note: fsck may modify lower layers, so even match only one directory
 *       is triggered as mounted.
 * FIXME: We cannot distinguish mounted directories if overlayfs was
 *        mounted use relative path, so there may have misjudgment.
 */
int ovl_check_mount(struct ovl_fs *ofs, bool *mounted)
{
	struct ovl_mnt_entry *ovl_mnt_entries = NULL;
	int ovl_mnt_entry_count = 0;
	char *mounted_path = NULL;
	int i,j,k;
	int ret;

	ret = ovl_scan_mount_init(&ovl_mnt_entries, &ovl_mnt_entry_count);
	if (ret)
		return ret;

	/* Only check hard matching */
	for (i = 0; i < ovl_mnt_entry_count; i++) {
		/* Check lower */
		for (j = 0; j < ovl_mnt_entries[i].lowernum; j++) {
			for (k = 0; k < ofs->lower_num; k++) {
				if (!strcmp(ofs->lower_layer[k].path,
					    ovl_mnt_entries[i].lowerdir[j])) {
					mounted_path = ofs->lower_layer[k].path;
					*mounted = true;
					goto out;
				}
			}
		}

		/* Check upper */
		if (ofs->upper_layer.path && ovl_mnt_entries[i].upperdir &&
		    !(strcmp(ofs->upper_layer.path, ovl_mnt_entries[i].upperdir))) {
			mounted_path = ofs->upper_layer.path;
			*mounted = true;
			goto out;
		}

		/* Check worker */
		if (ofs->workdir.path && ovl_mnt_entries[i].workdir &&
		    !(strcmp(ofs->workdir.path, ovl_mnt_entries[i].workdir))) {
			mounted_path = ofs->workdir.path;
			*mounted = true;
			goto out;
		}
	}
out:
	ovl_scan_mount_exit(ovl_mnt_entries, ovl_mnt_entry_count);

	if (*mounted)
		print_info(_("WARNING: Dir %s is mounted\n"), mounted_path);

	return 0;
}
