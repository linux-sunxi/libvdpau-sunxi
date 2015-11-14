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
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include "kernel-headers/sunxi_disp_ioctl.h"
#include "ve.h"
#include "rgba.h"

static uint64_t get_time(void)
{
	struct timespec tp;

	if (clock_gettime(CLOCK_MONOTONIC, &tp) == -1)
		return 0;

	return (uint64_t)tp.tv_sec * 1000000000ULL + (uint64_t)tp.tv_nsec;
}

VdpStatus vdp_presentation_queue_target_create_x11(VdpDevice device,
                                                   Drawable drawable,
                                                   VdpPresentationQueueTarget *target)
{
	if (!target || !drawable)
		return VDP_STATUS_INVALID_POINTER;

	device_ctx_t *dev = handle_get(device);
	if (!dev)
		return VDP_STATUS_INVALID_HANDLE;

	queue_target_ctx_t *qt = handle_create(sizeof(*qt), target);
	if (!qt)
		return VDP_STATUS_RESOURCES;

	qt->drawable = drawable;
	qt->fd = open("/dev/disp", O_RDWR);
	if (qt->fd == -1)
	{
		handle_destroy(*target);
		return VDP_STATUS_ERROR;
	}

	int tmp = SUNXI_DISP_VERSION;
	if (ioctl(qt->fd, DISP_CMD_VERSION, &tmp) < 0)
	{
		close(qt->fd);
		handle_destroy(*target);
		return VDP_STATUS_ERROR;
	}

	uint32_t args[4] = { 0, DISP_LAYER_WORK_MODE_SCALER, 0, 0 };
	qt->layer = ioctl(qt->fd, DISP_CMD_LAYER_REQUEST, args);
	if (qt->layer == 0)
		goto out_layer;

	args[1] = qt->layer;
	ioctl(qt->fd, dev->osd_enabled ? DISP_CMD_LAYER_TOP : DISP_CMD_LAYER_BOTTOM, args);

	if (dev->osd_enabled)
	{
		args[1] = DISP_LAYER_WORK_MODE_NORMAL;
		qt->layer_top = ioctl(qt->fd, DISP_CMD_LAYER_REQUEST, args);
		if (qt->layer_top == 0)
			goto out_layer_top;

		args[1] = qt->layer_top;
		ioctl(qt->fd, DISP_CMD_LAYER_TOP, args);
	}

	XSetWindowBackground(dev->display, drawable, 0x000102);

	if (!dev->osd_enabled)
	{
		__disp_colorkey_t ck;
		ck.ck_max.red = ck.ck_min.red = 0;
		ck.ck_max.green = ck.ck_min.green = 1;
		ck.ck_max.blue = ck.ck_min.blue = 2;
		ck.red_match_rule = 2;
		ck.green_match_rule = 2;
		ck.blue_match_rule = 2;

		args[1] = (unsigned long)(&ck);
		ioctl(qt->fd, DISP_CMD_SET_COLORKEY, args);
	}


	return VDP_STATUS_OK;

out_layer_top:
	args[1] = qt->layer;
	ioctl(qt->fd, DISP_CMD_LAYER_RELEASE, args);
out_layer:
	close(qt->fd);
	handle_destroy(*target);
	return VDP_STATUS_RESOURCES;
}

VdpStatus vdp_presentation_queue_target_destroy(VdpPresentationQueueTarget presentation_queue_target)
{
	queue_target_ctx_t *qt = handle_get(presentation_queue_target);
	if (!qt)
		return VDP_STATUS_INVALID_HANDLE;

	uint32_t args[4] = { 0, qt->layer, 0, 0 };
	ioctl(qt->fd, DISP_CMD_LAYER_CLOSE, args);
	ioctl(qt->fd, DISP_CMD_LAYER_RELEASE, args);

	if (qt->layer_top)
	{
		args[1] = qt->layer_top;
		ioctl(qt->fd, DISP_CMD_LAYER_CLOSE, args);
		ioctl(qt->fd, DISP_CMD_LAYER_RELEASE, args);
	}

	close(qt->fd);

	handle_destroy(presentation_queue_target);

	return VDP_STATUS_OK;
}

