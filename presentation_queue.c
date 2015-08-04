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
#include <inttypes.h>
#include <linux/fb.h>
#include <pthread.h>
#include <math.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include "eventnames.h"
#include "queue.h"
#include "sunxi_disp_ioctl.h"
#include "ve.h"
#include "rgba.h"

static pthread_t presentation_thread_id;
static QUEUE *Queue;

typedef struct task
{
	VdpTime			when;
	uint32_t		clip_width;
	uint32_t		clip_height;
	output_surface_ctx_t	*surface;
	queue_ctx_t		*queue;
	uint32_t		wipeout;
} task_t;

VdpTime get_vdp_time(void)
{
	struct timespec tp;

	if (clock_gettime(CLOCK_MONOTONIC, &tp) == -1)
		return 0;

	return (uint64_t)tp.tv_sec * 1000000000ULL + (uint64_t)tp.tv_nsec;
}

static void cleanup_presentation_queue_target(void *ptr, void *meta)
{
	queue_target_ctx_t *target = ptr;

	VDPAU_LOG(LINFO, "Destroying target");

	uint32_t args[4] = { 0, target->layer, 0, 0 };

	if (target->layer)
	{
		ioctl(target->fd, DISP_CMD_VIDEO_STOP, args);
		ioctl(target->fd, DISP_CMD_LAYER_CLOSE, args);
		ioctl(target->fd, DISP_CMD_LAYER_RELEASE, args);
	}

	if (target->layer_top)
	{
		args[1] = target->layer_top;
		ioctl(target->fd, DISP_CMD_LAYER_CLOSE, args);
		ioctl(target->fd, DISP_CMD_LAYER_RELEASE, args);
	}

	if (target->fd > 0)
		close(target->fd);
}

static VdpStatus wait_for_vsync(device_ctx_t *dev)
{
	/* do the VSync */
	if ((dev->flags & DEVICE_FLAG_VSYNC) && ioctl(dev->fb_fd, FBIO_WAITFORVSYNC, 0))
		return VDP_STATUS_ERROR;

	return VDP_STATUS_OK;
}

static int rect_changed(VdpRect rect1, VdpRect rect2)
{
	if ((rect1.x0 != rect2.x0) ||
	    (rect1.x1 != rect2.x1) ||
	    (rect1.y0 != rect2.y0) ||
	    (rect1.y1 != rect2.y1))
		return 1;

	return 0;
}

VdpStatus vdp_presentation_queue_display(VdpPresentationQueue presentation_queue,
                                         VdpOutputSurface surface,
                                         uint32_t clip_width,
                                         uint32_t clip_height,
                                         VdpTime earliest_presentation_time)
{
	smart queue_ctx_t *q = handle_get(presentation_queue);
	if (!q)
		return VDP_STATUS_INVALID_HANDLE;

	smart output_surface_ctx_t *os = handle_get(surface);
	if (!os)
		return VDP_STATUS_INVALID_HANDLE;

	task_t *task = (task_t *)calloc(1, sizeof(task_t));
	task->when = earliest_presentation_time;
	task->clip_width = clip_width;
	task->clip_height = clip_height;
	task->surface = sref(os);
	task->queue = sref(q);
	task->wipeout = 0;
	os->first_presentation_time = 0;
	os->status = VDP_PRESENTATION_QUEUE_STATUS_QUEUED;

	if(q_push_tail(Queue, task))
	{
		VDPAU_LOG(LWARN, "Error inserting task");
		sfree(task->surface);
		sfree(task->queue);
		free(task);
	}

	return VDP_STATUS_OK;
}

