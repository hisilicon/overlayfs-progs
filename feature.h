/*
 * Copyright (c) 2018 Huawei.  All Rights Reserved.
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

#ifndef OVL_FEATURE_H
#define OVL_FEATURE_H

#include "overlayfs.h"

/* Get feature from feature xattr on layer root dir and check validity */
int ovl_get_features(struct ovl_layer *layer);

/* Set feature to feature xattr on layer root dir */
int ovl_set_feature(struct ovl_layer *layer,
		    enum ovl_feature_type type,
		    __u64 mask);

/* Check feature set on one layer were support or not */
bool ovl_check_feature_support(struct ovl_layer *layer);

/* Display each layer's features */
void ovl_display_layer_features(struct ovl_layer *layer);

#define OVL_FEATURE_COMPAT_FUNCS(name, flagname) \
static inline bool ovl_has_feature_##name(struct ovl_layer *layer) \
{ \
	return !!(layer->compat & OVL_FEATURE_COMPAT_##flagname); \
} \
static inline int ovl_set_feature_##name(struct ovl_layer *layer) \
{ \
	return ovl_set_feature(layer, OVL_FEATURE_COMPAT, \
			OVL_FEATURE_COMPAT_##flagname); \
} \

#define OVL_FEATURE_RO_COMPAT_FUNCS(name, flagname) \
static inline bool ovl_has_feature_##name(struct ovl_layer *layer) \
{ \
	return !!(layer->ro_compat & OVL_FEATURE_RO_COMPAT_##flagname); \
} \
static inline int ovl_set_feature_##name(struct ovl_layer *layer) \
{ \
	return ovl_set_feature(layer, OVL_FEATURE_RO_COMPAT, \
			OVL_FEATURE_RO_COMPAT_##flagname); \
} \

#define OVL_FEATURE_INCOMPAT_FUNCS(name, flagname) \
static inline bool ovl_has_feature_##name(struct ovl_layer *layer) \
{ \
	return !!(layer->incompat & OVL_FEATURE_INCOMPAT_##flagname); \
} \
static inline int ovl_set_feature_##name(struct ovl_layer *layer) \
{ \
	return ovl_set_feature(layer, OVL_FEATURE_INCOMPAT, \
			OVL_FEATURE_INCOMPAT_##flagname); \
}

OVL_FEATURE_COMPAT_FUNCS(feature_set, FEATURE_SET);

#endif /* OVL_FEATURE_H */
