/*
 * Copyright (c) 2013-2014 Jens Kuske <jenskuske@gmail.com>
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
#include "vdpau_private.h"
#include "tiled_yuv.h"

void yuv_unref(yuv_data_t *yuv)
{
	yuv->ref_count--;

	if (yuv->ref_count == 0)
	{
		cedrus_mem_free(yuv->data);
		free(yuv);
	}
}

yuv_data_t *yuv_ref(yuv_data_t *yuv)
{
	yuv->ref_count++;
	return yuv;
}

static VdpStatus yuv_new(video_surface_ctx_t *video_surface)
{
	video_surface->yuv = calloc(1, sizeof(yuv_data_t));
	if (!video_surface->yuv)
		return VDP_STATUS_RESOURCES;

	video_surface->yuv->ref_count = 1;
	video_surface->yuv->data = cedrus_mem_alloc(video_surface->device->cedrus, video_surface->luma_size + video_surface->chroma_size);

	if (!(video_surface->yuv->data))
	{
		free(video_surface->yuv);
		return VDP_STATUS_RESOURCES;
	}

	return VDP_STATUS_OK;
}

VdpStatus yuv_prepare(video_surface_ctx_t *video_surface)
{
	if (video_surface->yuv->ref_count > 1)
	{
		video_surface->yuv->ref_count--;
		return yuv_new(video_surface);
	}

	return VDP_STATUS_OK;
}

VdpStatus vdp_video_surface_create(VdpDevice device,
                                   VdpChromaType chroma_type,
                                   uint32_t width,
                                   uint32_t height,
                                   VdpVideoSurface *surface)
{
	if (!surface)
		return VDP_STATUS_INVALID_POINTER;

	if (width < 1 || width > 8192 || height < 1 || height > 8192)
		return VDP_STATUS_INVALID_SIZE;

	device_ctx_t *dev = handle_get(device);
	if (!dev)
		return VDP_STATUS_INVALID_HANDLE;

	video_surface_ctx_t *vs = handle_create(sizeof(*vs), surface);
	if (!vs)
		return VDP_STATUS_RESOURCES;

	vs->device = dev;
	vs->width = width;
	vs->height = height;
	vs->chroma_type = chroma_type;

	vs->luma_size = ALIGN(width, 32) * ALIGN(height, 32);
	switch (chroma_type)
	{
	case VDP_CHROMA_TYPE_444:
		vs->chroma_size = vs->luma_size * 2;
		break;
	case VDP_CHROMA_TYPE_422:
		vs->chroma_size = vs->luma_size;
		break;
	case VDP_CHROMA_TYPE_420:
		vs->chroma_size = ALIGN(vs->width, 32) * ALIGN(vs->height / 2, 32);
		break;
	default:
		handle_destroy(*surface);
		return VDP_STATUS_INVALID_CHROMA_TYPE;
	}

	VdpStatus ret = yuv_new(vs);
	if (ret != VDP_STATUS_OK)
	{
		handle_destroy(*surface);
		return ret;
	}

	return VDP_STATUS_OK;
}

VdpStatus vdp_video_surface_destroy(VdpVideoSurface surface)
{
	video_surface_ctx_t *vs = handle_get(surface);
	if (!vs)
		return VDP_STATUS_INVALID_HANDLE;

	if (vs->decoder_private_free)
		vs->decoder_private_free(vs);

	yuv_unref(vs->yuv);

	handle_destroy(surface);

	return VDP_STATUS_OK;
}

VdpStatus vdp_video_surface_get_parameters(VdpVideoSurface surface,
                                           VdpChromaType *chroma_type,
                                           uint32_t *width,
                                           uint32_t *height)
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

VdpStatus vdp_video_surface_get_bits_y_cb_cr(VdpVideoSurface surface,
                                             VdpYCbCrFormat destination_ycbcr_format,
                                             void *const *destination_data,
                                             uint32_t const *destination_pitches)
{
	video_surface_ctx_t *vs = handle_get(surface);
	if (!vs)
		return VDP_STATUS_INVALID_HANDLE;

	if (vs->chroma_type != VDP_CHROMA_TYPE_420)
		return VDP_STATUS_INVALID_CHROMA_TYPE;

	if (destination_pitches[0] < vs->width || destination_pitches[1] < vs->width / 2)
		return VDP_STATUS_ERROR;

	if (vs->source_format == VDP_YCBCR_FORMAT_YV12 && destination_ycbcr_format == VDP_YCBCR_FORMAT_YV12)
	{
		int i;
		const uint8_t *src;
		uint8_t *dst;

		src = cedrus_mem_get_pointer(vs->yuv->data);
		dst = destination_data[0];
		for (i = 0; i < vs->height; i++)
		{
			memcpy(dst, src, vs->width);
			src += ALIGN(vs->width, 32);
			dst += destination_pitches[0];
		}
		src = cedrus_mem_get_pointer(vs->yuv->data) + vs->luma_size;
		dst = destination_data[2];
		for (i = 0; i < vs->height / 2; i++)
		{
			memcpy(dst, src, vs->width / 2);
			src += ALIGN(vs->width / 2, 16);
			dst += destination_pitches[2];
		}
		src = cedrus_mem_get_pointer(vs->yuv->data) + vs->luma_size + vs->chroma_size / 2;
		dst = destination_data[1];
		for (i = 0; i < vs->height / 2; i++)
		{
			memcpy(dst, src, vs->width / 2);
			src += ALIGN(vs->width / 2, 16);
			dst += destination_pitches[1];
		}
		return VDP_STATUS_OK;
	}
#ifndef __aarch64__
	else if (vs->source_format == INTERNAL_YCBCR_FORMAT && destination_ycbcr_format == VDP_YCBCR_FORMAT_NV12)
	{
		tiled_to_planar(cedrus_mem_get_pointer(vs->yuv->data), destination_data[0], destination_pitches[0], vs->width, vs->height);
		tiled_to_planar(cedrus_mem_get_pointer(vs->yuv->data) + vs->luma_size, destination_data[1], destination_pitches[1], vs->width, vs->height / 2);
		return VDP_STATUS_OK;
	}
	else if (vs->source_format == INTERNAL_YCBCR_FORMAT && destination_ycbcr_format == VDP_YCBCR_FORMAT_YV12)
	{
		if (destination_pitches[2] != destination_pitches[1])
			return VDP_STATUS_ERROR;
		tiled_to_planar(cedrus_mem_get_pointer(vs->yuv->data), destination_data[0], destination_pitches[0], vs->width, vs->height);
		tiled_deinterleave_to_planar(cedrus_mem_get_pointer(vs->yuv->data) + vs->luma_size, destination_data[2], destination_data[1], destination_pitches[1], vs->width, vs->height / 2);
		return VDP_STATUS_OK;
	}
#endif

	return VDP_STATUS_INVALID_Y_CB_CR_FORMAT;
}

VdpStatus vdp_video_surface_put_bits_y_cb_cr(VdpVideoSurface surface,
                                             VdpYCbCrFormat source_ycbcr_format,
                                             void const *const *source_data,
                                             uint32_t const *source_pitches)
{
	int i;
	const uint8_t *src;
	uint8_t *dst;
	video_surface_ctx_t *vs = handle_get(surface);
	if (!vs)
		return VDP_STATUS_INVALID_HANDLE;

	VdpStatus ret = yuv_prepare(vs);
	if (ret != VDP_STATUS_OK)
		return ret;

	vs->source_format = source_ycbcr_format;

	switch (source_ycbcr_format)
	{
	case VDP_YCBCR_FORMAT_YUYV:
	case VDP_YCBCR_FORMAT_UYVY:
		if (vs->chroma_type != VDP_CHROMA_TYPE_422)
			return VDP_STATUS_INVALID_CHROMA_TYPE;
		src = source_data[0];
		dst = cedrus_mem_get_pointer(vs->yuv->data);
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
		dst = cedrus_mem_get_pointer(vs->yuv->data);
		for (i = 0; i < vs->height; i++) {
			memcpy(dst, src, vs->width);
			src += source_pitches[0];
			dst += vs->width;
		}
		src = source_data[1];
		dst = cedrus_mem_get_pointer(vs->yuv->data) + vs->luma_size;
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
		dst = cedrus_mem_get_pointer(vs->yuv->data);
		for (i = 0; i < vs->height; i++) {
			memcpy(dst, src, vs->width);
			src += source_pitches[0];
			dst += vs->width;
		}
		src = source_data[2];
		dst = cedrus_mem_get_pointer(vs->yuv->data) + vs->luma_size;
		for (i = 0; i < vs->height / 2; i++) {
			memcpy(dst, src, vs->width / 2);
			src += source_pitches[1];
			dst += vs->width / 2;
		}
		src = source_data[1];
		dst = cedrus_mem_get_pointer(vs->yuv->data) + vs->luma_size + vs->chroma_size / 2;
		for (i = 0; i < vs->height / 2; i++) {
			memcpy(dst, src, vs->width / 2);
			src += source_pitches[2];
			dst += vs->width / 2;
		}
		break;
	}

	cedrus_mem_flush_cache(vs->yuv->data);

	return VDP_STATUS_OK;
}

VdpStatus vdp_video_surface_query_capabilities(VdpDevice device,
                                               VdpChromaType surface_chroma_type,
                                               VdpBool *is_supported,
                                               uint32_t *max_width,
                                               uint32_t *max_height)
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

VdpStatus vdp_video_surface_query_get_put_bits_y_cb_cr_capabilities(VdpDevice device,
                                                                    VdpChromaType surface_chroma_type,
                                                                    VdpYCbCrFormat bits_ycbcr_format,
                                                                    VdpBool *is_supported)
{
	if (!is_supported)
		return VDP_STATUS_INVALID_POINTER;

	device_ctx_t *dev = handle_get(device);
	if (!dev)
		return VDP_STATUS_INVALID_HANDLE;

	if (surface_chroma_type == VDP_CHROMA_TYPE_420)
		*is_supported = (bits_ycbcr_format == VDP_YCBCR_FORMAT_NV12) ||
				(bits_ycbcr_format == VDP_YCBCR_FORMAT_YV12);
	else
		*is_supported = VDP_FALSE;

	return VDP_STATUS_OK;
}