static VdpStatus do_presentation_queue_display(task_t *task, int restart)
{
	queue_ctx_t *q = task->queue;
	output_surface_ctx_t *os = task->surface;

	uint32_t clip_width = task->clip_width;
	uint32_t clip_height = task->clip_height;

	/*
	 * Check for XEvents like position and dimension changes,
	 * unmapping and mapping of the window
	 * FIXME: not correct position if no surface is in queue
	 */
	int i = 0;

	VDPAU_LOG(LALL, "QueueLength: %d", XEventsQueued(q->device->display, QueuedAlready));

	while (XPending(q->device->display) && i++<20)
	{
		XEvent ev;
		XNextEvent(q->device->display, &ev);


		VDPAU_LOG(LDBG, "Received the following XEvent: %s", event_names[ev.type]);

		switch(ev.type) {
		/*
		 * Window was unmapped.
		 * This closes both layers.
		 */
		case UnmapNotify:
			q->target->drawable_change = 0;
			q->target->drawable_unmap = 1;
			q->target->start_flag = 0;
			VDPAU_LOG(LINFO, "Processing UnmapNotify (QueueLength: %d)",  XEventsQueued(q->device->display, QueuedAlready));
			break;
		/*
		 * Window was mapped.
		 * This restarts the displaying routines without extra resizing.
		 */
		case MapNotify:
			q->target->drawable_change = 0;
			q->target->drawable_unmap = 0;
			q->target->start_flag = 1;
			os->rgba.flags |= RGBA_FLAG_CHANGED;
			VDPAU_LOG(LINFO, "Processing MapNotify (QueueLength: %d)",  XEventsQueued(q->device->display, QueuedAlready));
			break;
		/*
		 * Window dimension or position has changed.
		 * Reset x, y, width and height without restarting the whole displaying routines.
		 */
		case ConfigureNotify:
			if (ev.xconfigure.x != q->target->drawable_x
					|| ev.xconfigure.y != q->target->drawable_y
					|| ev.xconfigure.width != q->target->drawable_width
					|| ev.xconfigure.height != q->target->drawable_height)
			{
				q->target->drawable_x = ev.xconfigure.x;
				q->target->drawable_y = ev.xconfigure.y;
				q->target->drawable_width = ev.xconfigure.width;
				q->target->drawable_height = ev.xconfigure.height;
				q->target->drawable_change = 1;
				os->rgba.flags |= RGBA_FLAG_CHANGED;
			}
			VDPAU_LOG(LINFO, "Processing ConfigureNotify (QueueLength: %d)",  XEventsQueued(q->device->display, QueuedAlready));
			break;
		default:
			VDPAU_LOG(LINFO, "Skipping XEvent (QueueLength: %d)",  XEventsQueued(q->device->display, QueuedAlready));
			break;
		}
	}

	if (q->target->drawable_unmap) /* Window was unmapped: Close both layers */
	{
		if (q->target->drawable_unmap == 1) /* Window was or is already unmapped: Close both layers */
		{
			uint32_t args[4] = { 0, q->target->layer, 0, 0 };
			ioctl(q->target->fd, DISP_CMD_LAYER_CLOSE, args);
			if (q->device->flags & DEVICE_FLAG_OSD)
			{
				args[1] = q->target->layer_top;
				ioctl(q->target->fd, DISP_CMD_LAYER_CLOSE, args);
			}
			q->target->drawable_unmap = 2;
		}
		goto noosd;
	}

	if (q->target->drawable_change)
	{
		/* Get new window offset */
		Window dummy;
		XTranslateCoordinates(q->device->display, q->target->drawable, RootWindow(q->device->display, q->device->screen),
		      0, 0, &q->target->x, &q->target->y, &dummy);
		XClearWindow(q->device->display, q->target->drawable);

		uint32_t args[4] = { 0, q->target->layer, 0, 0 };
		__disp_rect_t scn_win, src_win;

		/* Get scn window dimension and position */
		scn_win.x = q->target->x + os->video_dst_rect.x0;
		scn_win.y = q->target->y + os->video_dst_rect.y0;
		scn_win.width = os->video_dst_rect.x1 - os->video_dst_rect.x0;
		scn_win.height = os->video_dst_rect.y1 - os->video_dst_rect.y0;

		/* Get src window dimension and position */
		src_win.x = os->video_src_rect.x0;
		src_win.y = os->video_src_rect.y0;
		src_win.width = os->video_src_rect.x1 - os->video_src_rect.x0;
		src_win.height = os->video_src_rect.y1 - os->video_src_rect.y0;

		/* Do the y cutoff (due to a bug in sunxi disp driver) */
		if (scn_win.y < 0)
		{
			int cutoff = -scn_win.y;
			src_win.y += cutoff;
			src_win.height -= cutoff;
			scn_win.y = 0;
			scn_win.height -= cutoff;
		}

		/* Reset window dimension and position */
		args[2] = (unsigned long)(&scn_win);
		ioctl(q->target->fd, DISP_CMD_LAYER_SET_SCN_WINDOW, args);
		args[2] = (unsigned long)(&src_win);
		ioctl(q->target->fd, DISP_CMD_LAYER_SET_SRC_WINDOW, args);

		q->target->drawable_change = 0;
	}

	/*
	 * Display the VIDEO layer
	 */
	if (os->vs)
	{
		static int last_id;
		uint32_t args[4] = { 0, q->target->layer, 0, 0 };

		if (os->vs->start_flag == 1 || q->target->start_flag == 1 || restart == 1)
		{
			last_id = -1; /* Reset the video.id */

			__disp_layer_info_t layer_info;
			memset(&layer_info, 0, sizeof(layer_info));

			args[2] = (unsigned long)(&layer_info);
			ioctl(q->target->fd, DISP_CMD_LAYER_GET_PARA, args);

			layer_info.pipe = (q->device->flags & DEVICE_FLAG_OSD) ? 0 : 1;
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
			if (os->vs->height < 720)
				layer_info.fb.cs_mode = DISP_BT601;
			else
				layer_info.fb.cs_mode = DISP_BT709;
			layer_info.fb.size.width = os->vs->width;
			layer_info.fb.size.height = os->vs->height;
			layer_info.src_win.x = os->video_src_rect.x0;
			layer_info.src_win.y = os->video_src_rect.y0;
			layer_info.src_win.width = os->video_src_rect.x1 - os->video_src_rect.x0;
			layer_info.src_win.height = os->video_src_rect.y1 - os->video_src_rect.y0;
			layer_info.scn_win.x = q->target->x + os->video_dst_rect.x0;
			layer_info.scn_win.y = q->target->y + os->video_dst_rect.y0;
			layer_info.scn_win.width = os->video_dst_rect.x1 - os->video_dst_rect.x0;
			layer_info.scn_win.height = os->video_dst_rect.y1 - os->video_dst_rect.y0;
			layer_info.ck_enable = (q->device->flags & DEVICE_FLAG_OSD) ? 0 : 1;

			if (layer_info.scn_win.y < 0)
			{
				int cutoff = -(layer_info.scn_win.y);
				layer_info.src_win.y += cutoff;
				layer_info.src_win.height -= cutoff;
				layer_info.scn_win.y = 0;
				layer_info.scn_win.height -= cutoff;
			}

			layer_info.fb.addr[0] = ve_virt2phys(os->yuv->data) + DRAM_OFFSET;
			layer_info.fb.addr[1] = ve_virt2phys(os->yuv->data + os->vs->luma_size) + DRAM_OFFSET;
			layer_info.fb.addr[2] = ve_virt2phys(os->yuv->data + os->vs->luma_size + os->vs->luma_size / 4) + DRAM_OFFSET;

			args[2] = (unsigned long)(&layer_info);
			ioctl(q->target->fd, DISP_CMD_LAYER_SET_PARA, args);

			args[2] = 0;
			ioctl(q->target->fd, DISP_CMD_LAYER_OPEN, args);
			ioctl(q->target->fd, DISP_CMD_VIDEO_START, args);

			/* Initial run is done, only set video.addr[] in the next runs */
			os->vs->start_flag = 0;
			q->target->start_flag = 0;
		}
		else
		{
			__disp_video_fb_t video;
			memset(&video, 0, sizeof(__disp_video_fb_t));
			video.id = last_id + 1;
			video.addr[0] = ve_virt2phys(os->yuv->data) + DRAM_OFFSET;
			video.addr[1] = ve_virt2phys(os->yuv->data + os->vs->luma_size) + DRAM_OFFSET;
			video.addr[2] = ve_virt2phys(os->yuv->data + os->vs->luma_size + os->vs->luma_size / 4) + DRAM_OFFSET;

			if (q->device->flags & DEVICE_FLAG_DEINT)
			{
				video.interlace = os->video_deinterlace;
				video.top_field_first = os->video_field ? 0 : 1;
			}

			args[2] = (unsigned long)(&video);
			int tmp, i = 0;
			while ((tmp = ioctl(q->target->fd, DISP_CMD_VIDEO_GET_FRAME_ID, args)) != last_id)
			{
				if (tmp == -1)
					break;
				VDPAU_LOG(LINFO, "Waiting for frame id ... tmp=%d, last_id=%d", tmp, last_id);

				usleep(1000);
				if (i++ > 10)
				{
					VDPAU_LOG(LWARN, "Waiting for frame id failed");
					break;
				}
			}

			ioctl(q->target->fd, DISP_CMD_VIDEO_SET_FB, args);
			last_id++;
		}

		if (os->bg_change)
		{
			__disp_color_t background;
			memset(&background, 0, sizeof(__disp_color_t));

			/* range is 0~255 */
			background.red = os->vs->background.red * 255.0;
			background.green = os->vs->background.green * 255.0;
			background.blue = os->vs->background.blue * 255.0;
			/* alpha isn't used in display sun7i driver according to user manual */
			background.alpha = os->vs->background.alpha * 255.0;

			args[2] = (unsigned long)(&background);
			ioctl(q->target->fd, DISP_CMD_SET_BKCOLOR, args);

			VDPAU_LOG(LINFO, ">red: %d, green: %d, blue: %d, alpha: %d",
			          background.red, background.green,
			          background.blue, background.alpha);

			os->bg_change = 0;
		}

		/*
		 * Note: might be more reliable (but slower and problematic when there
		 * are driver issues and the GET functions return wrong values) to query the
		 * old values instead of relying on our internal csc_change.
		 * Since the driver calculates a matrix out of these values after each
		 * set doing this unconditionally is costly.
		 */
		if (os->csc_change)
		{
			uint32_t b, c, s, h;

			ioctl(q->target->fd, DISP_CMD_LAYER_ENHANCE_OFF, args);

			/* scale VDPAU: -1.0 ~ 1.0 to SUNXI: 0 ~ 100 */
			b = args[2] = ((os->brightness + 1.0) * 50.0) + 0.5;
			ioctl(q->target->fd, DISP_CMD_LAYER_SET_BRIGHT, args);

			/* scale VDPAU: 0.0 ~ 10.0 to SUNXI: 0 ~ 100 */
			if (os->contrast <= 1.0)
				c = args[2] = (os->contrast * 50.0) + 0.5;
			else
				c = args[2] = (50.0 + (os->contrast - 1.0) * 50.0 / 9.0) + 0.5;
			ioctl(q->target->fd, DISP_CMD_LAYER_SET_CONTRAST, args);

			/* scale VDPAU: 0.0 ~ 10.0 to SUNXI: 0 ~ 100 */
			if (os->saturation <= 1.0)
				s = args[2] = (os->saturation * 50.0) + 0.5;
			else
				s = args[2] = (50.0 + (os->saturation - 1.0) * 50.0 / 9.0) + 0.5;
			ioctl(q->target->fd, DISP_CMD_LAYER_SET_SATURATION, args);

			/* scale VDPAU: -PI ~ PI   to SUNXI: 0 ~ 100 */
			h = args[2] = (((os->hue / M_PI) + 1.0) * 50.0) + 0.5;
			ioctl(q->target->fd, DISP_CMD_LAYER_SET_HUE, args);

			ioctl(q->target->fd, DISP_CMD_LAYER_ENHANCE_ON, args);

			VDPAU_LOG(LINFO, "Presentation queue csc change");
			VDPAU_LOG(LINFO, "display driver -> bright: %d, contrast: %d, saturation: %d, hue: %d", b, c, s, h);
			os->csc_change = 0;
		}
	}
	else /* No video surface present. Close the layer. */
	{
		uint32_t args[4] = { 0, q->target->layer, 0, 0 };
		ioctl(q->target->fd, DISP_CMD_LAYER_CLOSE, args);
	}

	/* OSD is disabled, so skip OSD displaying. */
	if (!(q->device->flags & DEVICE_FLAG_OSD))
		goto noosd;

	/*
	 * Display the OSD layer
	 */
	if (os->rgba.flags & RGBA_FLAG_NEEDS_CLEAR)
		rgba_clear(&os->rgba);

	if (os->rgba.flags & RGBA_FLAG_DIRTY) /* rgba surface is dirty */
	{
		rgba_flush(&os->rgba);
		uint32_t args[4] = { 0, q->target->layer_top, 0, 0 };

		if (os->rgba.flags & RGBA_FLAG_CHANGED) /* we have some changed bits on it */
		{
			__disp_layer_info_t layer_info;
			memset(&layer_info, 0, sizeof(layer_info));

			args[2] = (unsigned long)(&layer_info);
			ioctl(q->target->fd, DISP_CMD_LAYER_GET_PARA, args);

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
			layer_info.fb.cs_mode = DISP_BT601;
			layer_info.fb.size.width = os->rgba.width;
			layer_info.fb.size.height = os->rgba.height;
			layer_info.src_win.x = 0;
			layer_info.src_win.y = 0;
			layer_info.src_win.width = os->rgba.width;
			layer_info.src_win.height = os->rgba.height;
			layer_info.scn_win.x = q->target->x;
			layer_info.scn_win.y = q->target->y;
			layer_info.scn_win.width = clip_width ? clip_width : os->rgba.width;
			layer_info.scn_win.height = clip_height ? clip_height : os->rgba.height;
			layer_info.fb.addr[0] = ve_virt2phys(os->rgba.data) + DRAM_OFFSET;

			args[2] = (unsigned long)(&layer_info);
			ioctl(q->target->fd, DISP_CMD_LAYER_SET_PARA, args);

			if (!(os->rgba.flags & RGBA_FLAG_LAYEROPEN))
			{
				args[2] = 0;
				ioctl(q->target->fd, DISP_CMD_LAYER_OPEN, args);
				os->rgba.flags |= RGBA_FLAG_LAYEROPEN;
			}

			os->rgba.flags &= ~RGBA_FLAG_CHANGED;
		}
		else
		{
			__disp_rect_t scn_win, src_win;
			src_win.x = os->rgba.dirty.x0;
			src_win.y = os->rgba.dirty.y0;
			src_win.width = os->rgba.dirty.x1 - os->rgba.dirty.x0;
			src_win.height = os->rgba.dirty.y1 - os->rgba.dirty.y0;
			scn_win.x = q->target->x + os->rgba.dirty.x0;
			scn_win.y = q->target->y + os->rgba.dirty.y0;
			scn_win.width = min_nz(clip_width, os->rgba.dirty.x1) - os->rgba.dirty.x0;
			scn_win.height = min_nz(clip_height, os->rgba.dirty.y1) - os->rgba.dirty.y0;

			args[2] = (unsigned long)(&scn_win);
			ioctl(q->target->fd, DISP_CMD_LAYER_SET_SCN_WINDOW, args);
			args[2] = (unsigned long)(&src_win);
			ioctl(q->target->fd, DISP_CMD_LAYER_SET_SRC_WINDOW, args);

			__disp_fb_t fb_info;
			memset(&fb_info, 0, sizeof(__disp_fb_t));
			args[2] = (unsigned long)(&fb_info);
			ioctl(q->target->fd, DISP_CMD_LAYER_GET_FB, args);

			fb_info.addr[0] = ve_virt2phys(os->rgba.data) + DRAM_OFFSET;
			args[2] = (unsigned long)(&fb_info);
			ioctl(q->target->fd, DISP_CMD_LAYER_SET_FB, args);
		}
	}
	else
	{
		uint32_t args[4] = { 0, q->target->layer_top, 0, 0 };
		ioctl(q->target->fd, DISP_CMD_LAYER_CLOSE, args);
		os->rgba.flags &= ~RGBA_FLAG_LAYEROPEN;
	}

noosd:
	return VDP_STATUS_OK;
}

