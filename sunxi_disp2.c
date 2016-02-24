/*
 * Copyright (c) 2015-2016 Jens Kuske <jenskuske@gmail.com>
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
#include "kernel-headers/sunxi_display2.h"
#include "vdpau_private.h"
#include "sunxi_disp.h"

struct sunxi_disp2_private
{
	struct sunxi_disp pub;

	int fd;
	disp_layer_config video_config;
	unsigned int screen_width;
	disp_layer_config osd_config;
};

static void sunxi_disp2_close(struct sunxi_disp *sunxi_disp);
static int sunxi_disp2_set_video_layer(struct sunxi_disp *sunxi_disp, int x, int y, int width, int height, output_surface_ctx_t *surface);
static void sunxi_disp2_close_video_layer(struct sunxi_disp *sunxi_disp);
static int sunxi_disp2_set_osd_layer(struct sunxi_disp *sunxi_disp, int x, int y, int width, int height, output_surface_ctx_t *surface);
static void sunxi_disp2_close_osd_layer(struct sunxi_disp *sunxi_disp);

struct sunxi_disp *sunxi_disp2_open(int osd_enabled)
{
	struct sunxi_disp2_private *disp = calloc(1, sizeof(*disp));

	disp->fd = open("/dev/disp", O_RDWR);
	if (disp->fd == -1)
		goto err_open;

	unsigned long args[4] = { 0, (unsigned long)(&disp->video_config), 1, 0 };

	disp->video_config.info.mode = LAYER_MODE_BUFFER;
	disp->video_config.info.alpha_mode = 1;
	disp->video_config.info.alpha_value = 255;

	disp->video_config.enable = 0;
	disp->video_config.channel = 0;
	disp->video_config.layer_id = 0;
	disp->video_config.info.zorder = 1;

	if (ioctl(disp->fd, DISP_LAYER_SET_CONFIG, args))
		goto err_video_layer;

	if (osd_enabled)
	{
		disp->osd_config.info.mode = LAYER_MODE_BUFFER;
		disp->osd_config.info.alpha_mode = 0;
		disp->osd_config.info.alpha_value = 255;

		disp->osd_config.enable = 0;
		disp->osd_config.channel = 2;
		disp->osd_config.layer_id = 0;
		disp->osd_config.info.zorder = 2;

		args[1] = (unsigned long)(&disp->osd_config);
		if (ioctl(disp->fd, DISP_LAYER_SET_CONFIG, args))
			goto err_video_layer;
	}

	disp->screen_width = ioctl(disp->fd, DISP_GET_SCN_WIDTH, args);

	disp->pub.close = sunxi_disp2_close;
	disp->pub.set_video_layer = sunxi_disp2_set_video_layer;
	disp->pub.close_video_layer = sunxi_disp2_close_video_layer;
	disp->pub.set_osd_layer = sunxi_disp2_set_osd_layer;
	disp->pub.close_osd_layer = sunxi_disp2_close_osd_layer;

	return (struct sunxi_disp *)disp;

err_video_layer:
	close(disp->fd);
err_open:
	free(disp);
	return NULL;
}

static void sunxi_disp2_close(struct sunxi_disp *sunxi_disp)
{
	struct sunxi_disp2_private *disp = (struct sunxi_disp2_private *)sunxi_disp;

	unsigned long args[4] = { 0, (unsigned long)(&disp->video_config), 1, 0 };

	disp->video_config.enable = 0;
	ioctl(disp->fd, DISP_LAYER_SET_CONFIG, args);

	if (disp->osd_config.enable)
	{
		disp->osd_config.enable = 0;
		args[1] = (unsigned long)(&disp->osd_config);
		ioctl(disp->fd, DISP_LAYER_SET_CONFIG, args);
	}

	close(disp->fd);
	free(sunxi_disp);
}

static void clip(disp_rect *src, disp_rect *scn, unsigned int screen_width)
{
	if (scn->y < 0)
	{
		int scn_clip = -scn->y;
		int src_clip = scn_clip * src->height / scn->height;
		scn->y = 0;
		scn->height -= scn_clip;
		src->y += src_clip;
		src->height -= src_clip;
	}
	if (scn->x < 0)
	{
		int scn_clip = -scn->x;
		int src_clip = scn_clip * src->width / scn->width;
		scn->x = 0;
		scn->width -= scn_clip;
		src->x += src_clip;
		src->width -= src_clip;
	}
	if (scn->x + scn->width > screen_width)
	{
		int scn_clip = scn->x + scn->width - screen_width;
		int src_clip = scn_clip * src->width / scn->width;
		scn->width -= scn_clip;
		src->width -= src_clip;
	}
}

static int sunxi_disp2_set_video_layer(struct sunxi_disp *sunxi_disp, int x, int y, int width, int height, output_surface_ctx_t *surface)
{
	struct sunxi_disp2_private *disp = (struct sunxi_disp2_private *)sunxi_disp;

	disp_rect src = { .x = surface->video_src_rect.x0, .y = surface->video_src_rect.y0,
			  .width = surface->video_src_rect.x1 - surface->video_src_rect.x0,
			  .height = surface->video_src_rect.y1 - surface->video_src_rect.y0 };
	disp_rect scn = { .x = x + surface->video_dst_rect.x0, .y = y + surface->video_dst_rect.y0,
			  .width = surface->video_dst_rect.x1 - surface->video_dst_rect.x0,
			  .height = surface->video_dst_rect.y1 - surface->video_dst_rect.y0 };

	clip (&src, &scn, disp->screen_width);

	unsigned long args[4] = { 0, (unsigned long)(&disp->video_config), 1, 0 };
	switch (surface->vs->source_format)
	{
	case VDP_YCBCR_FORMAT_YUYV:
		disp->video_config.info.fb.format = DISP_FORMAT_YUV422_I_YUYV;
		break;
	case VDP_YCBCR_FORMAT_UYVY:
		disp->video_config.info.fb.format = DISP_FORMAT_YUV422_I_UYVY;
		break;
	case VDP_YCBCR_FORMAT_NV12:
		disp->video_config.info.fb.format = DISP_FORMAT_YUV420_SP_UVUV;
		break;
	case VDP_YCBCR_FORMAT_YV12:
	default:
	case INTERNAL_YCBCR_FORMAT:
		disp->video_config.info.fb.format = DISP_FORMAT_YUV420_P;
		break;
	}

	disp->video_config.info.fb.addr[0] = cedrus_mem_get_phys_addr(surface->yuv->data);
	disp->video_config.info.fb.addr[1] = cedrus_mem_get_phys_addr(surface->yuv->data) + surface->vs->luma_size;
	disp->video_config.info.fb.addr[2] = cedrus_mem_get_phys_addr(surface->yuv->data) + surface->vs->luma_size + surface->vs->chroma_size / 2;

	disp->video_config.info.fb.size[0].width = surface->vs->width;
	disp->video_config.info.fb.size[0].height = surface->vs->height;
	disp->video_config.info.fb.align[0] = 32;
	disp->video_config.info.fb.size[1].width = surface->vs->width / 2;
	disp->video_config.info.fb.size[1].height = surface->vs->height / 2;
	disp->video_config.info.fb.align[1] = 16;
	disp->video_config.info.fb.size[2].width = surface->vs->width / 2;
	disp->video_config.info.fb.size[2].height = surface->vs->height / 2;
	disp->video_config.info.fb.align[2] = 16;
	disp->video_config.info.fb.crop.x = (unsigned long long)(src.x) << 32;
	disp->video_config.info.fb.crop.y = (unsigned long long)(src.y) << 32;
	disp->video_config.info.fb.crop.width = (unsigned long long)(src.width) << 32;
	disp->video_config.info.fb.crop.height = (unsigned long long)(src.height) << 32;
	disp->video_config.info.screen_win = scn;
	disp->video_config.enable = 1;

	if (ioctl(disp->fd, DISP_LAYER_SET_CONFIG, args))
		return -EINVAL;

	return 0;
}

static void sunxi_disp2_close_video_layer(struct sunxi_disp *sunxi_disp)
{
	struct sunxi_disp2_private *disp = (struct sunxi_disp2_private *)sunxi_disp;

	unsigned long args[4] = { 0, (unsigned long)(&disp->video_config), 1, 0 };

	disp->video_config.enable = 0;

	ioctl(disp->fd, DISP_LAYER_SET_CONFIG, args);
}

static int sunxi_disp2_set_osd_layer(struct sunxi_disp *sunxi_disp, int x, int y, int width, int height, output_surface_ctx_t *surface)
{
	struct sunxi_disp2_private *disp = (struct sunxi_disp2_private *)sunxi_disp;

	unsigned long args[4] = { 0, (unsigned long)(&disp->osd_config), 1, 0 };

	disp_rect src = { .x = surface->rgba.dirty.x0, .y = surface->rgba.dirty.y0,
			  .width = surface->rgba.dirty.x1 - surface->rgba.dirty.x0,
			  .height = surface->rgba.dirty.y1 - surface->rgba.dirty.y0 };
	disp_rect scn = { .x = x + surface->rgba.dirty.x0, .y = y + surface->rgba.dirty.y0,
			  .width = min_nz(width, surface->rgba.dirty.x1) - surface->rgba.dirty.x0,
			  .height = min_nz(height, surface->rgba.dirty.y1) - surface->rgba.dirty.y0 };

	clip (&src, &scn, disp->screen_width);

	switch (surface->rgba.format)
	{
	case VDP_RGBA_FORMAT_R8G8B8A8:
		disp->osd_config.info.fb.format = DISP_FORMAT_ABGR_8888;
		break;
	case VDP_RGBA_FORMAT_B8G8R8A8:
	default:
		disp->osd_config.info.fb.format = DISP_FORMAT_ARGB_8888;
		break;
	}

	disp->osd_config.info.fb.addr[0] = cedrus_mem_get_phys_addr(surface->rgba.data);
	disp->osd_config.info.fb.size[0].width = surface->rgba.width;
	disp->osd_config.info.fb.size[0].height = surface->rgba.height;
	disp->osd_config.info.fb.align[0] = 1;
	disp->osd_config.info.fb.crop.x = (unsigned long long)(src.x) << 32;
	disp->osd_config.info.fb.crop.y = (unsigned long long)(src.y) << 32;
	disp->osd_config.info.fb.crop.width = (unsigned long long)(src.width) << 32;
	disp->osd_config.info.fb.crop.height = (unsigned long long)(src.height) << 32;
	disp->osd_config.info.screen_win = scn;
	disp->osd_config.enable = 1;

	if (ioctl(disp->fd, DISP_LAYER_SET_CONFIG, args))
		return -EINVAL;

	return 0;
}

static void sunxi_disp2_close_osd_layer(struct sunxi_disp *sunxi_disp)
{
	struct sunxi_disp2_private *disp = (struct sunxi_disp2_private *)sunxi_disp;

	unsigned long args[4] = { 0, (unsigned long)(&disp->osd_config), 1, 0 };

	disp->osd_config.enable = 0;

	ioctl(disp->fd, DISP_LAYER_SET_CONFIG, args);
}
