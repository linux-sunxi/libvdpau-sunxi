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

#include <string.h>
#include <cedrus/cedrus.h>
#include <sys/ioctl.h>
#include "vdpau_private.h"
#include "rgba_pixman.h"
#include "pixman.h"

static pixman_color_t uint32_to_pcolor(uint32_t color)
{
	pixman_color_t pcolor;

	/* Fetch 8bit values */
	uint16_t alpha = color >> 24;
	uint16_t red   = color >> 16 & 0xff;
	uint16_t green = color >> 8 & 0xff;
	uint16_t blue  = color & 0xff;

	/* Premultiply with alpha value. Pixman only deals with premultiplied alpha. */
	red = red     * alpha / 255;
	green = green * alpha / 255;
	blue =  blue  * alpha / 255;

	/* Convert 8bit -> 16bit for pixman */
	pcolor.alpha = ((alpha & 0xFF) << 8) | (alpha & 0xFF);
	pcolor.red =   ((red   & 0xFF) << 8) | (red   & 0xFF);
	pcolor.green = ((green & 0xFF) << 8) | (green & 0xFF);
	pcolor.blue =  ((blue  & 0xFF) << 8) | (blue  & 0xFF);

	return pcolor;
}

VdpStatus vdp_pixman_ref(rgba_surface_t *rgba)
{
	rgba->pimage = pixman_image_create_bits(PIXMAN_a8r8g8b8,
						rgba->width, rgba->height,
						cedrus_mem_get_pointer(rgba->data),
						(rgba->width * 4));

	return VDP_STATUS_OK;
}

VdpStatus vdp_pixman_unref(rgba_surface_t *rgba)
{
	pixman_image_unref(rgba->pimage);

	return VDP_STATUS_OK;
}

VdpStatus vdp_pixman_blit(rgba_surface_t *rgba_dst, const VdpRect *dst_rect,
			  rgba_surface_t *rgba_src, const VdpRect *src_rect)
{
	pixman_image_t *dst;
	pixman_image_t *src;
	pixman_transform_t transform;
	double fscale_x, fscale_y;

	dst = rgba_dst->pimage;
	src = rgba_src->pimage;

	if ((dst_rect->x1 - dst_rect->x0) == 0 ||
	    (src_rect->x1 - src_rect->x0) == 0 ||
	    (dst_rect->y1 - dst_rect->y0) == 0 ||
	    (src_rect->y1 - src_rect->y0) == 0 )
		goto zero_size_blit;

	/* Transform src_rct to dest_rct size */
	fscale_x = (double)(dst_rect->x1 - dst_rect->x0) / (double)(src_rect->x1 - src_rect->x0);
	fscale_y = (double)(dst_rect->y1 - dst_rect->y0) / (double)(src_rect->y1 - src_rect->y0);
	pixman_transform_init_identity(&transform);
	pixman_transform_scale(&transform, NULL,
			       pixman_double_to_fixed(fscale_x),
			       pixman_double_to_fixed(fscale_y));
	pixman_image_set_transform(src, &transform);

	/* Composite to the dest_img */
	pixman_image_composite32(
		PIXMAN_OP_OVER, src, NULL, dst,
		(src_rect->x0 * fscale_x), (src_rect->y0 * fscale_y),
		0, 0,
		dst_rect->x0, dst_rect->y0,
		(dst_rect->x1 - dst_rect->x0), (dst_rect->y1 - dst_rect->y0));

	return VDP_STATUS_OK;

zero_size_blit:
	VDPAU_DBG("Zero size blit requested!");
	return VDP_STATUS_ERROR;
}

VdpStatus vdp_pixman_fill(rgba_surface_t *rgba_dst, const VdpRect *dst_rect,
			  uint32_t color)
{

	/* Define default values if dst_rect == NULL) */
	VdpRect rect = {
		.x0 = 0,
		.y0 = 0,
		.x1 = rgba_dst->width,
		.y1 = rgba_dst->height
	};

	if (dst_rect)
	{
		rect.x0 = dst_rect->x0;
		rect.y0 = dst_rect->y0;
		rect.x1 = dst_rect->x1;
		rect.y1 = dst_rect->y1;
	}

	/* Check for zerosized rect */
	if ((rect.x1 - rect.x0) == 0 ||
	    (rect.y1 - rect.y0) == 0)
		goto zero_size_fill;

	pixman_color_t pcolor = uint32_to_pcolor(color);
	pixman_image_t *src = pixman_image_create_solid_fill(&pcolor);

	pixman_image_t *dst = rgba_dst->pimage;

	/* Composite to the dest_img */
	pixman_image_composite32(
		PIXMAN_OP_SRC, src, NULL, dst,
		0, 0, 0, 0,
		rect.x0, rect.y0,
		(rect.x1 - rect.x0), (rect.y1 - rect.y0));

	pixman_image_unref(src);

	return VDP_STATUS_OK;

zero_size_fill:
	VDPAU_DBG("Zero size fill requested!");
	return VDP_STATUS_ERROR;
}