static void *presentation_thread(void *param)
{
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

	smart device_ctx_t *dev = (device_ctx_t *)param;

	output_surface_ctx_t *os_prev = NULL;
	output_surface_ctx_t *os_cur = NULL;

	VdpTime timein, timebeforevsync, timeaftervsync, oldvsync;
	timeaftervsync = 0;
	oldvsync = 0;

	int restart = 1;

	while (!(dev->flags & DEVICE_FLAG_EXIT)) {
		if(Queue && !q_isEmpty(Queue)) /* We have a task in the queue to display */
		{
			task_t *task;
			if (!q_pop_head(Queue, (void *)&task)) /* remove it from Queue */
			{
				timein = get_vdp_time();

				if (task->wipeout) /* Got the wipeout task */
				{
					sfree(os_cur);
					os_cur = NULL;
					sfree(os_prev);
					os_prev = NULL;
					dev->flags |= DEVICE_FLAG_EXIT;
					VDPAU_LOG(LDBG, "Wipeout task received, ending presentation thread ...");
				}
				else
				{
					/* Rotate the surfaces (previous becomes current) */
					sfree(os_prev);
					os_prev = os_cur;

					os_cur = sref(task->surface);
					if (!os_cur)
						VDPAU_LOG(LERR, "Error getting surface");

					/* Trigger display init, if the video rect size has changed */
					if (os_prev && os_cur)
						if ((rect_changed(os_cur->video_dst_rect, os_prev->video_dst_rect)) ||
						    (rect_changed(os_cur->video_src_rect, os_prev->video_src_rect)))
						{
							VDPAU_LOG(LINFO, "Video rect changed, init triggered.");
							restart = 1;
						}

					/*
					 * Main part: display the task, meaning:
					 * push the frame to the address and then wait for the vsync
					 */
					do_presentation_queue_display(task, restart);

					timebeforevsync = get_vdp_time();
					/* do the VSync, if enabled */
					if (wait_for_vsync(dev))
						VDPAU_LOG(LWARN, "VSync failed");
					timeaftervsync = get_vdp_time();

					/* Set the status flags after the vsync was done */

					/* This is the actually displayed surface */
					os_cur->first_presentation_time = timeaftervsync;
					os_cur->status = VDP_PRESENTATION_QUEUE_STATUS_VISIBLE;

					/* This is the previously displayed surface */
					if (os_prev)
						os_prev->status = VDP_PRESENTATION_QUEUE_STATUS_IDLE;

					/*
					 * Check, if the time difference between two VSync is < 10ms, so something must be wrong
					 * this causes missed frames messages in softhddevice 
					 */
					if ((((timeaftervsync - timein) / 1000) < 10000) && oldvsync)
						VDPAU_TIME(LPQ2, "PQ time difference: in>vsync %" PRIu64 ", vsync>out %" PRIu64 ", in>out %" PRIu64 ", vsync>vsync%" PRIu64 " <- TOO SHORT!",
						          ((timebeforevsync - timein) / 1000), ((timeaftervsync - timebeforevsync) / 1000), ((timeaftervsync - timein) / 1000), ((timeaftervsync - oldvsync) / 1000));
					else if (oldvsync)
						VDPAU_TIME(LPQ2, "PQ time difference: in>vsync %" PRIu64 ", vsync>out %" PRIu64 ", in>out %" PRIu64 ", vsync>vsync%" PRIu64 "",
						          ((timebeforevsync - timein) / 1000), ((timeaftervsync - timebeforevsync) / 1000), ((timeaftervsync - timein) / 1000), ((timeaftervsync - oldvsync) / 1000));

					oldvsync = timeaftervsync;

					restart = 0;
				}
				sfree(task->surface);
				sfree(task->queue);
				free(task);
			}
			else /* This should never happen! */
				VDPAU_LOG(LERR, "Error getting task");
		}
		/* We have no queue or surface in the queue, so simply wait some period of time (find a suitable value!)
		 * Otherwise, while is doing a race, that it can't win.
		 */
		else
			usleep(1000);
	}
	return NULL;
}

