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

	err = EINVAL;
	if (!(odf->compat & cpu_to_be64(OVL_FEATURE_COMPAT_FEATURE_SET)))
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

	/* Set "feature set" feature automatically */
	compat = layer->compat | OVL_FEATURE_COMPAT_FEATURE_SET;
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
	layer->compat = compat;
	*feature = *temp;
	return err;
}

/* Check feature set on one layer were support or not */
bool ovl_check_feature_support(struct ovl_layer *layer)
{
	bool support = true;

	if (ovl_has_unknown_compat_features(layer)) {
		print_info(_("Unknown compat features %llx\n"),
			     layer->compat & OVL_FEATURE_COMPAT_UNKNOWN);
		support = false;
	}

	if (ovl_has_unknown_ro_compat_features(layer)) {
		print_info(_("Unknown ro_compat features %llx\n"),
			     layer->ro_compat & OVL_FEATURE_RO_COMPAT_UNKNOWN);
		support = false;
	}

	if (ovl_has_unknown_incompat_features(layer)) {
		print_info(_("Unknown incompat features %llx\n"),
			     layer->incompat & OVL_FEATURE_INCOMPAT_UNKNOWN);
		support = false;
	}

	return support;
}

struct ovl_feature {
	enum ovl_feature_type type;
	__u64 mask;
	const char *string;
};

static struct ovl_feature ovl_feature_list[] = {
	/* Compatible */
	{OVL_FEATURE_COMPAT, OVL_FEATURE_COMPAT_FEATURE_SET, "feature_set"},
	/* Read-only compatible */
	{OVL_FEATURE_RO_COMPAT, OVL_FEATURE_RO_COMPAT_INDEX, "index"},
	{OVL_FEATURE_RO_COMPAT, OVL_FEATURE_RO_COMPAT_NFS_EXPORT, "nfs_export"},
	/* Incompatible */
	{OVL_FEATURE_INCOMPAT, OVL_FEATURE_INCOMPAT_REDIRECT_DIR, "redirect_dir"},
	{OVL_FEATURE_INCOMPAT, OVL_FEATURE_INCOMPAT_METACOPY, "metacopy"},
	{0, 0, NULL}
};

/*
 * Switch feature mask bit to string, return the feature string directly
 * if the feature mask is known, return feature compatible type and bit
 * number otherwise.
 */
static const char *ovl_feature2string(enum ovl_feature_type type, __u64 mask)
{
	struct ovl_feature *of;
	static char string[32];
	int num;

	/* Known feature */
	for (of = ovl_feature_list; of->string; of++) {
		if (type == of->type && mask == of->mask)
			return of->string;
	}

	/* Unknown feature */
	for (num = 0; mask >>= 1; num++);
	if (type == OVL_FEATURE_COMPAT)
		snprintf(string, sizeof(string), "FEATURE_COMPAT_BIT%d", num);
	else if (type == OVL_FEATURE_RO_COMPAT)
		snprintf(string, sizeof(string), "FEATURE_RO_COMPAT_BIT%d", num);
	else if (type == OVL_FEATURE_INCOMPAT)
		snprintf(string, sizeof(string), "FEATURE_INCOMPTA_BIT%d", num);
	else
		snprintf(string, sizeof(string), "FEATURE_UNKNOWN");
	return string;
}

/* Display each layer's features */
void ovl_display_layer_features(struct ovl_layer *layer)
{
	enum ovl_feature_type type;
	__u64 masks[OVL_FEATURE_TYPE_MAX] = {
		[OVL_FEATURE_COMPAT] = layer->compat,
		[OVL_FEATURE_RO_COMPAT] = layer->ro_compat,
		[OVL_FEATURE_INCOMPAT] = layer->incompat,
	};
	__u64 *mask = masks;
	__u64 bit;
	int total;
	int err;

	if (layer->type == OVL_UPPER)
		print_info(_("Upper layer %s features: "), layer->path);
	else
		print_info(_("Lower layer %d %s features: "),
			     layer->stack, layer->path);

	err = ovl_get_features(layer);
	if (err < 0 || err == EINVAL) {
		print_info(_("invalid xattr\n"));
		return;
	} else if (err == ENODATA) {
		print_info(_("no xattr\n"));
		return;
	} else if (err == ENOTSUP) {
		print_info(_("unsupport xattr\n"));
		return;
	}

	for (type = 0, total = 0; type < OVL_FEATURE_TYPE_MAX;
	     type++, mask++) {
		for (bit = 1; bit != 0; bit<<=1) {
			const char *p;

			if (!(*mask & bit))
				continue;

			p = ovl_feature2string(type, bit);
			print_info(_("%s "), p);
			total++;
		}
	}
	if (total == 0)
		print_info(_("none"));

	print_info(_("\n"));
}
