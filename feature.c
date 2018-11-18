/*
 * feature.c - Check underlying layer feature of overlayfs
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

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <stdbool.h>
#include <fcntl.h>

#include "common.h"
#include "ovl.h"
#include "lib.h"
#include "check.h"
#include "overlayfs.h"
#include "feature.h"

extern char *program_name;

/*
 * Get features from the layer's feature xattr on the root dir and
 * check validity.
 *
 * Return 0 for a valid feature set found, ENODATA for no feature
 * set found, ENOTSUP for a high version feature set found, EINVAL
 * for an invalid feature set, < 0 otherwise.
 */
int ovl_get_features(struct ovl_layer *layer)
{
	struct ovl_d_feature *odf;
	ssize_t ret;
	int err;

	/* Read layer feature xattr */
	ret = get_xattr(layer->fd, ".", OVL_FEATURE_XATTR,
			(char **)&odf, NULL);
	if (ret < 0)
		return (int)ret;
	if (ret == 0)
		return ENODATA;

	/* Check validity */
	err = EINVAL;
	if (ret < (ssize_t)sizeof(struct ovl_d_feature))
		goto out;
	if (odf->magic != OVL_FEATURE_MAGIC)
		goto out;

	err = ENOTSUP;
	if (odf->version > OVL_FEATURE_VERSION_1)
		goto out;

	err = 0;
	layer->compat = be64_to_cpu(odf->compat);
	layer->ro_compat = be64_to_cpu(odf->ro_compat);
	layer->incompat = be64_to_cpu(odf->incompat);

	print_debug(_("Get features from %s root: %s: compat=%llx, "
		      "ro_compat=%llx, incompat=%llx\n"),
		      layer->type == OVL_UPPER ? "upper" : "lower",
		      layer->path, layer->compat, layer->ro_compat,
		      layer->incompat);
out:
	free(odf);
	return err;
}

/* Set feature to the feature set xattr on one layer root dir */
int ovl_set_feature(struct ovl_layer *layer,
		    enum ovl_feature_type type,
		    __u64 mask)
{
	struct ovl_d_feature odf = { };
	__u64 compat, ro_compat, incompat;
	__u64 *feature, *temp;
	int err;

	switch (type) {
	case OVL_FEATURE_COMPAT:
		feature = &layer->compat;
		temp = &compat;
		break;
	case OVL_FEATURE_RO_COMPAT:
		feature = &layer->ro_compat;
		temp = &ro_compat;
		break;
	case OVL_FEATURE_INCOMPAT:
		feature = &layer->incompat;
		temp = &incompat;
		break;
	default:
		return -1;
	}

	if ((*feature) & mask)
		return 0;

	compat = layer->compat;
	ro_compat = layer->ro_compat;
	incompat = layer->incompat;
	*temp |= mask;

	/* Set on-disk feature set xattr */
	odf.magic = OVL_FEATURE_MAGIC;
	odf.version = OVL_FEATURE_VERSION_1;
	odf.compat = cpu_to_be64(compat);
	odf.ro_compat = cpu_to_be64(ro_compat);
	odf.incompat = cpu_to_be64(incompat);

	print_debug(_("Set feature set on %s root: %s: compat=%llx, "
		      "ro_compat=%llx, incompat=%llx\n"),
		      layer->type == OVL_UPPER ? "upper" : "lower",
		      layer->path, compat, ro_compat, incompat);

	err = set_xattr(layer->fd, ".", OVL_FEATURE_XATTR, &odf,
			sizeof(struct ovl_d_feature));
	if (err)
		return err;

	/* Update in-memory feature set */
	*feature = *temp;
	return err;
}
