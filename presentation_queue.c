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
#include <stropts.h>
#include <string.h>
#include "sunxi_disp_ioctl.h"
#include "ve.h"

static uint64_t get_time(void)
{
	struct timespec tp;

	if (clock_gettime(CLOCK_MONOTONIC, &tp) == -1)
		return 0;

	return (uint64_t)tp.tv_sec * 1000000000ULL + (uint64_t)tp.tv_nsec;
}

VdpStatus vdp_presentation_queue_target_create_x11(VdpDevice device, Drawable drawable, VdpPresentationQueueTarget *target)
{
	if (!target || !drawable)
		return VDP_STATUS_INVALID_POINTER;

	device_ctx_t *dev = handle_get(device);
	if (!dev)
		return VDP_STATUS_INVALID_HANDLE;

	queue_target_ctx_t *qt = calloc(1, sizeof(queue_target_ctx_t));
	if (!qt)
		return VDP_STATUS_RESOURCES;

	qt->drawable = drawable;
	qt->fd = open("/dev/disp", O_RDWR);
	if (qt->fd == -1)
	{
		free(qt);
		return VDP_STATUS_ERROR;
	}

	int tmp = SUNXI_DISP_VERSION;
	if (ioctl(qt->fd, DISP_CMD_VERSION, &tmp) < 0)
	{
		close(qt->fd);
		free(qt);
		return VDP_STATUS_ERROR;
	}

	uint32_t args[4] = { 0, DISP_LAYER_WORK_MODE_SCALER, 0, 0 };
	qt->layer = ioctl(qt->fd, DISP_CMD_LAYER_REQUEST, args);
	if (qt->layer == 0)
		goto out_layer;

	args[1] = DISP_LAYER_WORK_MODE_NORMAL;
	qt->layer_top = ioctl(qt->fd, DISP_CMD_LAYER_REQUEST, args);
	if (qt->layer_top == 0)
		goto out_layer_top;

	XSetWindowBackground(dev->display, drawable, 0x000102);

	int handle = handle_create(qt);
	if (handle == -1)
		goto out_handle_create;

	*target = handle;
	return VDP_STATUS_OK;

out_handle_create:
	args[1] = qt->layer_top;
	ioctl(qt->fd, DISP_CMD_LAYER_RELEASE, args);
out_layer_top:
	args[1] = qt->layer;
	ioctl(qt->fd, DISP_CMD_LAYER_RELEASE, args);
out_layer:
	close(qt->fd);
	free(qt);
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

	args[1] = qt->layer_top;
	ioctl(qt->fd, DISP_CMD_LAYER_CLOSE, args);
	ioctl(qt->fd, DISP_CMD_LAYER_RELEASE, args);

	close(qt->fd);

	handle_destroy(presentation_queue_target);
	free(qt);

	return VDP_STATUS_OK;
}

VdpStatus vdp_presentation_queue_create(VdpDevice device, VdpPresentationQueueTarget presentation_queue_target, VdpPresentationQueue *presentation_queue)
{
	if (!presentation_queue)
		return VDP_STATUS_INVALID_POINTER;

	device_ctx_t *dev = handle_get(device);
	if (!dev)
		return VDP_STATUS_INVALID_HANDLE;

	queue_target_ctx_t *qt = handle_get(presentation_queue_target);
	if (!qt)
		return VDP_STATUS_INVALID_HANDLE;

	queue_ctx_t *q = calloc(1, sizeof(queue_ctx_t));
	if (!q)
		return VDP_STATUS_RESOURCES;

	q->target = qt;
	q->device = dev;

	int handle = handle_create(q);
	if (handle == -1)
	{
		free(q);
		return VDP_STATUS_RESOURCES;
	}

	*presentation_queue = handle;
	return VDP_STATUS_OK;
}

VdpStatus vdp_presentation_queue_destroy(VdpPresentationQueue presentation_queue)
{
	queue_ctx_t *q = handle_get(presentation_queue);
	if (!q)
		return VDP_STATUS_INVALID_HANDLE;

	handle_destroy(presentation_queue);
	free(q);

	return VDP_STATUS_OK;
}

VdpStatus vdp_presentation_queue_set_background_color(VdpPresentationQueue presentation_queue, VdpColor *const background_color)
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