VdpStatus vdp_presentation_queue_create(VdpDevice device,
                                        VdpPresentationQueueTarget presentation_queue_target,
                                        VdpPresentationQueue *presentation_queue)
{
	if (!presentation_queue)
		return VDP_STATUS_INVALID_POINTER;

	device_ctx_t *dev = handle_get(device);
	if (!dev)
		return VDP_STATUS_INVALID_HANDLE;

	queue_target_ctx_t *qt = handle_get(presentation_queue_target);
	if (!qt)
		return VDP_STATUS_INVALID_HANDLE;

	queue_ctx_t *q = handle_create(sizeof(*q), presentation_queue);
	if (!q)
		return VDP_STATUS_RESOURCES;

	q->target = qt;
	q->device = dev;

	return VDP_STATUS_OK;
}

VdpStatus vdp_presentation_queue_destroy(VdpPresentationQueue presentation_queue)
{
	queue_ctx_t *q = handle_get(presentation_queue);
	if (!q)
		return VDP_STATUS_INVALID_HANDLE;

	handle_destroy(presentation_queue);

	return VDP_STATUS_OK;
}

VdpStatus vdp_presentation_queue_set_background_color(VdpPresentationQueue presentation_queue,
                                                      VdpColor *const background_color)
{
	if (!background_color)
		return VDP_STATUS_INVALID_POINTER;

	queue_ctx_t *q = handle_get(presentation_queue);
	if (!q)
		return VDP_STATUS_INVALID_HANDLE;

	q->background.red = background_color->red;
	q->background.green = background_color->green;
	q->background.blue = background_color->blue;
	q->background.alpha = background_color->alpha;

	return VDP_STATUS_OK;
}

VdpStatus vdp_presentation_queue_get_background_color(VdpPresentationQueue presentation_queue,
                                                      VdpColor *const background_color)
{
	if (!background_color)
		return VDP_STATUS_INVALID_POINTER;

	queue_ctx_t *q = handle_get(presentation_queue);
	if (!q)
		return VDP_STATUS_INVALID_HANDLE;

	background_color->red = q->background.red;
	background_color->green = q->background.green;
	background_color->blue = q->background.blue;
	background_color->alpha = q->background.alpha;

	return VDP_STATUS_OK;
}

VdpStatus vdp_presentation_queue_get_time(VdpPresentationQueue presentation_queue,
                                          VdpTime *current_time)
{
	queue_ctx_t *q = handle_get(presentation_queue);
	if (!q)
		return VDP_STATUS_INVALID_HANDLE;

	*current_time = get_time();
	return VDP_STATUS_OK;
}

