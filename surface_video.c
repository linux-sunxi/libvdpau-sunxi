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
#include "vdpau_private.h"
#include "ve.h"

VdpStatus vdp_video_surface_create(VdpDevice device, VdpChromaType chroma_type, uint32_t width, uint32_t height, VdpVideoSurface *surface)
{
	if (!surface)
		return VDP_STATUS_INVALID_POINTER;

	if (!width || !height)
		return VDP_STATUS_INVALID_SIZE;

	device_ctx_t *dev = handle_get(device);
	if (!dev)
		return VDP_STATUS_INVALID_HANDLE;

	video_surface_ctx_t *vs = calloc(1, sizeof(video_surface_ctx_t));
	if (!vs)
		return VDP_STATUS_RESOURCES;

	vs->device = dev;
	vs->width = width;
	vs->height = height;
	vs->chroma_type = chroma_type;

	vs->plane_size = ((width + 63) & ~63) * ((height + 63) & ~63);

	switch (chroma_type)
	{
	case VDP_CHROMA_TYPE_444:
		vs->data = ve_malloc(vs->plane_size * 3);
		break;
	case VDP_CHROMA_TYPE_422:
		vs->data = ve_malloc(vs->plane_size * 2);
		break;
	case VDP_CHROMA_TYPE_420:
		vs->data = ve_malloc(vs->plane_size + (vs->plane_size / 2));
		break;
	default:
		free(vs);
		return VDP_STATUS_INVALID_CHROMA_TYPE;
	}

	if (!(vs->data))
	{
		free(vs);
		return VDP_STATUS_RESOURCES;
	}

	int handle = handle_create(vs);
	if (handle == -1)
	{
		ve_free(vs->data);
		free(vs);
		return VDP_STATUS_RESOURCES;
	}

	*surface = handle;

	return VDP_STATUS_OK;
}

VdpStatus vdp_video_surface_destroy(VdpVideoSurface surface)
{
	video_surface_ctx_t *vs = handle_get(surface);
	if (!vs)
		return VDP_STATUS_INVALID_HANDLE;

	if (vs->extra_data)
		ve_free(vs->extra_data);
	ve_free(vs->data);

	handle_destroy(surface);
	free(vs);

	return VDP_STATUS_OK;
}

VdpStatus vdp_video_surface_get_parameters(VdpVideoSurface surface, VdpChromaType *chroma_type, uint32_t *width, uint32_t *height)
{
	video_surface_ctx_t *vid = handle_get(surface);
	if (!vid)
		return VDP_STATUS_INVALID_HANDLE;

	if (chroma_type)
		*chroma_type = vid->chroma_type;

	if (width)
		*width = vid->width;

	if (height)
		*height = vid->height;

	return VDP_STATUS_OK;
}

VdpStatus vdp_video_surface_get_bits_y_cb_cr(VdpVideoSurface surface, VdpYCbCrFormat destination_ycbcr_format, void *const *destination_data, uint32_t const *destination_pitches)
{
	video_surface_ctx_t *vs = handle_get(surface);
	if (!vs)
		return VDP_STATUS_INVALID_HANDLE;


	return VDP_STATUS_ERROR;
}

VdpStatus vdp_video_surface_put_bits_y_cb_cr(VdpVideoSurface surface, VdpYCbCrFormat source_ycbcr_format, void const *const *source_data, uint32_t const *source_pitches)
{
	int i;
	const uint8_t *src;
	uint8_t *dst;
	video_surface_ctx_t *vs = handle_get(surface);
	if (!vs)
		return VDP_STATUS_INVALID_HANDLE;

	vs->source_format = source_ycbcr_format;

	switch (source_ycbcr_format)
	{
	case VDP_YCBCR_FORMAT_YUYV:
	case VDP_YCBCR_FORMAT_UYVY:
		if (vs->chroma_type != VDP_CHROMA_TYPE_422)
			return VDP_STATUS_INVALID_CHROMA_TYPE;
		src = source_data[0];
		dst = vs->data;
		for (i = 0; i < vs->height; i++) {
			memcpy(dst, src, 2*vs->width);
			src += source_pitches[0];
			dst += 2*vs->width;
		}
		break;
	case VDP_YCBCR_FORMAT_Y8U8V8A8:
	case VDP_YCBCR_FORMAT_V8U8Y8A8:

		break;

	case VDP_YCBCR_FORMAT_NV12:
		if (vs->chroma_type != VDP_CHROMA_TYPE_420)
			return VDP_STATUS_INVALID_CHROMA_TYPE;
		src = source_data[0];
		dst = vs->data;
		for (i = 0; i < vs->height; i++) {
			memcpy(dst, src, vs->width);
			src += source_pitches[0];
			dst += vs->width;
		}
		src = source_data[1];
		dst = vs->data + vs->plane_size;
		for (i = 0; i < vs->height / 2; i++) {
			memcpy(dst, src, vs->width);
			src += source_pitches[1];
			dst += vs->width;
		}
		break;

	case VDP_YCBCR_FORMAT_YV12:
		if (vs->chroma_type != VDP_CHROMA_TYPE_420)
			return VDP_STATUS_INVALID_CHROMA_TYPE;
		src = source_data[0];
		dst = vs->data;
		for (i = 0; i < vs->height; i++) {
			memcpy(dst, src, vs->width);
			src += source_pitches[0];
			dst += vs->width;
		}
		src = source_data[2];
		dst = vs->data + vs->plane_size;
		for (i = 0; i < vs->height / 2; i++) {
			memcpy(dst, src, vs->width / 2);
			src += source_pitches[1];
			dst += vs->width / 2;
		}
		src = source_data[1];
		dst = vs->data + vs->plane_size + vs->plane_size / 4;
		for (i = 0; i < vs->height / 2; i++) {
			memcpy(dst, src, vs->width / 2);
			src += source_pitches[2];
			dst += vs->width / 2;
		}
		break;
	}

	return VDP_STATUS_OK;
}

VdpStatus vdp_video_surface_query_capabilities(VdpDevice device, VdpChromaType surface_chroma_type, VdpBool *is_supported, uint32_t *max_width, uint32_t *max_height)
{
	if (!is_supported || !max_width || !max_height)
		return VDP_STATUS_INVALID_POINTER;

	device_ctx_t *dev = handle_get(device);
	if (!dev)
		return VDP_STATUS_INVALID_HANDLE;

	*is_supported = surface_chroma_type == VDP_CHROMA_TYPE_420;
	*max_width = 8192;
	*max_height = 8192;

	return VDP_STATUS_OK;
}

VdpStatus vdp_video_surface_query_get_put_bits_y_cb_cr_capabilities(VdpDevice device, VdpChromaType surface_chroma_type, VdpYCbCrFormat bits_ycbcr_format, VdpBool *is_supported)
{
	if (!is_supported)
		return VDP_STATUS_INVALID_POINTER;

	device_ctx_t *dev = handle_get(device);
	if (!dev)
		return VDP_STATUS_INVALID_HANDLE;

	*is_supported = VDP_FALSE;

	return VDP_STATUS_OK;
}
