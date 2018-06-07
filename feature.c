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