VdpStatus vdp_presentation_queue_get_background_color(VdpPresentationQueue presentation_queue, VdpColor *const background_color)
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

VdpStatus vdp_presentation_queue_get_time(VdpPresentationQueue presentation_queue, VdpTime *current_time)
{
	queue_ctx_t *q = handle_get(presentation_queue);
	if (!q)
		return VDP_STATUS_INVALID_HANDLE;

	*current_time = get_time();
	return VDP_STATUS_OK;
}

VdpStatus vdp_presentation_queue_display(VdpPresentationQueue presentation_queue, VdpOutputSurface surface, uint32_t clip_width, uint32_t clip_height, VdpTime earliest_presentation_time)
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
		layer_info.pipe = 0;
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
		layer_info.fb.addr[0] = ve_virt2phys(os->vs->data) + 0x40000000;
		layer_info.fb.addr[1] = ve_virt2phys(os->vs->data + os->vs->plane_size) + 0x40000000;
		layer_info.fb.addr[2] = ve_virt2phys(os->vs->data + os->vs->plane_size + os->vs->plane_size / 4) + 0x40000000;

		layer_info.fb.cs_mode = DISP_BT601;
		layer_info.fb.size.width = os->vs->width;
		layer_info.fb.size.height = os->vs->height;
		layer_info.src_win.x = 0;
		layer_info.src_win.y = 0;
		layer_info.src_win.width = os->vs->width;
		layer_info.src_win.height = os->vs->height;
		layer_info.scn_win.x = x + os->video_x;
		layer_info.scn_win.y = y + os->video_y;
		layer_info.scn_win.width = os->video_width;
		layer_info.scn_win.height = os->video_height;

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

		ioctl(q->target->fd, DISP_CMD_LAYER_TOP, args);
		ioctl(q->target->fd, DISP_CMD_LAYER_OPEN, args);
	}
	else
	{
		uint32_t args[4] = { 0, q->target->layer, 0, 0 };
		ioctl(q->target->fd, DISP_CMD_LAYER_CLOSE, args);
	}

	// TOP layer
	__disp_layer_info_t layer_info;
	memset(&layer_info, 0, sizeof(layer_info));
	layer_info.pipe = 1;
	layer_info.mode = DISP_LAYER_WORK_MODE_NORMAL;
	layer_info.fb.mode = DISP_MOD_INTERLEAVED;
	layer_info.fb.format = DISP_FORMAT_ARGB8888;
	layer_info.fb.seq = DISP_SEQ_ARGB;
	switch (os->rgba_format)
	{
	case VDP_RGBA_FORMAT_B8G8R8A8:
		layer_info.fb.br_swap = 1;
		break;
	case VDP_RGBA_FORMAT_R8G8B8A8:
	default:
		layer_info.fb.br_swap = 0;
		break;
	}
	layer_info.fb.addr[0] = ve_virt2phys(os->data) + 0x40000000;
	layer_info.fb.cs_mode = DISP_BT601;
	layer_info.fb.size.width = os->width;
	layer_info.fb.size.height = os->height;
	layer_info.src_win.x = 0;
	layer_info.src_win.y = 0;
	layer_info.src_win.width = os->width;
	layer_info.src_win.height = os->height;
	layer_info.scn_win.x = x;
	layer_info.scn_win.y = y;
	layer_info.scn_win.width = clip_width;
	layer_info.scn_win.height = clip_height;

	uint32_t args[4] = { 0, q->target->layer_top, (unsigned long)(&layer_info), 0 };
	ioctl(q->target->fd, DISP_CMD_LAYER_SET_PARA, args);

	ioctl(q->target->fd, DISP_CMD_LAYER_TOP, args);
	ioctl(q->target->fd, DISP_CMD_LAYER_OPEN, args);

	return VDP_STATUS_OK;
}

VdpStatus vdp_presentation_queue_block_until_surface_idle(VdpPresentationQueue presentation_queue, VdpOutputSurface surface, VdpTime *first_presentation_time)
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

VdpStatus vdp_presentation_queue_query_surface_status(VdpPresentationQueue presentation_queue, VdpOutputSurface surface, VdpPresentationQueueStatus *status, VdpTime *first_presentation_time)
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
