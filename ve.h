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

#ifndef __VE_H__
#define __VE_H__

#include <stdint.h>

int ve_open(void);
void ve_close(void);
int ve_get_version(void);
int ve_wait(int timeout);
void *ve_get(int engine, uint32_t flags);
void ve_put(void);

struct ve_mem
{
	void *virt;
	uint32_t phys;
	int size;
};

struct ve_mem *ve_malloc(int size);
void ve_free(struct ve_mem *mem);
void ve_flush_cache(struct ve_mem *mem);

static inline void writel(uint32_t val, void *addr)
{
	*((volatile uint32_t *)addr) = val;
}

static inline uint32_t readl(void *addr)
{
	return *((volatile uint32_t *) addr);
}

#define VE_ENGINE_MPEG			0x0
#define VE_ENGINE_H264			0x1
#define VE_ENGINE_HEVC			0x4

#define VE_CTRL				0x000
#define VE_EXTRA_OUT_FMT_OFFSET		0x0e8
#define VE_VERSION			0x0f0

#define VE_MPEG_PIC_HDR			0x100
#define VE_MPEG_VOP_HDR			0x104
#define VE_MPEG_SIZE			0x108
#define VE_MPEG_FRAME_SIZE		0x10c
#define VE_MPEG_MBA			0x110
#define VE_MPEG_CTRL			0x114
#define VE_MPEG_TRIGGER			0x118
#define VE_MPEG_STATUS			0x11c
#define VE_MPEG_TRBTRD_FIELD		0x120
#define VE_MPEG_TRBTRD_FRAME		0x124
#define VE_MPEG_VLD_ADDR		0x128
#define VE_MPEG_VLD_OFFSET		0x12c
#define VE_MPEG_VLD_LEN			0x130
#define VE_MPEG_VLD_END			0x134
#define VE_MPEG_MBH_ADDR		0x138
#define VE_MPEG_DCAC_ADDR		0x13c
#define VE_MPEG_NCF_ADDR		0x144
#define VE_MPEG_REC_LUMA		0x148
#define VE_MPEG_REC_CHROMA		0x14c
#define VE_MPEG_FWD_LUMA		0x150
#define VE_MPEG_FWD_CHROMA		0x154
#define VE_MPEG_BACK_LUMA		0x158
#define VE_MPEG_BACK_CHROMA		0x15c
#define VE_MPEG_IQ_MIN_INPUT		0x180
#define VE_MPEG_QP_INPUT		0x184

#define VE_MPEG_ROT_LUMA		0x1cc
#define VE_MPEG_ROT_CHROMA		0x1d0
#define VE_MPEG_SDROT_CTRL		0x1d4

#define VE_H264_FRAME_SIZE		0x200
#define VE_H264_PIC_HDR			0x204
#define VE_H264_SLICE_HDR		0x208
#define VE_H264_SLICE_HDR2		0x20c
#define VE_H264_PRED_WEIGHT		0x210
#define VE_H264_QP_PARAM		0x21c
#define VE_H264_CTRL			0x220
#define VE_H264_TRIGGER			0x224
#define VE_H264_STATUS			0x228
#define VE_H264_CUR_MB_NUM		0x22c
#define VE_H264_VLD_ADDR		0x230
#define VE_H264_VLD_OFFSET		0x234
#define VE_H264_VLD_LEN			0x238
#define VE_H264_VLD_END			0x23c
#define VE_H264_SDROT_CTRL		0x240
#define VE_H264_SDROT_LUMA		0x244
#define VE_H264_SDROT_CHROMA		0x248
#define VE_H264_OUTPUT_FRAME_IDX	0x24c
#define VE_H264_EXTRA_BUFFER1		0x250
#define VE_H264_EXTRA_BUFFER2		0x254
#define VE_H264_BASIC_BITS		0x2dc
#define VE_H264_RAM_WRITE_PTR		0x2e0
#define VE_H264_RAM_WRITE_DATA		0x2e4

#define VE_SRAM_H264_PRED_WEIGHT_TABLE	0x000
#define VE_SRAM_H264_FRAMEBUFFER_LIST	0x400
#define VE_SRAM_H264_REF_LIST0		0x640
#define VE_SRAM_H264_REF_LIST1		0x664
#define VE_SRAM_H264_SCALING_LISTS	0x800

#define VE_HEVC_NAL_HDR			0x500
#define VE_HEVC_SPS			0x504
#define VE_HEVC_PIC_SIZE		0x508
#define VE_HEVC_PCM_HDR			0x50c
#define VE_HEVC_PPS0			0x510
#define VE_HEVC_PPS1			0x514
#define VE_HEVC_SLICE_HDR0		0x520
#define VE_HEVC_SLICE_HDR1		0x524
#define VE_HEVC_SLICE_HDR2		0x528
#define VE_HEVC_CTB_ADDR		0x52c
#define VE_HEVC_CTRL			0x530
#define VE_HEVC_TRIG			0x534
#define VE_HEVC_STATUS			0x538
#define VE_HEVC_CTU_NUM			0x53c
#define VE_HEVC_BITS_ADDR		0x540
#define VE_HEVC_BITS_OFFSET		0x544
#define VE_HEVC_BITS_LEN		0x548
#define VE_HEVC_BITS_END_ADDR		0x54c
#define VE_HEVC_REC_BUF_IDX 		0x55c
#define VE_HEVC_NEIGHBOR_INFO_ADDR	0x560
#define VE_HEVC_TILE_LIST_ADDR		0x564
#define VE_HEVC_TILE_START_CTB		0x568
#define VE_HEVC_TILE_END_CTB		0x56c
#define VE_HEVC_BITS_DATA		0x5dc
#define VE_HEVC_SRAM_ADDR		0x5e0
#define VE_HEVC_SRAM_DATA		0x5e4

#define VE_SRAM_HEVC_PRED_WEIGHT_LUMA_L0	0x000
#define VE_SRAM_HEVC_PRED_WEIGHT_CHROMA_L0	0x020
#define VE_SRAM_HEVC_PRED_WEIGHT_LUMA_L1	0x060
#define VE_SRAM_HEVC_PRED_WEIGHT_CHROMA_L1	0x080
#define VE_SRAM_HEVG_PIC_LIST			0x400
#define VE_SRAM_HEVC_REF_PIC_LIST0		0xc00
#define VE_SRAM_HEVC_REF_PIC_LIST1		0xc10

#endif
