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

#include "vdpau_private.h"
#include "rgba.h"

VdpStatus vdp_bitmap_surface_create(VdpDevice device,
                                    VdpRGBAFormat rgba_format,
                                    uint32_t width,
                                    uint32_t height,
                                    VdpBool frequently_accessed,
                                    VdpBitmapSurface *surface)
{
	int ret = VDP_STATUS_OK;

	if (!surface)
		return VDP_STATUS_INVALID_POINTER;

	device_ctx_t *dev = handle_get(device);
	if (!dev)
		return VDP_STATUS_INVALID_HANDLE;

	bitmap_surface_ctx_t *out = handle_create(sizeof(*out), surface);
	if (!out)
		return VDP_STATUS_RESOURCES;

	out->frequently_accessed = frequently_accessed;

	ret = rgba_create(&out->rgba, dev, width, height, rgba_format);
	if (ret != VDP_STATUS_OK)
	{
		handle_destroy(*surface);
		return ret;
	}

	return VDP_STATUS_OK;
}

VdpStatus vdp_bitmap_surface_destroy(VdpBitmapSurface surface)
{
	bitmap_surface_ctx_t *out = handle_get(surface);
	if (!out)
		return VDP_STATUS_INVALID_HANDLE;

	rgba_destroy(&out->rgba);

	handle_destroy(surface);

	return VDP_STATUS_OK;
}

VdpStatus vdp_bitmap_surface_get_parameters(VdpBitmapSurface surface,
                                            VdpRGBAFormat *rgba_format,
                                            uint32_t *width,
                                            uint32_t *height,
                                            VdpBool *frequently_accessed)
{
	bitmap_surface_ctx_t *out = handle_get(surface);
	if (!out)
		return VDP_STATUS_INVALID_HANDLE;

	if (rgba_format)
		*rgba_format = out->rgba.format;

	if (width)
		*width = out->rgba.width;

	if (height)
		*height = out->rgba.height;

	if (frequently_accessed)
		*frequently_accessed = out->frequently_accessed;

	return VDP_STATUS_OK;
}

VdpStatus vdp_bitmap_surface_put_bits_native(VdpBitmapSurface surface,
                                             void const *const *source_data,
                                             uint32_t const *source_pitches,
                                             VdpRect const *destination_rect)
{
	bitmap_surface_ctx_t *out = handle_get(surface);
	if (!out)
		return VDP_STATUS_INVALID_HANDLE;

	rgba_put_bits_native(&out->rgba, source_data, source_pitches, destination_rect);

	return VDP_STATUS_OK;
}

VdpStatus vdp_bitmap_surface_query_capabilities(VdpDevice device,
                                                VdpRGBAFormat surface_rgba_format,
                                                VdpBool *is_supported,
                                                uint32_t *max_width,
                                                uint32_t *max_height)
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
