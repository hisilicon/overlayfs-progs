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

#ifndef OVL_FSCK_H
#define OVL_FSCK_H

/* Common return value */
#define FSCK_OK 		0	/* No errors */
#define FSCK_NONDESTRUCT	1	/* File system errors corrected */
#define FSCK_REBOOT		2	/* System should be rebooted */
#define FSCK_UNCORRECTED	4	/* File system errors left uncorrected */
#define FSCK_ERROR		8	/* Operational error */
#define FSCK_USAGE		16	/* Usage or syntax error */
#define FSCK_CANCELED		32	/* Aborted with a signal or ^C */
#define FSCK_LIBRARY		128	/* Shared library error */

/* Fsck status */
#define OVL_ST_INCONSISTNECY	(1 << 0)
#define OVL_ST_ABORT		(1 << 1)
#define OVL_ST_CHANGED		(1 << 2)

/* Option flags */
#define FL_VERBOSE		(0x00000001)	/* verbose */
#define FL_DSP_FEATURE		(0x00000002)	/* display features on each layer */


static inline void set_inconsistency(int *status)
{
	*status |= OVL_ST_INCONSISTNECY;
}

static inline void set_abort(int *status)
{
	*status |= OVL_ST_ABORT;
}

static inline void set_changed(int *status)
{
	*status |= OVL_ST_CHANGED;
}

/*
 * Feature set is not necessary for V1 underlying layers, but is necessary
 * for V2 underlying layers.
 */
static inline bool ovl_features_required(struct ovl_fs *ofs,
					 struct ovl_layer *layer)
{
	return !((ofs->config.format == OVL_FS_V1) ||
		((ofs->config.format == OVL_FS_UPPER_V2) &&
		 (layer->type == OVL_LOWER)));
}

#endif /* OVL_FSCK_H */
