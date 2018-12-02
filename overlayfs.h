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

/*
 * This is common constant definition of overlayfs from the
 * Linux kernel.
 * (see fs/overlayfs/overlayfs.h and fs/overlayfs/super.c)
 */

#ifndef OVL_OVERLAYFS_H
#define OVL_OVERLAYFS_H

#include <linux/types.h>

/* Name of overlay filesystem type */
#define OVERLAY_NAME "overlay"

/* overlay max lower stacks */
#define OVL_MAX_STACK 500

/* Mount options */
#define OPT_LOWERDIR "lowerdir="
#define OPT_UPPERDIR "upperdir="
#define OPT_WORKDIR "workdir="

/* Xattr */
#define XATTR_TRUSTED_PREFIX	"trusted."
#define OVL_XATTR_PREFIX	XATTR_TRUSTED_PREFIX "overlay."
#define OVL_OPAQUE_XATTR	OVL_XATTR_PREFIX "opaque"
#define OVL_REDIRECT_XATTR	OVL_XATTR_PREFIX "redirect"
#define OVL_ORIGIN_XATTR	OVL_XATTR_PREFIX "origin"
#define OVL_IMPURE_XATTR	OVL_XATTR_PREFIX "impure"
#define OVL_FEATURE_XATTR	OVL_XATTR_PREFIX "feature"

/* Features */
#define OVL_FEATURE_COMPAT_FEATURE_SET		(0 << 1)
#define OVL_FEATURE_COMPAT_SUPP 		(OVL_FEATURE_COMPAT_FEATURE_SET)
#define OVL_FEATURE_COMPAT_UNKNOWN		(~OVL_FEATURE_COMPAT_SUPP)

#define OVL_FEATURE_RO_COMPAT_INDEX		(1 << 0)
#define OVL_FEATURE_RO_COMPAT_NFS_EXPORT	(1 << 1)
#define OVL_FEATURE_RO_COMPAT_SUPP		(0)
#define OVL_FEATURE_RO_COMPAT_UNKNOWN		(~OVL_FEATURE_RO_COMPAT_SUPP)

#define OVL_FEATURE_INCOMPAT_REDIRECT_DIR	(1 << 0)
#define OVL_FEATURE_INCOMPAT_METACOPY		(1 << 1)
#define OVL_FEATURE_INCOMPAT_SUPP		(0)
#define OVL_FEATURE_INCOMPAT_UNKNOWN		(~OVL_FEATURE_INCOMPAT_SUPP)

#define OVL_FEATURE_MAGIC	0xfe
#define OVL_FEATURE_VERSION_1	0x1

static inline bool ovl_has_unknown_compat_features(struct ovl_layer *layer)
{
	return !!(layer->compat & OVL_FEATURE_COMPAT_UNKNOWN);
}

static inline bool ovl_has_unknown_ro_compat_features(struct ovl_layer *layer)
{
	return !!(layer->ro_compat & OVL_FEATURE_RO_COMPAT_UNKNOWN);
}

static inline bool ovl_has_unknown_incompat_features(struct ovl_layer *layer)
{
	return !!(layer->incompat & OVL_FEATURE_INCOMPAT_UNKNOWN);
}

enum ovl_feature_type {
	OVL_FEATURE_COMPAT = 0,
	OVL_FEATURE_RO_COMPAT,
	OVL_FEATURE_INCOMPAT,
	OVL_FEATURE_TYPE_MAX
};

/* On-disk overlay layer features */
struct ovl_d_feature {
	__u8 magic;		/* 0xfe */
	__u8 version;		/* feature version */
	__u16 pad;
	__be64 compat;		/* compatible features */
	__be64 ro_compat;	/* read-only compatible features */
	__be64 incompat;	/* incompatible features */
} __attribute__((packed));

unsigned int ovl_split_lowerdirs(char *lower);
char *ovl_next_opt(char **s);

#endif /* OVL_OVERLAYFS_H */
