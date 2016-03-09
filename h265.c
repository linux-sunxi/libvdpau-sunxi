/*
 * Copyright (c) 2015 Jens Kuske <jenskuske@gmail.com>
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

static int find_startcode(const uint8_t *data, int len, int start)
{
	int pos, zeros = 0;
	for (pos = start; pos < len; pos++)
	{
		if (data[pos] == 0x00)
			zeros++;
		else if (data[pos] == 0x01 && zeros >= 2)
			return pos + 1;
		else
			zeros = 0;
	}

	return -1;
}

static void skip_bits(void *regs, int num)
{
	for (; num > 32; num -= 32)
	{
		writel(0x3 | (32 << 8), regs + VE_HEVC_TRIG);
		while (readl(regs + VE_HEVC_STATUS) & (1 << 8));
	}
	writel(0x3 | (num << 8), regs + VE_HEVC_TRIG);
	while (readl(regs + VE_HEVC_STATUS) & (1 << 8));
}

static uint32_t get_u(void *regs, int num)
{
	writel(0x2 | (num << 8), regs + VE_HEVC_TRIG);

	while (readl(regs + VE_HEVC_STATUS) & (1 << 8));

	return readl(regs + VE_HEVC_BITS_DATA);
}

static uint32_t get_ue(void *regs)
{
	writel(0x5, regs + VE_HEVC_TRIG);

	while (readl(regs + VE_HEVC_STATUS) & (1 << 8));

	return readl(regs + VE_HEVC_BITS_DATA);
}

static int32_t get_se(void *regs)
{
	writel(0x4, regs + VE_HEVC_TRIG);

	while (readl(regs + VE_HEVC_STATUS) & (1 << 8));

	return readl(regs + VE_HEVC_BITS_DATA);
}

#define SLICE_B	0
#define SLICE_P	1
#define SLICE_I	2

#define MinCbLog2SizeY (p->info->log2_min_luma_coding_block_size_minus3 + 3)
#define CtbLog2SizeY (MinCbLog2SizeY + p->info->log2_diff_max_min_luma_coding_block_size)
#define CtbSizeY (1 << CtbLog2SizeY)
#define PicWidthInCtbsY DIV_ROUND_UP(p->info->pic_width_in_luma_samples, CtbSizeY)
#define PicHeightInCtbsY DIV_ROUND_UP(p->info->pic_height_in_luma_samples, CtbSizeY)
#define PicSizeInCtbsY (PicWidthInCtbsY * PicHeightInCtbsY)

#define ChromaLog2WeightDenom (p->slice.luma_log2_weight_denom + p->slice.delta_chroma_log2_weight_denom)
#define ChromaWeightL0(i, j) ((1 << ChromaLog2WeightDenom) + p->slice.delta_chroma_weight_l0[i][j])
#define ChromaWeightL1(i, j) ((1 << ChromaLog2WeightDenom) + p->slice.delta_chroma_weight_l1[i][j])
#define ChromaOffsetL0(i, j) (clamp(-128, 127, p->slice.delta_chroma_offset_l0[i][j] - ((128 * ChromaWeightL0(i, j)) >> ChromaLog2WeightDenom) + 128))
#define ChromaOffsetL1(i, j) (clamp(-128, 127, p->slice.delta_chroma_offset_l1[i][j] - ((128 * ChromaWeightL1(i, j)) >> ChromaLog2WeightDenom) + 128))

struct h265_slice_header {
	uint8_t first_slice_segment_in_pic_flag;
	uint8_t no_output_of_prior_pics_flag;
	uint8_t slice_pic_parameter_set_id;
	uint8_t dependent_slice_segment_flag;
	uint16_t slice_segment_address;
	uint8_t slice_type;
	uint8_t pic_output_flag;
	uint8_t colour_plane_id;
	uint16_t slice_pic_order_cnt_lsb;
	uint8_t short_term_ref_pic_set_sps_flag;

	uint8_t slice_temporal_mvp_enabled_flag;
	uint8_t	slice_sao_luma_flag;
	uint8_t slice_sao_chroma_flag;
	uint8_t num_ref_idx_active_override_flag;
	uint8_t num_ref_idx_l0_active_minus1;
	uint8_t num_ref_idx_l1_active_minus1;
	uint8_t mvd_l1_zero_flag;
	uint8_t cabac_init_flag;
	uint8_t collocated_from_l0_flag;
	uint8_t collocated_ref_idx;
	uint8_t five_minus_max_num_merge_cand;
	int8_t slice_qp_delta;
	int8_t slice_cb_qp_offset;
	int8_t slice_cr_qp_offset;
	uint8_t deblocking_filter_override_flag;
	uint8_t slice_deblocking_filter_disabled_flag;
	int8_t slice_beta_offset_div2;
	int8_t slice_tc_offset_div2;
	uint8_t slice_loop_filter_across_slices_enabled_flag;
	uint16_t num_entry_point_offsets;
	uint8_t offset_len_minus1;
	uint32_t entry_point_offset_minus1[256];

	uint8_t ref_pic_list_modification_flag_l0;
	uint8_t ref_pic_list_modification_flag_l1;
	uint8_t list_entry_l0[16];
	uint8_t list_entry_l1[16];

	uint8_t luma_log2_weight_denom;
	int8_t delta_chroma_log2_weight_denom;
	uint8_t luma_weight_l0_flag[16];
	uint8_t chroma_weight_l0_flag[16];
	int8_t delta_luma_weight_l0[16];
	int8_t luma_offset_l0[16];
	int8_t delta_chroma_weight_l0[16][2];
	int16_t delta_chroma_offset_l0[16][2];
	uint8_t luma_weight_l1_flag[16];
	uint8_t chroma_weight_l1_flag[16];
	int8_t delta_luma_weight_l1[16];
	int8_t luma_offset_l1[16];
	int8_t delta_chroma_weight_l1[16][2];
	int16_t delta_chroma_offset_l1[16][2];
};

struct h265_private
{
	void *regs;
	VdpPictureInfoHEVC const *info;
	decoder_ctx_t *decoder;
	video_surface_ctx_t *output;
	uint8_t nal_unit_type;

	cedrus_mem_t *neighbor_info;
	cedrus_mem_t *entry_points;

	struct h265_slice_header slice;
};

struct h265_video_private
{
	cedrus_mem_t *extra_data;
};

static void h265_video_private_free(video_surface_ctx_t *surface)
{
	struct h265_video_private *vp = surface->decoder_private;
	cedrus_mem_free(vp->extra_data);
	free(vp);
}

static struct h265_video_private *get_surface_priv(struct h265_private *p, video_surface_ctx_t *surface)
{
	struct h265_video_private *vp = surface->decoder_private;

	if (!vp)
	{
		vp = calloc(1, sizeof(*vp));
		if (!vp)
			return NULL;

		vp->extra_data = cedrus_mem_alloc(surface->device->cedrus, PicSizeInCtbsY * 160);
		if (!vp->extra_data)
		{
			free(vp);
			return NULL;
		}

		surface->decoder_private = vp;
		surface->decoder_private_free = h265_video_private_free;
	}

	return vp;
}

static void pred_weight_table(struct h265_private *p)
{
	int i, j;

	p->slice.luma_log2_weight_denom = get_ue(p->regs);
	if (p->info->chroma_format_idc != 0)
		p->slice.delta_chroma_log2_weight_denom = get_se(p->regs);

	for (i = 0; i <= p->slice.num_ref_idx_l0_active_minus1; i++)
		p->slice.luma_weight_l0_flag[i] = get_u(p->regs, 1);

	if (p->info->chroma_format_idc != 0)
		for (i = 0; i <= p->slice.num_ref_idx_l0_active_minus1; i++)
			p->slice.chroma_weight_l0_flag[i] = get_u(p->regs, 1);

	for (i = 0; i <= p->slice.num_ref_idx_l0_active_minus1; i++)
	{
		if (p->slice.luma_weight_l0_flag[i])
		{
			p->slice.delta_luma_weight_l0[i] = get_se(p->regs);
			p->slice.luma_offset_l0[i] = get_se(p->regs);
		}

		if (p->slice.chroma_weight_l0_flag[i])
		{
			for (j = 0; j < 2; j++)
			{
				p->slice.delta_chroma_weight_l0[i][j] = get_se(p->regs);
				p->slice.delta_chroma_offset_l0[i][j] = get_se(p->regs);
			}
		}
	}

	if (p->slice.slice_type == SLICE_B)
	{
		for (i = 0; i <= p->slice.num_ref_idx_l1_active_minus1; i++)
			p->slice.luma_weight_l1_flag[i] = get_u(p->regs, 1);

		if (p->info->chroma_format_idc != 0)
			for (i = 0; i <= p->slice.num_ref_idx_l1_active_minus1; i++)
				p->slice.chroma_weight_l1_flag[i] = get_u(p->regs, 1);

		for (i = 0; i <= p->slice.num_ref_idx_l1_active_minus1; i++)
		{
			if (p->slice.luma_weight_l1_flag[i])
			{
				p->slice.delta_luma_weight_l1[i] = get_se(p->regs);
				p->slice.luma_offset_l1[i] = get_se(p->regs);
			}

			if (p->slice.chroma_weight_l1_flag[i])
			{
				for (j = 0; j < 2; j++)
				{
					p->slice.delta_chroma_weight_l1[i][j] = get_se(p->regs);
					p->slice.delta_chroma_offset_l1[i][j] = get_se(p->regs);
				}
			}
		}
	}
}

static void ref_pic_lists_modification(struct h265_private *p)
{
	int i;

	p->slice.ref_pic_list_modification_flag_l0 = get_u(p->regs, 1);

	if (p->slice.ref_pic_list_modification_flag_l0)
		for (i = 0; i <= p->slice.num_ref_idx_l0_active_minus1; i++)
			p->slice.list_entry_l0[i] = get_u(p->regs, ceil_log2(p->info->NumPocTotalCurr));

	if (p->slice.slice_type == SLICE_B)
	{
		p->slice.ref_pic_list_modification_flag_l1 = get_u(p->regs, 1);

		if (p->slice.ref_pic_list_modification_flag_l1)
			for (i = 0; i <= p->slice.num_ref_idx_l1_active_minus1; i++)
				p->slice.list_entry_l1[i] = get_u(p->regs, ceil_log2(p->info->NumPocTotalCurr));
	}
}

static void slice_header(struct h265_private *p)
{
	int i;

	p->slice.first_slice_segment_in_pic_flag = get_u(p->regs, 1);

	if (p->nal_unit_type >= 16 && p->nal_unit_type <= 23)
		p->slice.no_output_of_prior_pics_flag = get_u(p->regs, 1);

	p->slice.slice_pic_parameter_set_id = get_ue(p->regs);

	if (!p->slice.first_slice_segment_in_pic_flag)
	{
		if (p->info->dependent_slice_segments_enabled_flag)
			p->slice.dependent_slice_segment_flag = get_u(p->regs, 1);

		p->slice.slice_segment_address = get_u(p->regs, ceil_log2(PicSizeInCtbsY));
	}

	if (!p->slice.dependent_slice_segment_flag)
	{
		p->slice.pic_output_flag = 1;
		p->slice.num_ref_idx_l0_active_minus1 = p->info->num_ref_idx_l0_default_active_minus1;
		p->slice.num_ref_idx_l1_active_minus1 = p->info->num_ref_idx_l1_default_active_minus1;
		p->slice.collocated_from_l0_flag = 1;
		p->slice.collocated_ref_idx = 0;
		p->slice.slice_deblocking_filter_disabled_flag = p->info->pps_deblocking_filter_disabled_flag;
		p->slice.slice_beta_offset_div2 = p->info->pps_beta_offset_div2;
		p->slice.slice_tc_offset_div2 = p->info->pps_tc_offset_div2;
		p->slice.slice_loop_filter_across_slices_enabled_flag = p->info->pps_loop_filter_across_slices_enabled_flag;

		skip_bits(p->regs, p->info->num_extra_slice_header_bits);

		p->slice.slice_type = get_ue(p->regs);

		if (p->info->output_flag_present_flag)
			p->slice.pic_output_flag = get_u(p->regs, 1);

		if (p->info->separate_colour_plane_flag == 1)
			p->slice.colour_plane_id = get_u(p->regs, 2);

		if (p->nal_unit_type != 19 && p->nal_unit_type != 20)
		{
			p->slice.slice_pic_order_cnt_lsb = get_u(p->regs, p->info->log2_max_pic_order_cnt_lsb_minus4 + 4);

			p->slice.short_term_ref_pic_set_sps_flag = get_u(p->regs, 1);

			skip_bits(p->regs, p->info->NumShortTermPictureSliceHeaderBits);

			if (p->info->long_term_ref_pics_present_flag)
				skip_bits(p->regs, p->info->NumLongTermPictureSliceHeaderBits);

			if (p->info->sps_temporal_mvp_enabled_flag)
				p->slice.slice_temporal_mvp_enabled_flag = get_u(p->regs, 1);
		}

		if (p->info->sample_adaptive_offset_enabled_flag)
		{
			p->slice.slice_sao_luma_flag = get_u(p->regs, 1);
			p->slice.slice_sao_chroma_flag = get_u(p->regs, 1);
		}

		if (p->slice.slice_type == SLICE_P || p->slice.slice_type == SLICE_B)
		{
			p->slice.num_ref_idx_active_override_flag = get_u(p->regs, 1);

			if (p->slice.num_ref_idx_active_override_flag)
			{
				p->slice.num_ref_idx_l0_active_minus1 = get_ue(p->regs);
				if (p->slice.slice_type == SLICE_B)
					p->slice.num_ref_idx_l1_active_minus1 = get_ue(p->regs);
			}

			if (p->info->lists_modification_present_flag && p->info->NumPocTotalCurr > 1)
				ref_pic_lists_modification(p);

			if (p->slice.slice_type == SLICE_B)
				p->slice.mvd_l1_zero_flag = get_u(p->regs, 1);

			if (p->info->cabac_init_present_flag)
				p->slice.cabac_init_flag = get_u(p->regs, 1);

			if (p->slice.slice_temporal_mvp_enabled_flag)
			{
				if (p->slice.slice_type == SLICE_B)
					p->slice.collocated_from_l0_flag = get_u(p->regs, 1);

				if ((p->slice.collocated_from_l0_flag && p->slice.num_ref_idx_l0_active_minus1 > 0) || (!p->slice.collocated_from_l0_flag && p->slice.num_ref_idx_l1_active_minus1 > 0))
					p->slice.collocated_ref_idx = get_ue(p->regs);
			}

			if ((p->info->weighted_pred_flag && p->slice.slice_type == SLICE_P) || (p->info->weighted_bipred_flag && p->slice.slice_type == SLICE_B))
				pred_weight_table(p);

			p->slice.five_minus_max_num_merge_cand = get_ue(p->regs);
		}

		p->slice.slice_qp_delta = get_se(p->regs);

		if (p->info->pps_slice_chroma_qp_offsets_present_flag)
		{
			p->slice.slice_cb_qp_offset = get_se(p->regs);
			p->slice.slice_cr_qp_offset = get_se(p->regs);
		}

		if (p->info->deblocking_filter_override_enabled_flag)
			p->slice.deblocking_filter_override_flag = get_u(p->regs, 1);

		if (p->slice.deblocking_filter_override_flag)
		{
			p->slice.slice_deblocking_filter_disabled_flag = get_u(p->regs, 1);

			if (!p->slice.slice_deblocking_filter_disabled_flag)
			{
				p->slice.slice_beta_offset_div2 = get_se(p->regs);
				p->slice.slice_tc_offset_div2 = get_se(p->regs);
			}
		}

		if (p->info->pps_loop_filter_across_slices_enabled_flag && (p->slice.slice_sao_luma_flag || p->slice.slice_sao_chroma_flag || !p->slice.slice_deblocking_filter_disabled_flag))
			p->slice.slice_loop_filter_across_slices_enabled_flag = get_u(p->regs, 1);
	}

	if (p->info->tiles_enabled_flag || p->info->entropy_coding_sync_enabled_flag)
	{
		p->slice.num_entry_point_offsets = get_ue(p->regs);

		if (p->slice.num_entry_point_offsets > 0)
		{
			p->slice.offset_len_minus1 = get_ue(p->regs);

			for (i = 0; i < p->slice.num_entry_point_offsets; i++)
				p->slice.entry_point_offset_minus1[i] = get_u(p->regs, p->slice.offset_len_minus1 + 1);
		}
	}

	if (p->info->slice_segment_header_extension_present_flag)
		skip_bits(p->regs, get_ue(p->regs) * 8);
}

static void write_pic_list(struct h265_private *p)
{
	int i;

	for (i = 0; i < 16; i++)
	{
		if (p->info->RefPics[i] != VDP_INVALID_HANDLE)
		{
			video_surface_ctx_t *v = handle_get(p->info->RefPics[i]);
			struct h265_video_private *vp = get_surface_priv(p, v);

			writel(VE_SRAM_HEVC_PIC_LIST + i * 0x20, p->regs + VE_HEVC_SRAM_ADDR);
			writel(p->info->PicOrderCntVal[i], p->regs + VE_HEVC_SRAM_DATA);
			writel(p->info->PicOrderCntVal[i], p->regs + VE_HEVC_SRAM_DATA);
			writel(cedrus_mem_get_bus_addr(vp->extra_data) >> 8, p->regs + VE_HEVC_SRAM_DATA);
			writel(cedrus_mem_get_bus_addr(vp->extra_data) >> 8, p->regs + VE_HEVC_SRAM_DATA);
			writel(cedrus_mem_get_bus_addr(v->yuv->data) >> 8, p->regs + VE_HEVC_SRAM_DATA);
			writel((cedrus_mem_get_bus_addr(v->yuv->data) + v->luma_size) >> 8, p->regs + VE_HEVC_SRAM_DATA);
		}
	}

	struct h265_video_private *vp = get_surface_priv(p, p->output);

	writel(VE_SRAM_HEVC_PIC_LIST + i * 0x20, p->regs + VE_HEVC_SRAM_ADDR);
	writel(p->info->CurrPicOrderCntVal, p->regs + VE_HEVC_SRAM_DATA);
	writel(p->info->CurrPicOrderCntVal, p->regs + VE_HEVC_SRAM_DATA);
	writel(cedrus_mem_get_bus_addr(vp->extra_data) >> 8, p->regs + VE_HEVC_SRAM_DATA);
	writel(cedrus_mem_get_bus_addr(vp->extra_data) >> 8, p->regs + VE_HEVC_SRAM_DATA);
	writel(cedrus_mem_get_bus_addr(p->output->yuv->data) >> 8, p->regs + VE_HEVC_SRAM_DATA);
	writel((cedrus_mem_get_bus_addr(p->output->yuv->data) + p->output->luma_size) >> 8, p->regs + VE_HEVC_SRAM_DATA);

	writel(i, p->regs + VE_HEVC_REC_BUF_IDX);
}

static void write_ref_pic_lists(struct h265_private *p)
{
	int i, j, rIdx;

	if (p->slice.slice_type != SLICE_I)
	{
		int NumRpsCurrTempList0 = max(p->slice.num_ref_idx_l0_active_minus1 + 1, p->info->NumPocTotalCurr);
		uint8_t RefPicListTemp0[NumRpsCurrTempList0];
		for (rIdx = 0; rIdx < NumRpsCurrTempList0; )
		{
			for (i = 0; i < p->info->NumPocStCurrBefore && rIdx < NumRpsCurrTempList0; rIdx++, i++)
				RefPicListTemp0[rIdx] = p->info->RefPicSetStCurrBefore[i];
			for (i = 0; i < p->info->NumPocStCurrAfter && rIdx < NumRpsCurrTempList0; rIdx++, i++)
				RefPicListTemp0[rIdx] = p->info->RefPicSetStCurrAfter[i];
			for (i = 0; i < p->info->NumPocLtCurr && rIdx < NumRpsCurrTempList0; rIdx++, i++)
				RefPicListTemp0[rIdx] = p->info->RefPicSetLtCurr[i] | (1 << 7);
		}

		writel(VE_SRAM_HEVC_REF_PIC_LIST0, p->regs + VE_HEVC_SRAM_ADDR);
		for (i = 0; i < p->slice.num_ref_idx_l0_active_minus1 + 1; i += 4)
		{
			uint32_t list = 0;
			for (j = 0; j < 4; j++)
			{
				int entry = i + j;
				if (p->slice.ref_pic_list_modification_flag_l0)
					entry = p->slice.list_entry_l0[entry];

				list |= RefPicListTemp0[entry] << (j * 8);
			}

			writel(list, p->regs + VE_HEVC_SRAM_DATA);
		}
	}

	if (p->slice.slice_type == SLICE_B)
	{
		int NumRpsCurrTempList1 = max(p->slice.num_ref_idx_l1_active_minus1 + 1, p->info->NumPocTotalCurr);
		uint8_t RefPicListTemp1[NumRpsCurrTempList1];

		for (rIdx = 0; rIdx < NumRpsCurrTempList1; )
		{
			for (i = 0; i < p->info->NumPocStCurrAfter && rIdx < NumRpsCurrTempList1; rIdx++, i++)
				RefPicListTemp1[rIdx] = p->info->RefPicSetStCurrAfter[i];
			for (i = 0; i < p->info->NumPocStCurrBefore && rIdx < NumRpsCurrTempList1; rIdx++, i++)
				RefPicListTemp1[rIdx] = p->info->RefPicSetStCurrBefore[i];
			for (i = 0; i < p->info->NumPocLtCurr && rIdx < NumRpsCurrTempList1; rIdx++, i++)
				RefPicListTemp1[rIdx] = p->info->RefPicSetLtCurr[i] | (1 << 7);
		}

		writel(VE_SRAM_HEVC_REF_PIC_LIST1, p->regs + VE_HEVC_SRAM_ADDR);
		for (i = 0; i < p->slice.num_ref_idx_l1_active_minus1 + 1; i += 4)
		{
			uint32_t list = 0;
			for (j = 0; j < 4; j++)
			{
				int entry = i + j;
				if (p->slice.ref_pic_list_modification_flag_l1)
					entry = p->slice.list_entry_l1[entry];

				list |= RefPicListTemp1[entry] << (j * 8);
			}

			writel(list, p->regs + VE_HEVC_SRAM_DATA);
		}
	}
}

static void write_entry_point_list(struct h265_private *p)
{
	int i, x, tx, y, ty;

	if (!p->info->tiles_enabled_flag)
		return;

	for (x = 0, tx = 0; tx < p->info->num_tile_columns_minus1 + 1; tx++)
	{
		if (x + p->info->column_width_minus1[tx] + 1 > (p->slice.slice_segment_address % PicWidthInCtbsY))
			break;

		x += p->info->column_width_minus1[tx] + 1;
	}

	for (y = 0, ty = 0; ty < p->info->num_tile_rows_minus1 + 1; ty++)
	{
		if (y + p->info->row_height_minus1[ty] + 1 > (p->slice.slice_segment_address / PicWidthInCtbsY))
			break;

		y += p->info->row_height_minus1[ty] + 1;
	}

	writel((y << 16) | (x << 0), p->regs + VE_HEVC_TILE_START_CTB);
	writel(((y + p->info->row_height_minus1[ty]) << 16) | ((x + p->info->column_width_minus1[tx]) << 0), p->regs + VE_HEVC_TILE_END_CTB);

	uint32_t *entry_points = cedrus_mem_get_pointer(p->entry_points);
	for (i = 0; i < p->slice.num_entry_point_offsets; i++)
	{
		if (tx + 1 >= p->info->num_tile_columns_minus1 + 1)
		{
			x = tx = 0;
			y += p->info->row_height_minus1[ty++] + 1;
		}
		else
		{
			x += p->info->column_width_minus1[tx++] + 1;
		}

		entry_points[i * 4 + 0] = p->slice.entry_point_offset_minus1[i] + 1;
		entry_points[i * 4 + 1] = 0x0;
		entry_points[i * 4 + 2] = (y << 16) | (x << 0);
		entry_points[i * 4 + 3] = ((y + p->info->row_height_minus1[ty]) << 16) | ((x + p->info->column_width_minus1[tx]) << 0);
	}

	cedrus_mem_flush_cache(p->entry_points);
	writel(cedrus_mem_get_bus_addr(p->entry_points) >> 8, p->regs + VE_HEVC_TILE_LIST_ADDR);
}

static void write_weighted_pred(struct h265_private *p)
{
	int i;

	if (p->slice.slice_type != SLICE_I && p->info->weighted_pred_flag)
	{
		writel(VE_SRAM_HEVC_PRED_WEIGHT_LUMA_L0, p->regs + VE_HEVC_SRAM_ADDR);

		for (i = 0; i < p->slice.num_ref_idx_l0_active_minus1 + 1; i += 2)
			writel(((p->slice.delta_luma_weight_l0[i] & 0xff) << 0) |
				((p->slice.luma_offset_l0[i] & 0xff) << 8) |
				((p->slice.delta_luma_weight_l0[i + 1] & 0xff) << 16) |
				((p->slice.luma_offset_l0[i + 1] & 0xff) << 24), p->regs + VE_HEVC_SRAM_DATA);

		writel(VE_SRAM_HEVC_PRED_WEIGHT_CHROMA_L0, p->regs + VE_HEVC_SRAM_ADDR);

		for (i = 0; i < p->slice.num_ref_idx_l0_active_minus1 + 1; i++)
			writel(((p->slice.delta_chroma_weight_l0[i][0] & 0xff) << 0) |
				((ChromaOffsetL0(i, 0) & 0xff) << 8) |
				((p->slice.delta_chroma_weight_l0[i][1] & 0xff) << 16) |
				((ChromaOffsetL0(i, 1) & 0xff) << 24), p->regs + VE_HEVC_SRAM_DATA);
	}

	if (p->slice.slice_type == SLICE_B &&p->info->weighted_bipred_flag)
	{
		writel(VE_SRAM_HEVC_PRED_WEIGHT_LUMA_L1, p->regs + VE_HEVC_SRAM_ADDR);

		for (i = 0; i < p->slice.num_ref_idx_l1_active_minus1 + 1; i += 2)
			writel(((p->slice.delta_luma_weight_l1[i] & 0xff) << 0) |
				((p->slice.luma_offset_l1[i] & 0xff) << 8) |
				((p->slice.delta_luma_weight_l1[i + 1] & 0xff) << 16) |
				((p->slice.luma_offset_l1[i + 1] & 0xff) << 24), p->regs + VE_HEVC_SRAM_DATA);

		writel(VE_SRAM_HEVC_PRED_WEIGHT_CHROMA_L1, p->regs + VE_HEVC_SRAM_ADDR);


		for (i = 0; i < p->slice.num_ref_idx_l1_active_minus1 + 1; i++)
			writel(((p->slice.delta_chroma_weight_l1[i][0] & 0xff) << 0) |
				((ChromaOffsetL1(i, 0) & 0xff) << 8) |
				((p->slice.delta_chroma_weight_l1[i][1] & 0xff) << 16) |
				((ChromaOffsetL1(i, 1) & 0xff) << 24), p->regs + VE_HEVC_SRAM_DATA);
	}
}

static void write_scaling_lists(struct h265_private *p)
{
	static const uint8_t diag4x4[16] = {
		 0,  1,  3,  6,
		 2,  4,  7, 10,
		 5,  8, 11, 13,
		 9, 12, 14, 15,
	};

	static const uint8_t diag8x8[64] = {
		 0,  1,  3,  6, 10, 15, 21, 28,
		 2,  4,  7, 11, 16, 22, 29, 36,
		 5,  8, 12, 17, 23, 30, 37, 43,
		 9, 13, 18, 24, 31, 38, 44, 49,
		14, 19, 25, 32, 39, 45, 50, 54,
		20, 26, 33, 40, 46, 51, 55, 58,
		27, 34, 41, 47, 52, 56, 59, 61,
		35, 42, 48, 53, 57, 60, 62, 63,
	};

	uint32_t i, j, word = 0x0;

	writel((p->info->ScalingListDCCoeff32x32[1] << 24) |
		(p->info->ScalingListDCCoeff32x32[0] << 16) |
		(p->info->ScalingListDCCoeff16x16[1] << 8) |
		(p->info->ScalingListDCCoeff16x16[0] << 0), p->regs + VE_HEVC_SCALING_LIST_DC_COEF0);

	writel((p->info->ScalingListDCCoeff16x16[5] << 24) |
		(p->info->ScalingListDCCoeff16x16[4] << 16) |
		(p->info->ScalingListDCCoeff16x16[3] << 8) |
		(p->info->ScalingListDCCoeff16x16[2] << 0), p->regs + VE_HEVC_SCALING_LIST_DC_COEF1);

	writel(VE_SRAM_HEVC_SCALING_LISTS, p->regs + VE_HEVC_SRAM_ADDR);

	for (i = 0; i < 6; i++)
	{
		for (j = 0; j < 64; j++)
		{
			word |= p->info->ScalingList8x8[i][diag8x8[j]] << ((j % 4) * 8);

			if (j % 4 == 3)
			{
				writel(word, p->regs + VE_HEVC_SRAM_DATA);
				word = 0x0;
			}
		}
	}

	for (i = 0; i < 2; i++)
	{
		for (j = 0; j < 64; j++)
		{
			word |= p->info->ScalingList32x32[i][diag8x8[j]] << ((j % 4) * 8);

			if (j % 4 == 3)
			{
				writel(word, p->regs + VE_HEVC_SRAM_DATA);
				word = 0x0;
			}
		}
	}

	for (i = 0; i < 6; i++)
	{
		for (j = 0; j < 64; j++)
		{
			word |= p->info->ScalingList16x16[i][diag8x8[j]] << ((j % 4) * 8);

			if (j % 4 == 3)
			{
				writel(word, p->regs + VE_HEVC_SRAM_DATA);
				word = 0x0;
			}
		}
	}

	for (i = 0; i < 6; i++)
	{
		for (j = 0; j < 16; j++)
		{
			word |= p->info->ScalingList4x4[i][diag4x4[j]] << ((j % 4) * 8);

			if (j % 4 == 3)
			{
				writel(word, p->regs + VE_HEVC_SRAM_DATA);
				word = 0x0;
			}
		}
	}

	writel((0x1 << 31), p->regs + VE_HEVC_SCALING_LIST_CTRL);
}

static VdpStatus h265_decode(decoder_ctx_t *decoder,
                             VdpPictureInfo const *_info,
                             const int len,
                             video_surface_ctx_t *output)
{
	struct h265_private *p = decoder->private;
	p->info = (VdpPictureInfoHEVC const *)_info;
	p->decoder = decoder;
	p->output = output;
	memset(&p->slice, 0, sizeof(p->slice));

	VdpStatus ret = yuv_prepare(output);
	if (ret != VDP_STATUS_OK)
		return ret;

	p->regs = cedrus_ve_get(decoder->device->cedrus, CEDRUS_ENGINE_HEVC, 0x0);

	int pos = 0;
	while ((pos = find_startcode(cedrus_mem_get_pointer(decoder->data), len, pos)) != -1)
	{
		writel((cedrus_mem_get_bus_addr(decoder->data) + VBV_SIZE - 1) >> 8, p->regs + VE_HEVC_BITS_END_ADDR);
		writel((len - pos) * 8, p->regs + VE_HEVC_BITS_LEN);
		writel(pos * 8, p->regs + VE_HEVC_BITS_OFFSET);
		writel((cedrus_mem_get_bus_addr(decoder->data) >> 8) | (0x7 << 28), p->regs + VE_HEVC_BITS_ADDR);

		writel(0x7, p->regs + VE_HEVC_TRIG);

		get_u(p->regs, 1);
		p->nal_unit_type = get_u(p->regs, 6);
		get_u(p->regs, 6);
		get_u(p->regs, 3);

		slice_header(p);

		writel(0x40 | p->nal_unit_type, p->regs + VE_HEVC_NAL_HDR);

		writel(((p->info->strong_intra_smoothing_enabled_flag & 0x1) << 26) |
			((p->info->sps_temporal_mvp_enabled_flag & 0x1) << 25) |
			((p->info->sample_adaptive_offset_enabled_flag & 0x1) << 24) |
			((p->info->amp_enabled_flag & 0x1) << 23) |
			((p->info->max_transform_hierarchy_depth_intra & 0x7) << 20) |
			((p->info->max_transform_hierarchy_depth_inter & 0x7) << 17) |
			((p->info->log2_diff_max_min_transform_block_size & 0x3) << 15) |
			((p->info->log2_min_transform_block_size_minus2 & 0x3) << 13) |
			((p->info->log2_diff_max_min_luma_coding_block_size & 0x3) << 11) |
			((p->info->log2_min_luma_coding_block_size_minus3 & 0x3) << 9) |
			((p->info->chroma_format_idc & 0x3) << 0), p->regs + VE_HEVC_SPS);

		writel((decoder->height << 16) | decoder->width, p->regs + VE_HEVC_PIC_SIZE);

		writel(((p->info->pcm_enabled_flag & 0x1) << 15) |
			((p->info->log2_diff_max_min_pcm_luma_coding_block_size & 0x3) << 10) |
			((p->info->log2_min_pcm_luma_coding_block_size_minus3 & 0x3) << 8) |
			((p->info->pcm_sample_bit_depth_chroma_minus1 & 0xf) << 4) |
			((p->info->pcm_sample_bit_depth_luma_minus1 & 0xf) << 0), p->regs + VE_HEVC_PCM_HDR);

		writel(((p->info->pps_cr_qp_offset & 0x1f) << 24) |
			((p->info->pps_cb_qp_offset & 0x1f) << 16) |
			((p->info->init_qp_minus26 & 0xff) << 8) |
			((p->info->diff_cu_qp_delta_depth & 0xf) << 4) |
			((p->info->cu_qp_delta_enabled_flag & 0x1) << 3) |
			((p->info->transform_skip_enabled_flag & 0x1) << 2) |
			((p->info->constrained_intra_pred_flag & 0x1) << 1) |
			((p->info->sign_data_hiding_enabled_flag & 0x1) << 0), p->regs + VE_HEVC_PPS0);
		writel(((p->info->log2_parallel_merge_level_minus2 & 0x7) << 8) |
			((p->info->pps_loop_filter_across_slices_enabled_flag & 0x1) << 6) |
			((p->info->loop_filter_across_tiles_enabled_flag & 0x1) << 5) |
			((p->info->entropy_coding_sync_enabled_flag & 0x1) << 4) |
			((p->info->tiles_enabled_flag & 0x1) << 3) |
			((p->info->transquant_bypass_enabled_flag & 0x1) << 2) |
			((p->info->weighted_bipred_flag & 0x1) << 1) |
			((p->info->weighted_pred_flag & 0x1) << 0), p->regs + VE_HEVC_PPS1);

		if (p->info->scaling_list_enabled_flag)
			write_scaling_lists(p);
		else
			writel((0x1 << 30), p->regs + VE_HEVC_SCALING_LIST_CTRL);

		writel(((p->slice.five_minus_max_num_merge_cand & 0x7) << 24) |
			((p->slice.num_ref_idx_l1_active_minus1 & 0xf) << 20) |
			((p->slice.num_ref_idx_l0_active_minus1 & 0xf) << 16) |
			((p->slice.collocated_ref_idx & 0xf) << 12) |
			((p->slice.collocated_from_l0_flag & 0x1) << 11) |
			((p->slice.cabac_init_flag & 0x1) << 10) |
			((p->slice.mvd_l1_zero_flag & 0x1) << 9) |
			((p->slice.slice_sao_chroma_flag & 0x1) << 8) |
			((p->slice.slice_sao_luma_flag & 0x1) << 7) |
			((p->slice.slice_temporal_mvp_enabled_flag & 0x1) << 6) |
			((p->slice.slice_type & 0x3) << 2) |
			((p->slice.dependent_slice_segment_flag & 0x1) << 1) |
			((p->slice.first_slice_segment_in_pic_flag & 0x1) << 0), p->regs + VE_HEVC_SLICE_HDR0);
		writel(((p->slice.slice_tc_offset_div2 & 0xf) << 28) |
			((p->slice.slice_beta_offset_div2 & 0xf) << 24) |
			((p->slice.slice_deblocking_filter_disabled_flag & 0x1) << 23) |
			((p->slice.slice_loop_filter_across_slices_enabled_flag & 0x1) << 22) |
			(((p->info->NumPocStCurrAfter == 0) & 0x1) << 21) |
			((p->slice.slice_cr_qp_offset & 0x1f) << 16) |
			((p->slice.slice_cb_qp_offset & 0x1f) << 8) |
			((p->slice.slice_qp_delta & 0x3f) << 0), p->regs + VE_HEVC_SLICE_HDR1);
		writel(((p->slice.num_entry_point_offsets) << 8) |
			(((p->slice.luma_log2_weight_denom + p->slice.delta_chroma_log2_weight_denom) & 0xf) << 4) |
			((p->slice.luma_log2_weight_denom & 0xf) << 0), p->regs + VE_HEVC_SLICE_HDR2);

		if (p->slice.first_slice_segment_in_pic_flag)
			writel(0x0, p->regs + VE_HEVC_CTU_NUM);

		writel(((p->slice.slice_segment_address / PicWidthInCtbsY) << 16) | ((p->slice.slice_segment_address % PicWidthInCtbsY) << 0), p->regs + VE_HEVC_CTB_ADDR);
		writel(0x00000007, p->regs + VE_HEVC_CTRL);

		writel(0xc0000000, p->regs + VE_EXTRA_OUT_FMT_OFFSET);
		writel((0x2 << 4), p->regs + 0x0ec);
		writel(output->chroma_size / 2, p->regs + 0x0c4);
		writel((ALIGN(decoder->width / 2, 16) << 16) | ALIGN(decoder->width, 32), p->regs + 0x0c8);
		writel(0x00000000, p->regs + 0x0cc);
		writel(0x00000000, p->regs + 0x550);
		writel(0x00000000, p->regs + 0x554);
		writel(0x00000000, p->regs + 0x558);

		write_entry_point_list(p);

		writel(0x0, p->regs + 0x580);
		writel(cedrus_mem_get_bus_addr(p->neighbor_info) >> 8, p->regs + VE_HEVC_NEIGHBOR_INFO_ADDR);

		write_pic_list(p);

		write_ref_pic_lists(p);
		write_weighted_pred(p);

		writel(0x8, p->regs + VE_HEVC_TRIG);
		cedrus_ve_wait(decoder->device->cedrus, 1);

		writel(readl(p->regs + VE_HEVC_STATUS) & 0x7, p->regs + VE_HEVC_STATUS);
	}

	cedrus_ve_put(decoder->device->cedrus);

	return VDP_STATUS_OK;
}

static void h265_private_free(decoder_ctx_t *decoder)
{
	struct h265_private *p = decoder->private;

	cedrus_mem_free(p->neighbor_info);
	cedrus_mem_free(p->entry_points);

	free(p);
}

VdpStatus new_decoder_h265(decoder_ctx_t *decoder)
{
	struct h265_private *p = calloc(1, sizeof(*p));
	if (!p)
		return VDP_STATUS_RESOURCES;

	p->neighbor_info = cedrus_mem_alloc(decoder->device->cedrus, 397 * 1024);
	p->entry_points = cedrus_mem_alloc(decoder->device->cedrus, 4 * 1024);

	decoder->decode = h265_decode;
	decoder->private = p;
	decoder->private_free = h265_private_free;

	return VDP_STATUS_OK;
}
