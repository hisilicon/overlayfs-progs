/*
 * ovl.c - Common overlay things for all utilities
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
#include "ovl.h"
#include "lib.h"
#include "overlayfs.h"

/*
 * Open underlying dirs (include upper dir and lower dirs), check system
 * file descriptor limits and try to expend it if necessary.
 */
int ovl_open_dirs(struct ovl_fs *ofs)
{
	unsigned int i;
	struct rlimit rlim;
	rlim_t rlim_need = ofs->lower_num + 20;

	/* If RLIMIT_NOFILE limit is small than we need, try to expand limit */
	if ((getrlimit(RLIMIT_NOFILE, &rlim))) {
		print_err(_("Failed to getrlimit:%s\n"), strerror(errno));
		return -1;
	}
	if (rlim.rlim_cur < rlim_need) {
		print_info(_("Process fd number limit=%lu "
			     "too small, need %lu\n"),
			     rlim.rlim_cur, rlim_need);

		rlim.rlim_cur = rlim_need;
		if (rlim.rlim_max < rlim.rlim_cur)
			rlim.rlim_max = rlim.rlim_cur;

		if ((setrlimit(RLIMIT_NOFILE, &rlim))) {
			print_err(_("Failed to setrlimit:%s\n"),
				    strerror(errno));
			return -1;
		}
	}

	if (ofs->upper_layer.path) {
		ofs->upper_layer.fd = open(ofs->upper_layer.path,
			       O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC);
		if (ofs->upper_layer.fd < 0) {
			print_err(_("Failed to open %s:%s\n"),
				    ofs->upper_layer.path, strerror(errno));
			return -1;
		}

		ofs->workdir.fd = open(ofs->workdir.path, O_RDONLY|O_NONBLOCK|
				       O_DIRECTORY|O_CLOEXEC);
		if (ofs->workdir.fd < 0) {
			print_err(_("Failed to open %s:%s\n"),
				    ofs->workdir.path, strerror(errno));
			goto err;
		}
	}

	for (i = 0; i < ofs->lower_num; i++) {
		ofs->lower_layer[i].fd = open(ofs->lower_layer[i].path,
				  O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC);
		if (ofs->lower_layer[i].fd < 0) {
			print_err(_("Failed to open %s:%s\n"),
				    ofs->lower_layer[i].path, strerror(errno));
			goto err2;
		}
	}

	return 0;
err2:
	for (i--; i >= 0; i--) {
		close(ofs->lower_layer[i].fd);
		ofs->lower_layer[i].fd = 0;
	}
	close(ofs->workdir.fd);
	ofs->workdir.fd = 0;
err:
	close(ofs->upper_layer.fd);
	ofs->upper_layer.fd = 0;
	return -1;
}

/* Cleanup underlying directories buffers */
void ovl_clean_dirs(struct ovl_fs *ofs)
{
	int i;

	for (i = 0; i < ofs->lower_num; i++) {
		if (ofs->lower_layer && ofs->lower_layer[i].fd) {
			close(ofs->lower_layer[i].fd);
			ofs->lower_layer[i].fd = 0;
		}
		free(ofs->lower_layer[i].path);
		ofs->lower_layer[i].path = NULL;
	}
	free(ofs->lower_layer);
	ofs->lower_layer = NULL;
	ofs->lower_num = 0;

	if (ofs->upper_layer.path) {
		close(ofs->upper_layer.fd);
		ofs->upper_layer.fd = 0;
		free(ofs->upper_layer.path);
		ofs->upper_layer.path = NULL;
		close(ofs->workdir.fd);
		ofs->workdir.fd = 0;
		free(ofs->workdir.path);
		ofs->workdir.path = NULL;
	}
}

/* Do basic check for one layer */
int ovl_basic_check_layer(struct ovl_layer *layer)
{
	struct statfs statfs;
	ssize_t ret;
	int err;

	/* Check the underlying layer is read-only or not */
	err = fstatfs(layer->fd, &statfs);
	if (err) {
		print_err(_("fstatfs failed:%s\n"), strerror(errno));
		return -1;
	}

	if (statfs.f_flags & ST_RDONLY)
		layer->flag |= FS_LAYER_RO;

	/* Check the underlying layer support xattr or not */
	ret = fgetxattr(layer->fd, OVL_XATTR_PREFIX, NULL, 0);
	if (ret < 0 && errno != ENOTSUP && errno != ENODATA) {
		print_err(_("flistxattr failed:%s\n"), strerror(errno));
		return -1;
	} else if (ret >= 0 || errno == ENODATA) {
		layer->flag |= FS_LAYER_XATTR;
	}

	return 0;
}


int ovl_ask_action(const char *description, const char *pathname,
		   int dirtype, int stack,
		   const char *question, int action)
{
	if (dirtype == OVL_UPPER || dirtype == OVL_WORK)
		print_info(_("%s: \"%s\" in %s "),
			     description, pathname, "upperdir");
	else
		print_info(_("%s: \"%s\" in %s-%d "),
			     description, pathname, "lowerdir", stack);

	return ask_question(question, action);
}

int ovl_ask_question(const char *question, const char *pathname,
		     int dirtype, int stack, int action)
{
	if (dirtype == OVL_UPPER || dirtype == OVL_WORK)
		print_info(_("%s: \"%s\" in %s "),
			     question, pathname, "upperdir");
	else
		print_info(_("%s: \"%s\" in %s-%d "),
			     question, pathname, "lowerdir", stack);

	return ask_question("", action);
}
