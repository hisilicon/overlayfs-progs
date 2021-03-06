/*
 * lib.c - Common things for all utilities
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


#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <fts.h>

#include "common.h"
#include "lib.h"
#include "path.h"

extern int flags;
extern int status;

static int ask_yn(const char *question, int def)
{
	char ans[16];

	print_info(_("%s ? [%s]: \n"), question, def ? _("y") : _("n"));
	fflush(stdout);
	while (fgets(ans, sizeof(ans)-1, stdin)) {
		if (ans[0] == '\n')
			return def;
		else if (!strcasecmp(ans, "y\n") || !strcasecmp(ans, "yes\n"))
			return 1;
		else if (!strcasecmp(ans, "n\n") || !strcasecmp(ans, "no\n"))
			return 0;
		else
			print_info(_("Illegal answer. Please input y/n or yes/no:"));
		fflush(stdout);
	}
	return def;
}

int ask_question(const char *question, int def)
{
	if (flags & FL_OPT_MASK) {
		def = (flags & FL_OPT_YES) ? 1 : (flags & FL_OPT_NO) ? 0 : def;
		print_info(_("%s? %s\n"), question, def ? _("y") : _("n"));
		return def;
	}

	return ask_yn(question, def);
}

/*
 * Get the value of the specified xattr
 *
 * @dirfd: base dir fd for relative pathname
 * @value: xattr value, can be NULL if empty value
 * @exit: xattr exit or not
 *
 * Return: a nonnegative value on success, -1 otherwise
 */
ssize_t get_xattr(int dirfd, const char *pathname, const char *xattrname,
		  char **value, bool *exist)
{
	char *buf = NULL;
	int fd;
	ssize_t ret;

	fd = openat(dirfd, pathname, O_CLOEXEC|O_NONBLOCK|O_NOFOLLOW|O_RDONLY);
	if (fd < 0) {
		print_err(_("Failed to openat %s: %s\n"),
			    pathname, strerror(errno));
		return -1;
	}

	ret = fgetxattr(fd, xattrname, NULL, 0);
	if (ret < 0) {
		if (errno != ENODATA && errno != ENOTSUP)
			goto fail;
		if (exist)
			*exist = false;
		ret = 0;
		goto out;
	}

	/* Zero size value means xattr exist but value unknown */
	if (exist)
		*exist = true;
	if (ret == 0 || !value)
		goto out;

	buf = smalloc(ret+1);
	ret = fgetxattr(fd, xattrname, buf, ret);
	if (ret <= 0)
		goto fail2;

	buf[ret] = '\0';
	*value = buf;
out:
	close(fd);
	return ret;

fail2:
	free(buf);
fail:
	print_err(_("Cannot fgetxattr %s %s: %s\n"), pathname,
		    xattrname, strerror(errno));
	goto out;
}

/*
 * Set the value of the specified xattr
 *
 * @dirfd: base dir fd for relative pathname
 * @value: xattr value, can be NULL if empty value
 * @size: size of xattr value
 */
int set_xattr(int dirfd, const char *pathname, const char *xattrname,
	      void *value, size_t size)
{
	int fd;
	int ret;

	fd = openat(dirfd, pathname, O_CLOEXEC|O_NONBLOCK|O_NOFOLLOW|O_RDONLY);
	if (fd < 0) {
		print_err(_("Failed to openat %s: %s\n"),
			    pathname, strerror(errno));
		return -1;
	}

	ret = fsetxattr(fd, xattrname, value, size, XATTR_CREATE);
	if (ret && errno != EEXIST)
		goto fail;

	if (errno == EEXIST) {
		ret = fsetxattr(fd, xattrname, value, size, XATTR_REPLACE);
		if (ret)
			goto fail;
	}

out:
	close(fd);
	return ret;
fail:
	print_err(_("Cannot fsetxattr %s %s: %s\n"), pathname,
		    xattrname, strerror(errno));
	goto out;
}