VdpStatus vdp_presentation_queue_target_create_x11(VdpDevice device,
                                                   Drawable drawable,
                                                   VdpPresentationQueueTarget *target)
{
	if (!target || !drawable)
		return VDP_STATUS_INVALID_POINTER;

	smart device_ctx_t *dev = handle_get(device);
	if (!dev)
		return VDP_STATUS_INVALID_HANDLE;

	smart queue_target_ctx_t *qt = handle_alloc(sizeof(*qt), cleanup_presentation_queue_target);
	if (!qt)
		return VDP_STATUS_RESOURCES;

	qt->fd = open("/dev/disp", O_RDWR);
	if (qt->fd == -1)
		return VDP_STATUS_ERROR;

	int tmp = SUNXI_DISP_VERSION;
	if (ioctl(qt->fd, DISP_CMD_VERSION, &tmp) < 0)
		return VDP_STATUS_ERROR;

	uint32_t args[4] = { 0, DISP_LAYER_WORK_MODE_SCALER, 0, 0 };
	qt->layer = ioctl(qt->fd, DISP_CMD_LAYER_REQUEST, args);
	if (qt->layer == 0)
		return VDP_STATUS_RESOURCES;

	args[1] = qt->layer;
	ioctl(qt->fd, (dev->flags & DEVICE_FLAG_OSD) ? DISP_CMD_LAYER_TOP : DISP_CMD_LAYER_BOTTOM, args);

	if (dev->flags & DEVICE_FLAG_OSD)
	{
		args[1] = DISP_LAYER_WORK_MODE_NORMAL;
		qt->layer_top = ioctl(qt->fd, DISP_CMD_LAYER_REQUEST, args);
		if (qt->layer_top == 0)
			return VDP_STATUS_RESOURCES;

		args[1] = qt->layer_top;
		ioctl(qt->fd, DISP_CMD_LAYER_TOP, args);
	}
	else
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

	VDPAU_LOG(LINFO, "Creating target");

	qt->start_flag = 1;
	qt->drawable_change = 0;
	qt->drawable_unmap = 0;
	qt->drawable_x = 0;
	qt->drawable_y = 0;
	qt->drawable_width = 0;
	qt->drawable_height = 0;

	qt->drawable = drawable;

	/* Register drawable and parent window for events */
	Window root, parent, *children;
	uint32_t nchildren;
	XQueryTree(dev->display, drawable, &root, &parent, &children, &nchildren);
	XSelectInput(dev->display, drawable, StructureNotifyMask);
	XSelectInput(dev->display, parent, StructureNotifyMask);

	/* get current window position */
	Window dummy;
	XTranslateCoordinates(dev->display, qt->drawable, RootWindow(dev->display, dev->screen), 0, 0, &qt->x, &qt->y, &dummy);
	XSetWindowBackground(dev->display, drawable, 0x000102);
	XClearWindow(dev->display, drawable);

	return handle_create(target, qt);
}

