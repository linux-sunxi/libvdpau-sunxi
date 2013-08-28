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

VdpStatus vdp_bitmap_surface_create(VdpDevice device, VdpRGBAFormat rgba_format, uint32_t width, uint32_t height, VdpBool frequently_accessed, VdpBitmapSurface *surface)
{
	device_ctx_t *dev = handle_get(device);
	if (!dev)
		return VDP_STATUS_INVALID_HANDLE;



	return VDP_STATUS_ERROR;
}

VdpStatus vdp_bitmap_surface_destroy(VdpBitmapSurface surface)
{


	return VDP_STATUS_ERROR;
}

VdpStatus vdp_bitmap_surface_get_parameters(VdpBitmapSurface surface, VdpRGBAFormat *rgba_format, uint32_t *width, uint32_t *height, VdpBool *frequently_accessed)
{


	return VDP_STATUS_ERROR;
}

VdpStatus vdp_bitmap_surface_put_bits_native(VdpBitmapSurface surface, void const *const *source_data, uint32_t const *source_pitches, VdpRect const *destination_rect)
{


	return VDP_STATUS_ERROR;
}

VdpStatus vdp_bitmap_surface_query_capabilities(VdpDevice device, VdpRGBAFormat surface_rgba_format, VdpBool *is_supported, uint32_t *max_width, uint32_t *max_height)
{
	if (!is_supported || !max_width || !max_height)
		return VDP_STATUS_INVALID_POINTER;

	device_ctx_t *dev = handle_get(device);
	if (!dev)
		return VDP_STATUS_INVALID_HANDLE;


	return VDP_STATUS_ERROR;
}
