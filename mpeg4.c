/*
 * Copyright (c) 2014 Jens Kuske <jenskuske@gmail.com>
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

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <cedrus/cedrus.h>
#include <cedrus/cedrus_regs.h>
#include "vdpau_private.h"

typedef struct
{
	const uint8_t *data;
	unsigned int length;
	unsigned int bitpos;
} bitstream;

static int find_startcode(bitstream *bs)
{
	unsigned int pos, zeros = 0;
	for (pos = bs->bitpos / 8; pos < bs->length; pos++)
	{
		if (bs->data[pos] == 0x00)
			zeros++;
		else if (bs->data[pos] == 0x01 && zeros >= 2)
		{
			bs->bitpos = (pos + 1) * 8;
			return 1;
		}
		else
			zeros = 0;
	}

	return 0;
}

static uint32_t get_bits(bitstream *bs, int n)
{
	uint32_t bits = 0;
	int remaining_bits = n;

	while (remaining_bits > 0)
	{
		int bits_in_current_byte = 8 - (bs->bitpos & 7);

		int trash_bits = 0;
		if (remaining_bits < bits_in_current_byte)
			trash_bits = bits_in_current_byte - remaining_bits;

		int useful_bits = bits_in_current_byte - trash_bits;

		bits = (bits << useful_bits) | (bs->data[bs->bitpos / 8] >> trash_bits);

		remaining_bits -= useful_bits;
		bs->bitpos += useful_bits;
	}

	return bits & ((1 << n) - 1);
}

typedef struct
{
	cedrus_mem_t *mbh_buffer;
	cedrus_mem_t *dcac_buffer;
	cedrus_mem_t *ncf_buffer;
} mpeg4_private_t;

static void mpeg4_private_free(decoder_ctx_t *decoder)
{
	mpeg4_private_t *decoder_p = (mpeg4_private_t *)decoder->private;
	cedrus_mem_free(decoder_p->mbh_buffer);
	cedrus_mem_free(decoder_p->dcac_buffer);
	cedrus_mem_free(decoder_p->ncf_buffer);
	free(decoder_p);
}

#define VOP_I	0
#define VOP_P	1
#define VOP_B	2
#define VOP_S	3

typedef struct
{
	int vop_coding_type;
	int intra_dc_vlc_thr;
	int vop_quant;
} vop_header;

static int decode_vop_header(bitstream *bs, VdpPictureInfoMPEG4Part2 const *info, vop_header *h)
{
	h->vop_coding_type = get_bits(bs, 2);

	// modulo_time_base
	while (get_bits(bs, 1) != 0);

	if (get_bits(bs, 1) != 1)
		VDPAU_DBG("vop header marker error");

	// vop_time_increment
	get_bits(bs, 32 - __builtin_clz(info->vop_time_increment_resolution));

	if (get_bits(bs, 1) != 1)
		VDPAU_DBG("vop header marker error");

	// vop_coded
	if (!get_bits(bs, 1))
		return 0;

	// rounding_type
	if (h->vop_coding_type == VOP_P)
		get_bits(bs, 1);

	h->intra_dc_vlc_thr = get_bits(bs, 3);

	// assume default size of 5 bits
	h->vop_quant = get_bits(bs, 5);

	// vop_fcode_forward
	if (h->vop_coding_type != VOP_I)
		get_bits(bs, 3);

	// vop_fcode_backward
	if (h->vop_coding_type == VOP_B)
		get_bits(bs, 3);

	return 1;
}

static VdpStatus mpeg4_decode(decoder_ctx_t *decoder,
                              VdpPictureInfo const *_info,
                              const int len,
                              video_surface_ctx_t *output)
{
	VdpPictureInfoMPEG4Part2 const *info = (VdpPictureInfoMPEG4Part2 const *)_info;
	mpeg4_private_t *decoder_p = (mpeg4_private_t *)decoder->private;

	if (!info->resync_marker_disable)
	{
		VDPAU_DBG("We can't decode VOPs with resync markers yet! Sorry");
		return VDP_STATUS_ERROR;
	}

	VdpStatus ret = yuv_prepare(output);
	if (ret != VDP_STATUS_OK)
		return ret;

	ret = rec_prepare(output);
	if (ret != VDP_STATUS_OK)
		return ret;

	bitstream bs = { .data = cedrus_mem_get_pointer(decoder->data), .length = len, .bitpos = 0 };

	while (find_startcode(&bs))
	{
		if (get_bits(&bs, 8) != 0xb6)
			continue;

		vop_header hdr;
		if (!decode_vop_header(&bs, info, &hdr))
			continue;

		// activate MPEG engine
		void *ve_regs = cedrus_ve_get(decoder->device->cedrus, CEDRUS_ENGINE_MPEG, 0);

		// set buffers
		writel(cedrus_mem_get_bus_addr(decoder_p->mbh_buffer), ve_regs + VE_MPEG_MBH_ADDR);
		writel(cedrus_mem_get_bus_addr(decoder_p->dcac_buffer), ve_regs + VE_MPEG_DCAC_ADDR);
		writel(cedrus_mem_get_bus_addr(decoder_p->ncf_buffer), ve_regs + VE_MPEG_NCF_ADDR);

		// set output buffers
		writel(cedrus_mem_get_bus_addr(output->rec), ve_regs + VE_MPEG_REC_LUMA);
		writel(cedrus_mem_get_bus_addr(output->rec) + output->luma_size, ve_regs + VE_MPEG_REC_CHROMA);
		writel(cedrus_mem_get_bus_addr(output->yuv->data), ve_regs + VE_MPEG_ROT_LUMA);
		writel(cedrus_mem_get_bus_addr(output->yuv->data) + output->luma_size, ve_regs + VE_MPEG_ROT_CHROMA);

		// ??
		writel(0x40620000, ve_regs + VE_MPEG_SDROT_CTRL);
		if (cedrus_get_ve_version(decoder->device->cedrus) >= 0x1680)
			writel((0x2 << 30) | (0x1 << 28) | (output->chroma_size / 2), ve_regs + VE_EXTRA_OUT_FMT_OFFSET);

		// set vop header
		writel(((hdr.vop_coding_type == VOP_B ? 0x1 : 0x0) << 28)
			| (info->quant_type << 24)
			| (info->quarter_sample << 23)
			| (info->resync_marker_disable << 22)
			| (hdr.vop_coding_type << 18)
			| (info->rounding_control << 17)
			| (hdr.intra_dc_vlc_thr << 8)
			| (info->top_field_first << 7)
			| (info->alternate_vertical_scan_flag << 6)
			| ((hdr.vop_coding_type != VOP_I ? info->vop_fcode_forward : 0) << 3)
			| ((hdr.vop_coding_type == VOP_B ? info->vop_fcode_backward : 0) << 0)
			, ve_regs + VE_MPEG_VOP_HDR);

		// set size
		uint16_t width = (decoder->width + 15) / 16;
		uint16_t height = (decoder->height + 15) / 16;
		writel((((width + 1) & ~0x1) << 16) | (width << 8) | height, ve_regs + VE_MPEG_SIZE);
		writel(((width * 16) << 16) | (height * 16), ve_regs + VE_MPEG_FRAME_SIZE);

		// ??
		writel(0x0, ve_regs + VE_MPEG_MBA);

		// enable interrupt, unknown control flags
		writel(0x80084118 | ((cedrus_get_ve_version(decoder->device->cedrus) < 0x1680) << 7) | ((hdr.vop_coding_type == VOP_P ? 0x1 : 0x0) << 12), ve_regs + VE_MPEG_CTRL);

		// set quantization parameter
		writel(hdr.vop_quant, ve_regs + VE_MPEG_QP_INPUT);

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

		// set trb/trd
		if (hdr.vop_coding_type == VOP_B)
		{
			writel((info->trb[0] << 16) | (info->trd[0] << 0), ve_regs + VE_MPEG_TRBTRD_FRAME);
			// unverified:
			writel((info->trb[1] << 16) | (info->trd[1] << 0), ve_regs + VE_MPEG_TRBTRD_FIELD);
		}

		// clear status
		writel(0xffffffff, ve_regs + VE_MPEG_STATUS);

		// set input offset in bits
		writel(bs.bitpos, ve_regs + VE_MPEG_VLD_OFFSET);

		// set input length in bits
		writel(len * 8 - bs.bitpos, ve_regs + VE_MPEG_VLD_LEN);

		// input end
		uint32_t input_addr = cedrus_mem_get_bus_addr(decoder->data);
		writel(input_addr + VBV_SIZE - 1, ve_regs + VE_MPEG_VLD_END);

		// set input buffer
		writel((input_addr & 0x0ffffff0) | (input_addr >> 28) | (0x7 << 28), ve_regs + VE_MPEG_VLD_ADDR);

		// trigger
		writel(0x8400000d | ((width * height) << 8), ve_regs + VE_MPEG_TRIGGER);

		cedrus_ve_wait(decoder->device->cedrus, 1);

		// clear status
		writel(readl(ve_regs + VE_MPEG_STATUS) | 0xf, ve_regs + VE_MPEG_STATUS);

		// stop MPEG engine
		cedrus_ve_put(decoder->device->cedrus);
	}

	return VDP_STATUS_OK;
}

VdpStatus new_decoder_mpeg4(decoder_ctx_t *decoder)
{
	mpeg4_private_t *decoder_p = calloc(1, sizeof(mpeg4_private_t));
	if (!decoder_p)
		goto err_priv;

	int width = ((decoder->width + 15) / 16);
	int height = ((decoder->height + 15) / 16);

	decoder_p->mbh_buffer = cedrus_mem_alloc(decoder->device->cedrus, height * 2048);
	if (!decoder_p->mbh_buffer)
		goto err_mbh;

	decoder_p->dcac_buffer = cedrus_mem_alloc(decoder->device->cedrus, width * height * 2);
	if (!decoder_p->dcac_buffer)
		goto err_dcac;

	decoder_p->ncf_buffer = cedrus_mem_alloc(decoder->device->cedrus, 4 * 1024);
	if (!decoder_p->ncf_buffer)
		goto err_ncf;

	decoder->decode = mpeg4_decode;
	decoder->private = decoder_p;
	decoder->private_free = mpeg4_private_free;

	return VDP_STATUS_OK;

err_ncf:
	cedrus_mem_free(decoder_p->dcac_buffer);
err_dcac:
	cedrus_mem_free(decoder_p->mbh_buffer);
err_mbh:
	free(decoder_p);
err_priv:
	return VDP_STATUS_RESOURCES;
}
