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
#include <cedrus/cedrus.h>
#include "vdpau_private.h"

VdpStatus vdp_decoder_create(VdpDevice device,
                             VdpDecoderProfile profile,
                             uint32_t width,
                             uint32_t height,
                             uint32_t max_references,
                             VdpDecoder *decoder)
{
	device_ctx_t *dev = handle_get(device);
	if (!dev)
		return VDP_STATUS_INVALID_HANDLE;

	if (max_references > 16)
		return VDP_STATUS_ERROR;

	decoder_ctx_t *dec = handle_create(sizeof(*dec), decoder);
	if (!dec)
		goto err_ctx;

	dec->device = dev;
	dec->profile = profile;
	dec->width = width;
	dec->height = height;

	dec->data = cedrus_mem_alloc(dec->device->cedrus, VBV_SIZE);
	if (!(dec->data))
		goto err_data;

	VdpStatus ret;
	switch (profile)
	{
	case VDP_DECODER_PROFILE_MPEG1:
	case VDP_DECODER_PROFILE_MPEG2_SIMPLE:
	case VDP_DECODER_PROFILE_MPEG2_MAIN:
		ret = new_decoder_mpeg12(dec);
		break;

	case VDP_DECODER_PROFILE_H264_BASELINE:
	case VDP_DECODER_PROFILE_H264_MAIN:
	case VDP_DECODER_PROFILE_H264_HIGH:
	case VDP_DECODER_PROFILE_H264_CONSTRAINED_BASELINE:
	case VDP_DECODER_PROFILE_H264_CONSTRAINED_HIGH:
		ret = new_decoder_h264(dec);
		break;

	case VDP_DECODER_PROFILE_MPEG4_PART2_SP:
	case VDP_DECODER_PROFILE_MPEG4_PART2_ASP:
		ret = new_decoder_mpeg4(dec);
		break;

	case VDP_DECODER_PROFILE_HEVC_MAIN:
		if (cedrus_get_ve_version(dec->device->cedrus) >= 0x1680)
			ret = new_decoder_h265(dec);
		else
			ret = VDP_STATUS_INVALID_DECODER_PROFILE;
		break;

	default:
		ret = VDP_STATUS_INVALID_DECODER_PROFILE;
		break;
	}

	if (ret != VDP_STATUS_OK)
		goto err_decoder;

	return VDP_STATUS_OK;

err_decoder:
	cedrus_mem_free(dec->data);
err_data:
	handle_destroy(*decoder);
err_ctx:
	return VDP_STATUS_RESOURCES;
}

VdpStatus vdp_decoder_destroy(VdpDecoder decoder)
{
	decoder_ctx_t *dec = handle_get(decoder);
	if (!dec)
		return VDP_STATUS_INVALID_HANDLE;

	if (dec->private_free)
		dec->private_free(dec);

	cedrus_mem_free(dec->data);

	handle_destroy(decoder);

	return VDP_STATUS_OK;
}

VdpStatus vdp_decoder_get_parameters(VdpDecoder decoder,
                                     VdpDecoderProfile *profile,
                                     uint32_t *width,
                                     uint32_t *height)
{
	decoder_ctx_t *dec = handle_get(decoder);
	if (!dec)
		return VDP_STATUS_INVALID_HANDLE;

	if (profile)
		*profile = dec->profile;

	if (width)
		*width = dec->width;

	if (height)
		*height = dec->height;

	return VDP_STATUS_OK;
}

VdpStatus vdp_decoder_render(VdpDecoder decoder,
                             VdpVideoSurface target,
                             VdpPictureInfo const *picture_info,
                             uint32_t bitstream_buffer_count,
                             VdpBitstreamBuffer const *bitstream_buffers)
{
	decoder_ctx_t *dec = handle_get(decoder);
	if (!dec)
		return VDP_STATUS_INVALID_HANDLE;

	video_surface_ctx_t *vid = handle_get(target);
	if (!vid)
		return VDP_STATUS_INVALID_HANDLE;

	vid->source_format = INTERNAL_YCBCR_FORMAT;
	unsigned int i, pos = 0;

	for (i = 0; i < bitstream_buffer_count; i++)
	{
		memcpy(cedrus_mem_get_pointer(dec->data) + pos, bitstream_buffers[i].bitstream, bitstream_buffers[i].bitstream_bytes);
		pos += bitstream_buffers[i].bitstream_bytes;
	}
	cedrus_mem_flush_cache(dec->data);

	return dec->decode(dec, picture_info, pos, vid);
}

VdpStatus vdp_decoder_query_capabilities(VdpDevice device,
                                         VdpDecoderProfile profile,
                                         VdpBool *is_supported,
                                         uint32_t *max_level,
                                         uint32_t *max_macroblocks,
                                         uint32_t *max_width,
                                         uint32_t *max_height)
{
	if (!is_supported || !max_level || !max_macroblocks || !max_width || !max_height)
		return VDP_STATUS_INVALID_POINTER;

	device_ctx_t *dev = handle_get(device);
	if (!dev)
		return VDP_STATUS_INVALID_HANDLE;

	*max_width = 3840;
	*max_height = 2160;
	*max_macroblocks = (*max_width * *max_height) / (16 * 16);

	switch (profile)
	{
	case VDP_DECODER_PROFILE_MPEG1:
		*max_level = VDP_DECODER_LEVEL_MPEG1_NA;
		*is_supported = VDP_TRUE;
		break;
	case VDP_DECODER_PROFILE_MPEG2_SIMPLE:
	case VDP_DECODER_PROFILE_MPEG2_MAIN:
		*max_level = VDP_DECODER_LEVEL_MPEG2_HL;
		*is_supported = VDP_TRUE;
		break;
	case VDP_DECODER_PROFILE_H264_BASELINE:
	case VDP_DECODER_PROFILE_H264_MAIN:
	case VDP_DECODER_PROFILE_H264_HIGH:
	case VDP_DECODER_PROFILE_H264_CONSTRAINED_BASELINE:
	case VDP_DECODER_PROFILE_H264_CONSTRAINED_HIGH:
		*max_level = VDP_DECODER_LEVEL_H264_5_1;
		*is_supported = VDP_TRUE;
		break;
	case VDP_DECODER_PROFILE_MPEG4_PART2_SP:
	case VDP_DECODER_PROFILE_MPEG4_PART2_ASP:
		*max_level = VDP_DECODER_LEVEL_MPEG4_PART2_ASP_L5;
		*is_supported = VDP_TRUE;
		break;
	case VDP_DECODER_PROFILE_HEVC_MAIN:
		*max_level = VDP_DECODER_LEVEL_HEVC_5;
		if (cedrus_get_ve_version(dev->cedrus) >= 0x1680)
			*is_supported = VDP_TRUE;
		else
			*is_supported = VDP_FALSE;
		break;

	default:
		*is_supported = VDP_FALSE;
		break;
	}

	return VDP_STATUS_OK;
}
