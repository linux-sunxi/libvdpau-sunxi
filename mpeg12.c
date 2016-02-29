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
#include <cedrus/cedrus_regs.h>
#include "vdpau_private.h"

static const uint8_t zigzag_scan[64] =
{
	 0,  1,  5,  6, 14, 15, 27, 28,
	 2,  4,  7, 13, 16, 26, 29, 42,
	 3,  8, 12, 17, 25, 30, 41, 43,
	 9, 11, 18, 24, 31, 40, 44, 53,
	10, 19, 23, 32, 39, 45, 52, 54,
	20, 22, 33, 38, 46, 51, 55, 60,
	21, 34, 37, 47, 50, 56, 59, 61,
	35, 36, 48, 49, 57, 58, 62, 63
};

static int mpeg_find_startcode(const uint8_t *data, int len)
{
	int pos = 0;
	while (pos < len)
	{
		int zeros = 0;
		for ( ; pos < len; pos++)
		{
			if (data[pos] == 0x00)
				zeros++;
			else if (data[pos] == 0x01 && zeros >= 2)
			{
				pos++;
				break;
			}
			else
				zeros = 0;
		}

		uint8_t marker = data[pos++];

		if (marker >= 0x01 && marker <= 0xaf)
			return pos - 4;
	}
	return 0;
}

static VdpStatus mpeg12_decode(decoder_ctx_t *decoder,
                               VdpPictureInfo const *_info,
                               const int len,
                               video_surface_ctx_t *output)
{
	VdpPictureInfoMPEG1Or2 const *info = (VdpPictureInfoMPEG1Or2 const *)_info;
	int start_offset = mpeg_find_startcode(cedrus_mem_get_pointer(decoder->data), len);

	VdpStatus ret = yuv_prepare(output);
	if (ret != VDP_STATUS_OK)
		return ret;

	ret = rec_prepare(output);
	if (ret != VDP_STATUS_OK)
		return ret;

	int i;

	// activate MPEG engine
	void *ve_regs = cedrus_ve_get(decoder->device->cedrus, CEDRUS_ENGINE_MPEG, 0);

	// set quantisation tables
	for (i = 0; i < 64; i++)
		writel((uint32_t)(64 + zigzag_scan[i]) << 8 | info->intra_quantizer_matrix[i], ve_regs + VE_MPEG_IQ_MIN_INPUT);
	for (i = 0; i < 64; i++)
		writel((uint32_t)(zigzag_scan[i]) << 8 | info->non_intra_quantizer_matrix[i], ve_regs + VE_MPEG_IQ_MIN_INPUT);

	// set size
	uint16_t width = (decoder->width + 15) / 16;
	uint16_t height = (decoder->height + 15) / 16;
	writel((width << 8) | height, ve_regs + VE_MPEG_SIZE);
	writel(((width * 16) << 16) | (height * 16), ve_regs + VE_MPEG_FRAME_SIZE);

	// set picture header
	uint32_t pic_header = 0;
	pic_header |= ((info->picture_coding_type & 0xf) << 28);
	pic_header |= ((info->f_code[0][0] & 0xf) << 24);
	pic_header |= ((info->f_code[0][1] & 0xf) << 20);
	pic_header |= ((info->f_code[1][0] & 0xf) << 16);
	pic_header |= ((info->f_code[1][1] & 0xf) << 12);
	pic_header |= ((info->intra_dc_precision & 0x3) << 10);
	pic_header |= ((info->picture_structure & 0x3) << 8);
	pic_header |= ((info->top_field_first & 0x1) << 7);
	pic_header |= ((info->frame_pred_frame_dct & 0x1) << 6);
	pic_header |= ((info->concealment_motion_vectors & 0x1) << 5);
	pic_header |= ((info->q_scale_type & 0x1) << 4);
	pic_header |= ((info->intra_vlc_format & 0x1) << 3);
	pic_header |= ((info->alternate_scan & 0x1) << 2);
	pic_header |= ((info->full_pel_forward_vector & 0x1) << 1);
	pic_header |= ((info->full_pel_backward_vector & 0x1) << 0);
	if (decoder->profile == VDP_DECODER_PROFILE_MPEG1)
		pic_header |= 0x000003c0;
	writel(pic_header, ve_regs + VE_MPEG_PIC_HDR);

	// ??
	writel(0x80000138 | ((cedrus_get_ve_version(decoder->device->cedrus) < 0x1680) << 7), ve_regs + VE_MPEG_CTRL);
	if (cedrus_get_ve_version(decoder->device->cedrus) >= 0x1680)
		writel((0x2 << 30) | (0x1 << 28) | (output->chroma_size / 2), ve_regs + VE_EXTRA_OUT_FMT_OFFSET);

	// set forward/backward predicion buffers
	if (info->forward_reference != VDP_INVALID_HANDLE)
	{
		video_surface_ctx_t *forward = handle_get(info->forward_reference);
		writel(cedrus_mem_get_bus_addr(forward->rec), ve_regs + VE_MPEG_FWD_LUMA);
		writel(cedrus_mem_get_bus_addr(forward->rec) + forward->luma_size, ve_regs + VE_MPEG_FWD_CHROMA);
	}
	if (info->backward_reference != VDP_INVALID_HANDLE)
	{
		video_surface_ctx_t *backward = handle_get(info->backward_reference);
		writel(cedrus_mem_get_bus_addr(backward->rec), ve_regs + VE_MPEG_BACK_LUMA);
		writel(cedrus_mem_get_bus_addr(backward->rec) + backward->luma_size, ve_regs + VE_MPEG_BACK_CHROMA);
	}

	// set output buffers (Luma / Croma)
	writel(cedrus_mem_get_bus_addr(output->rec), ve_regs + VE_MPEG_REC_LUMA);
	writel(cedrus_mem_get_bus_addr(output->rec) + output->luma_size, ve_regs + VE_MPEG_REC_CHROMA);
	writel(cedrus_mem_get_bus_addr(output->yuv->data), ve_regs + VE_MPEG_ROT_LUMA);
	writel(cedrus_mem_get_bus_addr(output->yuv->data) + output->luma_size, ve_regs + VE_MPEG_ROT_CHROMA);

	// set input offset in bits
	writel(start_offset * 8, ve_regs + VE_MPEG_VLD_OFFSET);

	// set input length in bits
	writel((len - start_offset) * 8, ve_regs + VE_MPEG_VLD_LEN);

	// input end
	uint32_t input_addr = cedrus_mem_get_bus_addr(decoder->data);
	writel(input_addr + VBV_SIZE - 1, ve_regs + VE_MPEG_VLD_END);

	// set input buffer
	writel((input_addr & 0x0ffffff0) | (input_addr >> 28) | (0x7 << 28), ve_regs + VE_MPEG_VLD_ADDR);

	// trigger
	writel((((decoder->profile == VDP_DECODER_PROFILE_MPEG1) ? 1 : 2) << 24) | 0x8000000f, ve_regs + VE_MPEG_TRIGGER);

	// wait for interrupt
	cedrus_ve_wait(decoder->device->cedrus, 1);

	// clean interrupt flag
	writel(0x0000c00f, ve_regs + VE_MPEG_STATUS);

	// stop MPEG engine
	cedrus_ve_put(decoder->device->cedrus);

	return VDP_STATUS_OK;
}

VdpStatus new_decoder_mpeg12(decoder_ctx_t *decoder)
{
	decoder->decode = mpeg12_decode;
	return VDP_STATUS_OK;
}
