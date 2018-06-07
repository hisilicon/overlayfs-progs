/*
 * Copyright (c) 2017 Huawei.  All Rights Reserved.
 * Author: zhangyi (F) <yi.zhang@huawei.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef OVL_OVL_H
#define OVL_OVL_H


/* Scan path type */
#define OVL_UPPER	0
#define OVL_LOWER	1
#define OVL_WORK	2
#define OVL_PTYPE_MAX	3

/* Information for each underlying layer */
struct ovl_layer {
	char *path;		/* root dir path for this layer */
	int fd;			/* root dir fd for this layer */
	int type;		/* OVL_UPPER or OVL_LOWER */
	int stack;		/* lower layer stack number, OVL_LOWER use only */
	int flag;		/* special flag for this layer */
	__u64 compat;		/* compatible features */
	__u64 ro_compat;	/* read-only compatible features */
	__u64 incompat;		/* incompatible features */
};

/* Information for the whole overlay filesystem */
struct ovl_fs {
	struct ovl_layer upper_layer;
	struct ovl_layer *lower_layer;
	int lower_num;
	struct ovl_layer workdir;
};

/* Open underlying dirs */
int ovl_open_dirs(struct ovl_fs *ofs);

/* Close underlying dirs */
void ovl_clean_dirs(struct ovl_fs *ofs);

/* Do basic check for one layer */
int ovl_basic_check_layer(struct ovl_layer *layer);

/* Ask user */
int ovl_ask_action(const char *description, const char *pathname,
		   int dirtype, int stack,
		   const char *question, int action);
int ovl_ask_question(const char *question, const char *pathname,
		     int dirtype, int stack, int action);

#endif /* OVL_OVL_H */