static void cleanup_presentation_queue(void *ptr, void *meta)
{
	queue_ctx_t *queue = ptr;

	sfree(queue->target);
	sfree(queue->device);
}

VdpStatus vdp_presentation_queue_create(VdpDevice device,
                                        VdpPresentationQueueTarget presentation_queue_target,
                                        VdpPresentationQueue *presentation_queue)
{
	if (!presentation_queue)
		return VDP_STATUS_INVALID_POINTER;

	smart device_ctx_t *dev = handle_get(device);
	if (!dev)
		return VDP_STATUS_INVALID_HANDLE;

	smart queue_target_ctx_t *qt = handle_get(presentation_queue_target);
	if (!qt)
		return VDP_STATUS_INVALID_HANDLE;

	smart queue_ctx_t *q = handle_alloc(sizeof(*q), cleanup_presentation_queue);
	if (!q)
		return VDP_STATUS_RESOURCES;

	VDPAU_LOG(LINFO, "Creating queue");

	q->target = sref(qt);
	q->device = sref(dev);

	/* initialize queue and launch presentation thread */
	if (!Queue)
		Queue = q_queue_init();

	if (!(dev->flags & DEVICE_FLAG_THREAD)) {
		dev->flags &= ~DEVICE_FLAG_EXIT;
		pthread_create(&presentation_thread_id, NULL, presentation_thread, sref(dev));
		dev->flags |= DEVICE_FLAG_THREAD;
	}

	return handle_create(presentation_queue, q);
}

