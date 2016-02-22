/*
 * Copyright (c) 2013-2014 Jens Kuske <jenskuske@gmail.com>
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
#include "kernel-headers/g2d_driver.h"

void g2d_fill(rgba_surface_t *dest, const VdpRect *dest_rect, uint32_t color)
{
	g2d_fillrect args;

	args.flag = G2D_FIL_PIXEL_ALPHA;
	args.dst_image.addr[0] = cedrus_mem_get_phys_addr(dest->data);
	args.dst_image.w = dest->width;
	args.dst_image.h = dest->height;
	args.dst_image.format = G2D_FMT_ARGB_AYUV8888;
	args.dst_image.pixel_seq = G2D_SEQ_NORMAL;
	if (dest_rect)
	{
		args.dst_rect.x = dest_rect->x0;
		args.dst_rect.y = dest_rect->y0;
		args.dst_rect.w = dest_rect->x1 - dest_rect->x0;
		args.dst_rect.h = dest_rect->y1 - dest_rect->y0;
	}
	else
	{
		args.dst_rect.x = 0;
		args.dst_rect.y = 0;
		args.dst_rect.w = dest->width;
		args.dst_rect.h = dest->height;
	}
	args.color = color & 0xffffff ;
	args.alpha = color >> 24;

	ioctl(dest->device->g2d_fd, G2D_CMD_FILLRECT, &args);
}

void g2d_blit(rgba_surface_t *dest, const VdpRect *dest_rect, rgba_surface_t *src, const VdpRect *src_rect)
{
	g2d_blt args;

	args.flag = (dest->flags & RGBA_FLAG_NEEDS_CLEAR) ? G2D_BLT_NONE : G2D_BLT_PIXEL_ALPHA;
	args.src_image.addr[0] = cedrus_mem_get_phys_addr(src->data);
	args.src_image.w = src->width;
	args.src_image.h = src->height;
	args.src_image.format = G2D_FMT_ARGB_AYUV8888;
	args.src_image.pixel_seq = G2D_SEQ_NORMAL;
	args.src_rect.x = src_rect->x0;
	args.src_rect.y = src_rect->y0;
	args.src_rect.w = src_rect->x1 - src_rect->x0;
	args.src_rect.h = src_rect->y1 - src_rect->y0;
	args.dst_image.addr[0] = cedrus_mem_get_phys_addr(dest->data);
	args.dst_image.w = dest->width;
	args.dst_image.h = dest->height;
	args.dst_image.format = G2D_FMT_ARGB_AYUV8888;
	args.dst_image.pixel_seq = G2D_SEQ_NORMAL;
	args.dst_x = dest_rect->x0;
	args.dst_y = dest_rect->y0;
	args.color = 0;
	args.alpha = 0;

	ioctl(dest->device->g2d_fd, G2D_CMD_BITBLT, &args);
}