VdpStatus vdp_presentation_queue_display(VdpPresentationQueue presentation_queue,
                                         VdpOutputSurface surface,
                                         uint32_t clip_width,
                                         uint32_t clip_height,
                                         VdpTime earliest_presentation_time)
{
	queue_ctx_t *q = handle_get(presentation_queue);
	if (!q)
		return VDP_STATUS_INVALID_HANDLE;

	output_surface_ctx_t *os = handle_get(surface);
	if (!os)
		return VDP_STATUS_INVALID_HANDLE;

	if (earliest_presentation_time != 0)
		VDPAU_DBG_ONCE("Presentation time not supported");

	Window c;
	int x,y;
	XTranslateCoordinates(q->device->display, q->target->drawable, RootWindow(q->device->display, q->device->screen), 0, 0, &x, &y, &c);
	XClearWindow(q->device->display, q->target->drawable);

	if (os->vs)
	{
		// VIDEO layer
		__disp_layer_info_t layer_info;
		memset(&layer_info, 0, sizeof(layer_info));
		layer_info.pipe = q->device->osd_enabled ? 0 : 1;
		layer_info.mode = DISP_LAYER_WORK_MODE_SCALER;
		layer_info.fb.format = DISP_FORMAT_YUV420;
		layer_info.fb.seq = DISP_SEQ_UVUV;
		switch (os->vs->source_format) {
		case VDP_YCBCR_FORMAT_YUYV:
			layer_info.fb.mode = DISP_MOD_INTERLEAVED;
			layer_info.fb.format = DISP_FORMAT_YUV422;
			layer_info.fb.seq = DISP_SEQ_YUYV;
			break;
		case VDP_YCBCR_FORMAT_UYVY:
			layer_info.fb.mode = DISP_MOD_INTERLEAVED;
			layer_info.fb.format = DISP_FORMAT_YUV422;
			layer_info.fb.seq = DISP_SEQ_UYVY;
			break;
		case VDP_YCBCR_FORMAT_NV12:
			layer_info.fb.mode = DISP_MOD_NON_MB_UV_COMBINED;
			break;
		case VDP_YCBCR_FORMAT_YV12:
			layer_info.fb.mode = DISP_MOD_NON_MB_PLANAR;
			break;
		default:
		case INTERNAL_YCBCR_FORMAT:
			layer_info.fb.mode = DISP_MOD_MB_UV_COMBINED;
			break;
		}
		layer_info.fb.br_swap = 0;
		layer_info.fb.addr[0] = os->yuv->data->phys + 0x40000000;
		layer_info.fb.addr[1] = os->yuv->data->phys + os->vs->luma_size + 0x40000000;
		layer_info.fb.addr[2] = os->yuv->data->phys + os->vs->luma_size + os->vs->luma_size / 4 + 0x40000000;

		layer_info.fb.cs_mode = DISP_BT601;
		layer_info.fb.size.width = os->vs->width;
		layer_info.fb.size.height = os->vs->height;
		layer_info.src_win.x = os->video_src_rect.x0;
		layer_info.src_win.y = os->video_src_rect.y0;
		layer_info.src_win.width = os->video_src_rect.x1 - os->video_src_rect.x0;
		layer_info.src_win.height = os->video_src_rect.y1 - os->video_src_rect.y0;
		layer_info.scn_win.x = x + os->video_dst_rect.x0;
		layer_info.scn_win.y = y + os->video_dst_rect.y0;
		layer_info.scn_win.width = os->video_dst_rect.x1 - os->video_dst_rect.x0;
		layer_info.scn_win.height = os->video_dst_rect.y1 - os->video_dst_rect.y0;
		layer_info.ck_enable = q->device->osd_enabled ? 0 : 1;

		if (layer_info.scn_win.y < 0)
		{
			int cutoff = -(layer_info.scn_win.y);
			layer_info.src_win.y += cutoff;
			layer_info.src_win.height -= cutoff;
			layer_info.scn_win.y = 0;
			layer_info.scn_win.height -= cutoff;
		}

		uint32_t args[4] = { 0, q->target->layer, (unsigned long)(&layer_info), 0 };
		ioctl(q->target->fd, DISP_CMD_LAYER_SET_PARA, args);

		ioctl(q->target->fd, DISP_CMD_LAYER_OPEN, args);
		// Note: might be more reliable (but slower and problematic when there
		// are driver issues and the GET functions return wrong values) to query the
		// old values instead of relying on our internal csc_change.
		// Since the driver calculates a matrix out of these values after each
		// set doing this unconditionally is costly.
		if (os->csc_change) {
			ioctl(q->target->fd, DISP_CMD_LAYER_ENHANCE_OFF, args);
			args[2] = 0xff * os->brightness + 0x20;
			ioctl(q->target->fd, DISP_CMD_LAYER_SET_BRIGHT, args);
			args[2] = 0x20 * os->contrast;
			ioctl(q->target->fd, DISP_CMD_LAYER_SET_CONTRAST, args);
			args[2] = 0x20 * os->saturation;
			ioctl(q->target->fd, DISP_CMD_LAYER_SET_SATURATION, args);
			// hue scale is randomly chosen, no idea how it maps exactly
			args[2] = (32 / 3.14) * os->hue + 0x20;
			ioctl(q->target->fd, DISP_CMD_LAYER_SET_HUE, args);
			ioctl(q->target->fd, DISP_CMD_LAYER_ENHANCE_ON, args);
			os->csc_change = 0;
		}
	}
	else
	{
		uint32_t args[4] = { 0, q->target->layer, 0, 0 };
		ioctl(q->target->fd, DISP_CMD_LAYER_CLOSE, args);
	}

	if (!q->device->osd_enabled)
		return VDP_STATUS_OK;

	if (os->rgba.flags & RGBA_FLAG_NEEDS_CLEAR)
		rgba_clear(&os->rgba);

	if (os->rgba.flags & RGBA_FLAG_DIRTY)
	{
		// TOP layer
		rgba_flush(&os->rgba);

		__disp_layer_info_t layer_info;
		memset(&layer_info, 0, sizeof(layer_info));
		layer_info.pipe = 1;
		layer_info.mode = DISP_LAYER_WORK_MODE_NORMAL;
		layer_info.fb.mode = DISP_MOD_INTERLEAVED;
		layer_info.fb.format = DISP_FORMAT_ARGB8888;
		layer_info.fb.seq = DISP_SEQ_ARGB;
		switch (os->rgba.format)
		{
		case VDP_RGBA_FORMAT_R8G8B8A8:
			layer_info.fb.br_swap = 1;
			break;
		case VDP_RGBA_FORMAT_B8G8R8A8:
		default:
			layer_info.fb.br_swap = 0;
			break;
		}
		layer_info.fb.addr[0] = os->rgba.data->phys + 0x40000000;
		layer_info.fb.cs_mode = DISP_BT601;
		layer_info.fb.size.width = os->rgba.width;
		layer_info.fb.size.height = os->rgba.height;
		layer_info.src_win.x = os->rgba.dirty.x0;
		layer_info.src_win.y = os->rgba.dirty.y0;
		layer_info.src_win.width = os->rgba.dirty.x1 - os->rgba.dirty.x0;
		layer_info.src_win.height = os->rgba.dirty.y1 - os->rgba.dirty.y0;
		layer_info.scn_win.x = x + os->rgba.dirty.x0;
		layer_info.scn_win.y = y + os->rgba.dirty.y0;
		layer_info.scn_win.width = min_nz(clip_width, os->rgba.dirty.x1) - os->rgba.dirty.x0;
		layer_info.scn_win.height = min_nz(clip_height, os->rgba.dirty.y1) - os->rgba.dirty.y0;

		uint32_t args[4] = { 0, q->target->layer_top, (unsigned long)(&layer_info), 0 };
		ioctl(q->target->fd, DISP_CMD_LAYER_SET_PARA, args);

		ioctl(q->target->fd, DISP_CMD_LAYER_OPEN, args);
	}
	else
	{
		uint32_t args[4] = { 0, q->target->layer_top, 0, 0 };
		ioctl(q->target->fd, DISP_CMD_LAYER_CLOSE, args);
	}

	return VDP_STATUS_OK;
}

VdpStatus vdp_presentation_queue_block_until_surface_idle(VdpPresentationQueue presentation_queue,
                                                          VdpOutputSurface surface,
                                                          VdpTime *first_presentation_time)
{
	queue_ctx_t *q = handle_get(presentation_queue);
	if (!q)
		return VDP_STATUS_INVALID_HANDLE;

	output_surface_ctx_t *out = handle_get(surface);
	if (!out)
		return VDP_STATUS_INVALID_HANDLE;

	*first_presentation_time = get_time();

	return VDP_STATUS_OK;
}

VdpStatus vdp_presentation_queue_query_surface_status(VdpPresentationQueue presentation_queue,
                                                      VdpOutputSurface surface,
                                                      VdpPresentationQueueStatus *status,
                                                      VdpTime *first_presentation_time)
{
	queue_ctx_t *q = handle_get(presentation_queue);
	if (!q)
		return VDP_STATUS_INVALID_HANDLE;

	output_surface_ctx_t *out = handle_get(surface);
	if (!out)
		return VDP_STATUS_INVALID_HANDLE;

	*status = VDP_PRESENTATION_QUEUE_STATUS_VISIBLE;
	*first_presentation_time = get_time();

	return VDP_STATUS_OK;
}
