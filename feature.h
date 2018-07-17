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

/* Set feature xattr to layer's roo dir */
int ovl_set_feature(struct ovl_layer *layer);

/* Get feature xattr to layer's root dir */
ssize_t ovl_get_feature(struct ovl_layer *layer,
			struct ovl_d_feature **odf);

/* Get feature from feature xattr on layer root dir and check validity */
int ovl_get_check_feature(struct ovl_layer *layer,
			  struct ovl_d_feature **odf);

/* Init an empty feature set to layer's root dir */
int ovl_init_empty_feature(struct ovl_layer *layer);

/* Check feature set on one layer were support or not */
bool ovl_check_feature_support(struct ovl_layer *layer);

/* Print each layer's features */
void ovl_print_feature_set(struct ovl_layer *layer);


#define OVL_FEATURE_COMPAT_FUNCS(name, feature) \
static inline bool ovl_has_feature_##name(struct ovl_layer *layer) \
{ \
	return ((layer->compat & OVL_FEATURE_COMPAT_##feature) != 0); \
} \
static inline int ovl_set_feature_##name(struct ovl_layer *layer) \
{ \
	layer->compat |= OVL_FEATURE_COMPAT_##feature; \
	return ovl_set_feature(layer); \
}

#define OVL_FEATURE_RO_COMPAT_FUNCS(name, feature) \
static inline bool ovl_has_feature_##name(struct ovl_layer *layer) \
{ \
	return ((layer->ro_compat & OVL_FEATURE_RO_COMPAT_##feature) != 0); \
} \
static inline int ovl_set_feature_##name(struct ovl_layer *layer) \
{ \
	layer->ro_compat |= OVL_FEATURE_RO_COMPAT_##feature; \
	return ovl_set_feature(layer); \
}

#define OVL_FEATURE_INCOMPAT_FUNCS(name, feature) \
static inline bool ovl_has_feature_##name(struct ovl_layer *layer) \
{ \
	return ((layer->incompat & OVL_FEATURE_INCOMPAT_##feature) != 0); \
} \
static inline int ovl_set_feature_##name(struct ovl_layer *layer) \
{ \
	layer->incompat |= OVL_FEATURE_INCOMPAT_##feature; \
	return ovl_set_feature(layer); \
}

OVL_FEATURE_INCOMPAT_FUNCS(redirect_dir, REDIRECT_DIR);

#endif /* OVL_FEATURE_H */