VdpStatus vdp_presentation_queue_destroy(VdpPresentationQueue presentation_queue)
{
	smart queue_ctx_t *q = handle_get(presentation_queue);
	if (!q)
		return VDP_STATUS_INVALID_HANDLE;

	VDPAU_LOG(LINFO, "Destroying queue");

	task_t *task = (task_t *)calloc(1, sizeof(task_t));
	task->wipeout = 1;

	if(!q_push_tail(Queue, task))
		VDPAU_LOG(LDBG, "Inserting wipe task");
	else
	{
		VDPAU_LOG(LDBG, "Error inserting wipe task");
		free(task);
	}

	pthread_join(presentation_thread_id, NULL);
	q->device->flags &= ~DEVICE_FLAG_THREAD;

	q_queue_free(Queue, 0);
	Queue = NULL;

	return handle_destroy(presentation_queue);
}

VdpStatus vdp_presentation_queue_set_background_color(VdpPresentationQueue presentation_queue,
                                                      VdpColor *const background_color)
{
	if (!background_color)
		return VDP_STATUS_INVALID_POINTER;

	smart queue_ctx_t *q = handle_get(presentation_queue);
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

	smart queue_ctx_t *q = handle_get(presentation_queue);
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
	smart queue_ctx_t *q = handle_get(presentation_queue);
	if (!q)
		return VDP_STATUS_INVALID_HANDLE;

	*current_time = get_vdp_time();
	return VDP_STATUS_OK;
}

