/*
 * Copyright (c) 2016 Andreas Baierl <ichgeh@imkreisrum.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifndef __RGBA_PIXMAN_H__
#define __RGBA_PIXMAN_H__

VdpStatus vdp_pixman_ref(rgba_surface_t *rgba);
VdpStatus vdp_pixman_unref(rgba_surface_t *rgba);
VdpStatus vdp_pixman_blit(rgba_surface_t *dst, const VdpRect *dst_rect,
			  rgba_surface_t *src, const VdpRect *src_rect);
VdpStatus vdp_pixman_fill(rgba_surface_t *dst, const VdpRect *dst_rect,
			  uint32_t color);

#endif
