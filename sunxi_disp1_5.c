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

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "kernel-headers/drv_display.h"
#include "vdpau_private.h"
#include "sunxi_disp.h"

struct sunxi_disp1_5_private
{
	struct sunxi_disp pub;

	int fd;
	disp_layer_info video_info;
	int layer_id;
	unsigned int screen_width;
};

static void sunxi_disp1_5_close(struct sunxi_disp *sunxi_disp);
static int sunxi_disp1_5_set_video_layer(struct sunxi_disp *sunxi_disp, int x, int y, int width, int height, output_surface_ctx_t *surface);
static void sunxi_disp1_5_close_video_layer(struct sunxi_disp *sunxi_disp);
static int sunxi_disp1_5_set_osd_layer(struct sunxi_disp *sunxi_disp, int x, int y, int width, int height, output_surface_ctx_t *surface);
static void sunxi_disp1_5_close_osd_layer(struct sunxi_disp *sunxi_disp);

struct sunxi_disp *sunxi_disp1_5_open(int osd_enabled)
{
	struct sunxi_disp1_5_private *disp = calloc(1, sizeof(*disp));

	disp->fd = open("/dev/disp", O_RDWR);
	if (disp->fd == -1)
		goto err_open;

	unsigned long args[3] = { 0, 0, (unsigned long) &disp->video_info };

	disp->layer_id = 1;
	args[1] = disp->layer_id;

	disp->video_info.mode = DISP_LAYER_WORK_MODE_SCALER;
	disp->video_info.alpha_mode = 1;
	disp->video_info.alpha_value = 255;
	disp->video_info.pipe = 1;
	disp->video_info.ck_enable = 0;
	disp->video_info.b_trd_out = 0;
	disp->video_info.zorder = 1;

	if (ioctl(disp->fd, DISP_CMD_LAYER_DISABLE, args))
		goto err_video_layer;

	if (ioctl(disp->fd, DISP_CMD_LAYER_SET_INFO, args))
		goto err_video_layer;

	disp->screen_width = ioctl(disp->fd, DISP_CMD_GET_SCN_WIDTH, args);

	disp->pub.close = sunxi_disp1_5_close;
	disp->pub.set_video_layer = sunxi_disp1_5_set_video_layer;
	disp->pub.close_video_layer = sunxi_disp1_5_close_video_layer;
	disp->pub.set_osd_layer = sunxi_disp1_5_set_osd_layer;
	disp->pub.close_osd_layer = sunxi_disp1_5_close_osd_layer;

	return (struct sunxi_disp *)disp;

err_video_layer:
	close(disp->fd);
err_open:
	free(disp);
	return NULL;
}

static void sunxi_disp1_5_close(struct sunxi_disp *sunxi_disp)
{
	struct sunxi_disp1_5_private *disp = (struct sunxi_disp1_5_private *)sunxi_disp;

	unsigned long args[2] = { 0, disp->layer_id };

	ioctl(disp->fd, DISP_CMD_LAYER_DISABLE, args);

	close(disp->fd);
	free(sunxi_disp);
}

static int sunxi_disp1_5_set_video_layer(struct sunxi_disp *sunxi_disp, int x, int y, int width, int height, output_surface_ctx_t *surface)
{
	struct sunxi_disp1_5_private *disp = (struct sunxi_disp1_5_private *)sunxi_disp;

	disp_window src = { .x = surface->video_src_rect.x0, .y = surface->video_src_rect.y0,
			    .width = surface->video_src_rect.x1 - surface->video_src_rect.x0,
			    .height = surface->video_src_rect.y1 - surface->video_src_rect.y0 };
	disp_window scn = { .x = x + surface->video_dst_rect.x0, .y = y + surface->video_dst_rect.y0,
			    .width = surface->video_dst_rect.x1 - surface->video_dst_rect.x0,
			    .height = surface->video_dst_rect.y1 - surface->video_dst_rect.y0 };

	if (scn.y < 0)
	{
		int scn_clip = -scn.y;
		int src_clip = scn_clip * src.height / scn.height;
		scn.y = 0;
		scn.height -= scn_clip;
		src.y += src_clip;
		src.height -= src_clip;
	}
	if (scn.x < 0)
	{
		int scn_clip = -scn.x;
		int src_clip = scn_clip * src.width / scn.width;
		scn.x = 0;
		scn.width -= scn_clip;
		src.x += src_clip;
		src.width -= src_clip;
	}
	if (scn.x + scn.width > disp->screen_width)
	{
		int scn_clip = scn.x + scn.width - disp->screen_width;
		int src_clip = scn_clip * src.width / scn.width;
		scn.width -= scn_clip;
		src.width -= src_clip;
	}

	unsigned long args[3] = { 0, disp->layer_id, (unsigned long)(&disp->video_info) };
	switch (surface->vs->source_format)
	{
	case VDP_YCBCR_FORMAT_YUYV:
		disp->video_info.fb.format = DISP_FORMAT_YUV422_I_YUYV;
		break;
	case VDP_YCBCR_FORMAT_UYVY:
		disp->video_info.fb.format = DISP_FORMAT_YUV422_I_UYVY;
		break;
	case VDP_YCBCR_FORMAT_NV12:
		disp->video_info.fb.format = DISP_FORMAT_YUV420_SP_UVUV;
		break;
	case INTERNAL_YCBCR_FORMAT:
		disp->video_info.fb.format = DISP_FORMAT_YUV420_SP_TILE_UVUV;
		break;
	case VDP_YCBCR_FORMAT_YV12:
	default:
		disp->video_info.fb.format = DISP_FORMAT_YUV420_P;
		break;
	}

	disp->video_info.fb.addr[0] = surface->yuv->data->phys + 0x40000000;
	disp->video_info.fb.addr[1] = surface->yuv->data->phys + surface->vs->luma_size + 0x40000000;
	disp->video_info.fb.addr[2] = surface->yuv->data->phys + surface->vs->luma_size + surface->vs->chroma_size / 2 + 0x40000000;

	disp->video_info.fb.size.width = surface->vs->width;
	disp->video_info.fb.size.height = surface->vs->height;
	disp->video_info.fb.src_win = src;
	disp->video_info.screen_win = scn;
	disp->video_info.fb.pre_multiply = 1;

	if (ioctl(disp->fd, DISP_CMD_LAYER_ENABLE, args))
		return -EINVAL;

	if (ioctl(disp->fd, DISP_CMD_LAYER_SET_INFO, args))
		return -EINVAL;

	return 0;
}

static void sunxi_disp1_5_close_video_layer(struct sunxi_disp *sunxi_disp)
{
	struct sunxi_disp1_5_private *disp = (struct sunxi_disp1_5_private *)sunxi_disp;

	unsigned long args[2] = { 0, disp->layer_id };

	ioctl(disp->fd, DISP_CMD_LAYER_DISABLE, args);
}

static int sunxi_disp1_5_set_osd_layer(struct sunxi_disp *sunxi_disp, int x, int y, int width, int height, output_surface_ctx_t *surface)
{
	return -ENOTSUP;
}

static void sunxi_disp1_5_close_osd_layer(struct sunxi_disp *sunxi_disp)
{
	return;
}