VdpStatus vdp_presentation_queue_block_until_surface_idle(VdpPresentationQueue presentation_queue,
                                                          VdpOutputSurface surface,
                                                          VdpTime *first_presentation_time)
{
	smart queue_ctx_t *q = handle_get(presentation_queue);
	if (!q)
		return VDP_STATUS_INVALID_HANDLE;

	smart output_surface_ctx_t *os = handle_get(surface);
	if (!os)
		return VDP_STATUS_INVALID_HANDLE;

	while (os->status != VDP_PRESENTATION_QUEUE_STATUS_IDLE) {
		usleep(1000);
		smart output_surface_ctx_t *os = handle_get(surface);
		if (!os)
			return VDP_STATUS_INVALID_HANDLE;
	}

	*first_presentation_time = os->first_presentation_time;

	return VDP_STATUS_OK;
}

VdpStatus vdp_presentation_queue_query_surface_status(VdpPresentationQueue presentation_queue,
                                                      VdpOutputSurface surface,
                                                      VdpPresentationQueueStatus *status,
                                                      VdpTime *first_presentation_time)
{
	if (!status || !first_presentation_time)
		return VDP_STATUS_INVALID_POINTER;

		smart queue_ctx_t *q = handle_get(presentation_queue);
	if (!q)
		return VDP_STATUS_INVALID_HANDLE;

	smart output_surface_ctx_t *os = handle_get(surface);
	if (!os)
		return VDP_STATUS_INVALID_HANDLE;

	*status = os->status;
	*first_presentation_time = os->first_presentation_time;

	return VDP_STATUS_OK;
}
