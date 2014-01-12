/*
 * Copyright (c) 2013 Jens Kuske <jenskuske@gmail.com>
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
#include <stropts.h>
#include "vdpau_private.h"
#include "ve.h"
#include "g2d_driver.h"

VdpStatus vdp_bitmap_surface_create(VdpDevice device, VdpRGBAFormat rgba_format, uint32_t width, uint32_t height, VdpBool frequently_accessed, VdpBitmapSurface *surface)
{
	if (!surface)
		return VDP_STATUS_INVALID_POINTER;

	if (rgba_format != VDP_RGBA_FORMAT_B8G8R8A8 && rgba_format != VDP_RGBA_FORMAT_R8G8B8A8)
		return VDP_STATUS_INVALID_RGBA_FORMAT;

	if (width < 1 || width > 8192 || height < 1 || height > 8192)
		return VDP_STATUS_INVALID_SIZE;

	device_ctx_t *dev = handle_get(device);
	if (!dev)
		return VDP_STATUS_INVALID_HANDLE;

	bitmap_surface_ctx_t *out = calloc(1, sizeof(bitmap_surface_ctx_t));
	if (!out)
		return VDP_STATUS_RESOURCES;

	out->width = width;
	out->height = height;
	out->rgba_format = rgba_format;
	out->device = dev;
	out->frequently_accessed = frequently_accessed;

	out->data = ve_malloc(width * height * 4);
	if (!out->data)
	{
		free(out);
		return VDP_STATUS_RESOURCES;
	}

	g2d_fillrect args;

	args.flag = G2D_FIL_NONE;
	args.dst_image.addr[0] = ve_virt2phys(out->data) + 0x40000000;
	args.dst_image.w = width;
	args.dst_image.h = height;
	args.dst_image.format = G2D_FMT_ARGB_AYUV8888;
	args.dst_image.pixel_seq = G2D_SEQ_NORMAL;
	args.dst_rect.x = 0;
	args.dst_rect.y = 0;
	args.dst_rect.w = width;
	args.dst_rect.h = height;
	args.color = 0;
	args.alpha = 0;

	ioctl(dev->g2d_fd, G2D_CMD_FILLRECT, &args);

	int handle = handle_create(out);
	if (handle == -1)
	{
		ve_free(out->data);
		free(out);
		return VDP_STATUS_RESOURCES;
	}

	*surface = handle;

	return VDP_STATUS_OK;
}

VdpStatus vdp_bitmap_surface_destroy(VdpBitmapSurface surface)
{
	bitmap_surface_ctx_t *out = handle_get(surface);
	if (!out)
		return VDP_STATUS_INVALID_HANDLE;

	ve_free(out->data);

	handle_destroy(surface);
	free(out);

	return VDP_STATUS_OK;
}

VdpStatus vdp_bitmap_surface_get_parameters(VdpBitmapSurface surface, VdpRGBAFormat *rgba_format, uint32_t *width, uint32_t *height, VdpBool *frequently_accessed)
{
	bitmap_surface_ctx_t *out = handle_get(surface);
	if (!out)
		return VDP_STATUS_INVALID_HANDLE;

	if (rgba_format)
		*rgba_format = out->rgba_format;

	if (width)
		*width = out->width;

	if (height)
		*height = out->height;

	if (frequently_accessed)
		*frequently_accessed = out->frequently_accessed;

	return VDP_STATUS_OK;
}

VdpStatus vdp_bitmap_surface_put_bits_native(VdpBitmapSurface surface, void const *const *source_data, uint32_t const *source_pitches, VdpRect const *destination_rect)
{
	bitmap_surface_ctx_t *out = handle_get(surface);
	if (!out)
		return VDP_STATUS_INVALID_HANDLE;

	VdpRect d_rect = {0, 0, out->width, out->height};
	if (destination_rect)
		d_rect = *destination_rect;

	if (0 == d_rect.x0 && out->width == d_rect.x1 && source_pitches[0] == d_rect.x1) {
		// full width
		const int bytes_to_copy =
			(d_rect.x1 - d_rect.x0) * (d_rect.y1 - d_rect.y0) * 4;
		memcpy(out->data + d_rect.y0 * out->width * 4,
			   source_data[0], bytes_to_copy);
	} else {
		const unsigned int bytes_in_line = (d_rect.x1-d_rect.x0) * 4;
		unsigned int y;
		for (y = d_rect.y0; y < d_rect.y1; y ++) {
			memcpy(out->data + (y * out->width + d_rect.x0) * 4,
				   source_data[0] + (y - d_rect.y0) * source_pitches[0],
				   bytes_in_line);
		}
	}

	return VDP_STATUS_OK;
}

VdpStatus vdp_bitmap_surface_query_capabilities(VdpDevice device, VdpRGBAFormat surface_rgba_format, VdpBool *is_supported, uint32_t *max_width, uint32_t *max_height)
{
	if (!is_supported || !max_width || !max_height)
		return VDP_STATUS_INVALID_POINTER;

	device_ctx_t *dev = handle_get(device);
	if (!dev)
		return VDP_STATUS_INVALID_HANDLE;

	*is_supported = (surface_rgba_format == VDP_RGBA_FORMAT_R8G8B8A8 || surface_rgba_format == VDP_RGBA_FORMAT_B8G8R8A8);
	*max_width = 8192;
	*max_height = 8192;

	return VDP_STATUS_OK;
}
