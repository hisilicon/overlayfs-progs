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

enum ovl_feature_type {
	OVL_FEATURE_COMPAT = 0,
	OVL_FEATURE_RO_COMPAT,
	OVL_FEATURE_INCOMPAT,
	OVL_FEATURE_TYPE_MAX
};

struct ovl_feature {
	enum ovl_feature_type type;
	__u64 mask;
	const char *string;
};

static struct ovl_feature ovl_feature_list[] = {
	/* Compatible */
	/* Read-only compatible */
	/* Incompatible */
	{OVL_FEATURE_INCOMPAT, OVL_FEATURE_INCOMPAT_REDIRECT_DIR, "redirect_dir"},
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

static void ovl_print_features(struct ovl_layer *layer)
{
	enum ovl_feature_type type;
	int total = 0;
	__u64 bit;
	__u64 masks[OVL_FEATURE_TYPE_MAX] = {
		[OVL_FEATURE_COMPAT] = layer->compat,
		[OVL_FEATURE_RO_COMPAT] = layer->ro_compat,
		[OVL_FEATURE_INCOMPAT] = layer->incompat,
	};
	__u64 *mask = masks;

	for (type = 0; type < OVL_FEATURE_TYPE_MAX; type++, mask++) {
		for (bit = 1; bit != 0; bit<<=1) {
			const char *p;

			if (!(*mask & bit))
				continue;

			p = ovl_feature2string(type, bit);
			print_info("%s ", p);
			total++;
		}
	}

	if (!total)
		print_info(_("none"));
	print_info(_("\n"));
}

/* Set feature xattr to layer's roo dir */
int ovl_set_feature(struct ovl_layer *layer)
{
	struct ovl_d_feature odf;

	print_debug(_("Set feature in %s root: %s: compat=%llx, "
		      "ro_compat=%llx, incompat=%llx\n"),
		      layer->type == OVL_UPPER ? "upper" : "lower",
		      layer->path, layer->compat, layer->ro_compat,
		      layer->incompat);

	odf.magic = OVL_FEATURE_MAGIC;
	odf.compat = cpu_to_be64(layer->compat);
	odf.ro_compat = cpu_to_be64(layer->ro_compat);
	odf.incompat = cpu_to_be64(layer->incompat);

	return set_xattr(layer->fd, ".", OVL_FEATURE_XATTR, &odf,
			 sizeof(struct ovl_d_feature));
}

static ssize_t ovl_get_feature(struct ovl_layer *layer,
			       struct ovl_d_feature **odf)
{
	return get_xattr(layer->fd, ".", OVL_FEATURE_XATTR,
			 (char **)odf, NULL);
}

/*
 * Get feature from feature xattr on layer root dir and check validity.
 *
 * Return 0 for a valid feature set or no feature set, EINVAL for an
 * invalid feature set, < 0 otherwise.
 */
int ovl_get_check_feature(struct ovl_layer *layer,
			  struct ovl_d_feature **odf)
{
	ssize_t ret;
	int err;

	/* Read layer feature xattr */
	ret = ovl_get_feature(layer, odf);
	if (ret <= 0) {
		err = (int)ret;
		*odf = NULL;
		goto out;
	}

	if (ret < (ssize_t)sizeof(struct ovl_d_feature)) {
		err = EINVAL;
		goto fail;

	}

	if ((*odf)->magic != OVL_FEATURE_MAGIC) {
		err = EINVAL;
		goto fail;
	}

	return 0;
out:
	return err;
fail:
	free(*odf);
	*odf = NULL;
	goto out;
}

/* Init an empty feature set to layer's root dir */
int ovl_init_empty_feature(struct ovl_layer *layer)
{
	layer->compat = 0;
	layer->ro_compat = 0;
	layer->incompat = 0;

	return ovl_set_feature(layer);
}

/* Check feature set on one layer were support or not */
bool ovl_check_feature_support(struct ovl_layer *layer)
{
	bool support = true;
	__u64 feature = 0;

	feature = layer->compat & OVL_FEATURE_COMPAT_UNKNOWN;
	if (feature) {
		print_info(_("Unknown optional compat feature: %llx\n"), feature);
		support = false;
	}

	feature = layer->ro_compat & OVL_FEATURE_RO_COMPAT_UNKNOWN;
	if (feature) {
		print_info(_("Unknown optional ro compat feature: %llx\n"), feature);
		support = false;
	}

	feature = layer->incompat & OVL_FEATURE_INCOMPAT_UNKNOWN;
	if (feature) {
		print_info(_("Unknown optional incompat feature: %llx\n"), feature);
		support = false;
	}

	return support;
}

/* Print each layer's features */
void ovl_print_feature_set(struct ovl_layer *layer)
{
	struct ovl_d_feature *odf = NULL;
	int err;

	if (layer->type == OVL_UPPER)
		print_info(_("Upper layer features: "));
	else
		print_info(_("Lower layer %d features: "), layer->stack);

	err = ovl_get_feature(layer, &odf);
	if (err < 0 || err == EINVAL) {
		print_info(_("invalid xattr\n"));
	} else if (!odf) {
		print_info(_("no xattr\n"));
	} else {
		layer->compat = be64_to_cpu(odf->compat);
		layer->ro_compat = be64_to_cpu(odf->ro_compat);
		layer->incompat = be64_to_cpu(odf->incompat);
		ovl_print_features(layer);
	}
}