/* Remove the specified xattr */
int remove_xattr(int dirfd, const char *pathname, const char *xattrname)
{
	int fd;
	int ret;

	fd = openat(dirfd, pathname, O_CLOEXEC|O_NONBLOCK|O_NOFOLLOW|O_RDONLY);
	if (fd < 0) {
		print_err(_("Failed to openat %s: %s\n"),
			    pathname, strerror(errno));
		return -1;
	}

	ret = fremovexattr(fd, xattrname);
	if (ret)
		print_err(_("Cannot fremovexattr %s %s: %s\n"), pathname,
			    xattrname, strerror(errno));

	close(fd);
	return ret;
}


static void scan_entry_init(struct scan_ctx *sctx, FTSENT *ftsent)
{
	sctx->pathname = basename2(ftsent->fts_path, sctx->layer->path);
	sctx->filename = ftsent->fts_name;
	sctx->st = ftsent->fts_statp;
}

static inline int scan_check_entry(int (*do_check)(struct scan_ctx *),
				   struct scan_ctx *sctx)
{
	return do_check ? do_check(sctx) : 0;
}

/*
 * Scan specified directories and invoke callback to check/fix underlying
 * dirs of overlay filesystem
 */
int scan_dir(struct scan_ctx *sctx, struct scan_operations *sop)
{
	char *paths[2] = {sctx->layer->path, NULL};
	FTS *ftsp;
	FTSENT *ftsent;
	int ret = 0;

	ftsp = fts_open(paths, FTS_NOCHDIR | FTS_PHYSICAL, NULL);
	if (ftsp == NULL) {
		print_err(_("Failed to fts open %s:%s\n"),
			    sctx->layer->path, strerror(errno));
		return -1;
	}

	while ((ftsent = fts_read(ftsp)) != NULL) {
		/* Fillup base context */
		scan_entry_init(sctx, ftsent);

		print_debug(_("Scan:%-3s %2d %7lld   %-40s %-20s\n"),
			      (ftsent->fts_info == FTS_D) ? "d" :
			      (ftsent->fts_info == FTS_DNR) ? "dnr" :
			      (ftsent->fts_info == FTS_DP) ? "dp" :
			      (ftsent->fts_info == FTS_F) ? "f" :
			      (ftsent->fts_info == FTS_NS) ? "ns" :
			      (ftsent->fts_info == FTS_SL) ? "sl" :
			      (ftsent->fts_info == FTS_SLNONE) ? "sln" :
			      (ftsent->fts_info == FTS_DEFAULT) ? "df" : "???",
			      ftsent->fts_level,
			      (long long)ftsent->fts_statp->st_size,
			      ftsent->fts_path, sctx->layer->path);

		switch (ftsent->fts_info) {
		case FTS_F:
			sctx->result.files++;

			/* Check impurities */
			ret = scan_check_entry(sop->impurity, sctx);
			if (ret)
			        goto out;
			break;
		case FTS_DEFAULT:
			/* Check whiteouts */
			ret = scan_check_entry(sop->whiteout, sctx);
			if (ret)
				goto out;
			break;
		case FTS_D:
			sctx->result.directories++;

			/* Check redirect xattr */
			ret = scan_check_entry(sop->redirect, sctx);
			if (ret)
				goto out;

			/* Check impurities */
			ret = scan_check_entry(sop->impurity, sctx);
			if (ret)
				goto out;

			/* Save current dir data and create new one for subdir */
			ftsent->fts_pointer = sctx->dirdata;
			sctx->dirdata = smalloc(sizeof(struct scan_dir_data));
			break;
		case FTS_DP:
			/* Check impure xattr */
			ret = scan_check_entry(sop->impure, sctx);
			if (ret)
				goto out;

			/* Restore parent's dir data */
			free(sctx->dirdata);
			sctx->dirdata = ftsent->fts_pointer;
			break;
		case FTS_NS:
		case FTS_DNR:
		case FTS_ERR:
			print_err(_("Failed to fts read %s:%s\n"),
				    ftsent->fts_path, strerror(ftsent->fts_errno));
			ret = -1;
			goto out;
		}
	}
out:
	fts_close(ftsp);
	return ret;
}
