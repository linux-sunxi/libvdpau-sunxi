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
	uint32_t		control;
} task_t;

VdpTime get_vdp_time(void)
{
	struct timespec tp;

	if (clock_gettime(CLOCK_MONOTONIC, &tp) == -1)
		return 0;

	return (uint64_t)tp.tv_sec * 1000000000ULL + (uint64_t)tp.tv_nsec;
}

/*
 * Use this function, to trigger some events in the presentation thread:
 * This pushes a task to the queue, that controls the workflow within the queue
 * in the display routine.
 * The following values are possible:
 *   flag = CONTROL_END_THREAD -> clear surfaces, end thread, clear queue
 *   flag = CONTROL_REINIT_DISPLAY -> clear surfaces, restart display engine
 *   flag = CONTROL_DISABLE_VIDEO -> disable video layer
 * return 0 if success
 */
int thread_control(uint32_t flag)
{
	VDPAU_LOG(LINFO, "Control flag received: %d", flag);
	task_t *task = (task_t *)calloc(1, sizeof(task_t));
	task->control = flag;

	if (q_push_tail(Queue, task))
	{
		VDPAU_LOG(LERR, "Error inserting control task!");
		free(task);
		return 1;
	}

	return 0;
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

#ifdef USE_KVSYNC
static uint64_t get_last_vsync(queue_target_ctx_t *qt)
{
	uint32_t args[4] = { 0, 0, 0, 0};
	uint64_t vsync;
	memset(&vsync, 0, sizeof(uint64_t));
	args[1] = (unsigned long)(&vsync);

	if (!(qt->device->flags & DEVICE_FLAG_VSYNC))
		return get_vdp_time();

	if (ioctl(qt->fd, DISP_CMD_VSYNC_GET, args) < 0)
		return -1;

	return vsync;
}

static uint32_t enable_vsync(queue_target_ctx_t *qt)
{
	uint32_t args[4] = { 0, 1, 0, 0};
	if (qt->device->flags & DEVICE_FLAG_VSYNC)
		ioctl(qt->fd, DISP_CMD_VSYNC_EVENT_EN, args);

	return 0;
}
#endif

static VdpStatus wait_for_vsync(queue_target_ctx_t *qt)
{
	/* do the VSync */
	if ((qt->device->flags & DEVICE_FLAG_VSYNC) && ioctl(qt->device->fb_fd, FBIO_WAITFORVSYNC, 0))
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

static int video_surface_changed(video_surface_ctx_t *vs1, video_surface_ctx_t *vs2)
{
	if (vs1 && vs2)
		if ((vs1->height != vs2->height) ||
		    (vs1->width != vs2->width) ||
		    (vs1->chroma_type != vs2->chroma_type) ||
		    (vs1->source_format != vs2->source_format))
			return 1;

	if ((!vs1 && vs2) || (vs1 && !vs2))
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

	static uint32_t id = 0;
	task_t *task = (task_t *)calloc(1, sizeof(task_t));
	task->when = earliest_presentation_time;
	task->clip_width = clip_width;
	task->clip_height = clip_height;
	task->surface = sref(os);
	task->queue = sref(q);
	task->control = CONTROL_NULL;
	os->first_presentation_time = 0;
	os->status = VDP_PRESENTATION_QUEUE_STATUS_QUEUED;
	os->id = id++;

	if(q_push_tail(Queue, task))
	{
		VDPAU_LOG(LWARN, "Error inserting task");
		sfree(task->surface);
		sfree(task->queue);
		free(task);
	}

#ifdef DEBUG_TIME
	os->stop = get_vdp_time();
	VDPAU_LOG(LDBG, "Pushing surface %d at %" PRIu64 ", Diff: %" PRIu64 "", os->id, os->stop, (os->stop - os->start) / 1000);
#endif
	return VDP_STATUS_OK;
}

static VdpStatus do_presentation_queue_display(task_t *task)
{
	uint32_t init_display = task->control;
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
			init_display = CONTROL_NULL;
			VDPAU_LOG(LINFO, "Processing UnmapNotify (QueueLength: %d)",  XEventsQueued(q->device->display, QueuedAlready));
			break;
		/*
		 * Window was mapped.
		 * This restarts the displaying routines without extra resizing.
		 */
		case MapNotify:
			q->target->drawable_change = 0;
			q->target->drawable_unmap = 0;
			init_display = CONTROL_REINIT_DISPLAY;
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
				init_display = CONTROL_REINIT_DISPLAY;
			}
			VDPAU_LOG(LINFO, "Processing ConfigureNotify (QueueLength: %d)",  XEventsQueued(q->device->display, QueuedAlready));
			break;
		default:
			VDPAU_LOG(LINFO, "Skipping XEvent (QueueLength: %d)",  XEventsQueued(q->device->display, QueuedAlready));
			break;
		}
	}

	if (q->target->drawable_unmap) /* Window was (1) or is (2) already unmapped */
	{
		if (q->target->drawable_unmap == 1) /* Window was unmapped: Close both layers */
		{
			uint32_t args[4] = { 0, q->target->layer, 0, 0 };
			if (ioctl(q->target->fd, DISP_CMD_LAYER_CLOSE, args))
				VDPAU_LOG(LERR, "Video layer close failed");
			else
			{
				VDPAU_LOG(LINFO, "Video layer closed");
				q->device->flags &= ~DEVICE_FLAG_VLAYEROPEN;
			}

			if (q->device->flags & DEVICE_FLAG_OSD)
			{
				args[1] = q->target->layer_top;
				if (ioctl(q->target->fd, DISP_CMD_LAYER_CLOSE, args))
					VDPAU_LOG(LERR, "OSD layer close failed");
				else
				{
					VDPAU_LOG(LINFO, "OSD layer closed");
					q->device->flags &= ~DEVICE_FLAG_RLAYEROPEN;
				}
			}
			q->target->drawable_unmap = 2;
		}
		goto skip_osd;
	}

	if (q->target->drawable_change)
	{
		/* Get new window offset */
		Window dummy;
		XTranslateCoordinates(q->device->display, q->target->drawable, RootWindow(q->device->display, q->device->screen),
		      0, 0, &q->target->x, &q->target->y, &dummy);
		XClearWindow(q->device->display, q->target->drawable);

		q->target->drawable_change = 0;
	}

	/*
	 * Display the VIDEO layer
	 */
	if (init_display == CONTROL_DISABLE_VIDEO && q->device->flags & DEVICE_FLAG_VLAYEROPEN)
	{
		uint32_t args[4] = { 0, q->target->layer, 0, 0 };
		if (ioctl(q->target->fd, DISP_CMD_LAYER_CLOSE, args))
			VDPAU_LOG(LERR, "Video layer close failed");
		else
		{
			VDPAU_LOG(LINFO, "Video Layer closed.");
			q->device->flags &= ~DEVICE_FLAG_VLAYEROPEN;
		}
		goto skip_video;
	}

	if (os->vs)
	{
		static int last_id;
		uint32_t args[4] = { 0, q->target->layer, 0, 0 };

		if (init_display == CONTROL_REINIT_DISPLAY)
		{
			last_id = -1; /* Reset the video.id */

			__disp_layer_info_t layer_info;
			memset(&layer_info, 0, sizeof(layer_info));

			args[2] = (unsigned long)(&layer_info);
			if (ioctl(q->target->fd, DISP_CMD_LAYER_GET_PARA, args))
				VDPAU_LOG(LERR, "DISP_CMD_LAYER_GET_PARA failed");

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
			if (ioctl(q->target->fd, DISP_CMD_LAYER_SET_PARA, args))
				VDPAU_LOG(LERR, "DISP_CMD_LAYER_SET_PARA failed");

			if (!(q->device->flags & DEVICE_FLAG_VLAYEROPEN))
			{
				args[2] = 0;
				if (ioctl(q->target->fd, DISP_CMD_LAYER_OPEN, args))
					VDPAU_LOG(LERR, "Video Layer open failed");
				else
				{
					VDPAU_LOG(LINFO, "Video Layer opened");
					q->device->flags |= DEVICE_FLAG_VLAYEROPEN;
				}
			}

			if (ioctl(q->target->fd, DISP_CMD_VIDEO_START, args))
				VDPAU_LOG(LERR, "DISP_CMD_VIDEO_START failed");
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

			/* Send the new frame to the framebuffer */
			args[2] = (unsigned long)(&video);
			if (ioctl(q->target->fd, DISP_CMD_VIDEO_SET_FB, args))
				VDPAU_LOG(LERR, "DISP_CMD_VIDEO_SET_FB failed");
			last_id++;

			if (!(q->device->flags & DEVICE_FLAG_VLAYEROPEN))
			{
				args[2] = 0;
				if (ioctl(q->target->fd, DISP_CMD_LAYER_OPEN, args))
					VDPAU_LOG(LERR, "Video Layer open failed");
				else
				{
					VDPAU_LOG(LINFO, "Video Layer opened");
					q->device->flags |= DEVICE_FLAG_VLAYEROPEN;
				}
			}
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

			args[1] = (unsigned long)(&background);
			args[2] = 0;
			if (ioctl(q->target->fd, DISP_CMD_SET_BKCOLOR, args))
				VDPAU_LOG(LERR, "DISP_CMD_SET_BKCOLOR failed");
			args[1] = q->target->layer;

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

			if (ioctl(q->target->fd, DISP_CMD_LAYER_ENHANCE_OFF, args))
				VDPAU_LOG(LERR, "DISP_CMD_LAYER_ENHANCE_OFF failed");

			/* scale VDPAU: -1.0 ~ 1.0 to SUNXI: 0 ~ 100 */
			b = args[2] = ((os->brightness + 1.0) * 50.0) + 0.5;
			if (ioctl(q->target->fd, DISP_CMD_LAYER_SET_BRIGHT, args))
				VDPAU_LOG(LERR, "DISP_CMD_LAYER_SET_BRIGHT failed");

			/* scale VDPAU: 0.0 ~ 10.0 to SUNXI: 0 ~ 100 */
			if (os->contrast <= 1.0)
				c = args[2] = (os->contrast * 50.0) + 0.5;
			else
				c = args[2] = (50.0 + (os->contrast - 1.0) * 50.0 / 9.0) + 0.5;
			if (ioctl(q->target->fd, DISP_CMD_LAYER_SET_CONTRAST, args))
				VDPAU_LOG(LERR, "DISP_CMD_LAYER_SET_CONTRAST failed");

			/* scale VDPAU: 0.0 ~ 10.0 to SUNXI: 0 ~ 100 */
			if (os->saturation <= 1.0)
				s = args[2] = (os->saturation * 50.0) + 0.5;
			else
				s = args[2] = (50.0 + (os->saturation - 1.0) * 50.0 / 9.0) + 0.5;
			if (ioctl(q->target->fd, DISP_CMD_LAYER_SET_SATURATION, args))
				VDPAU_LOG(LERR, "DISP_CMD_LAYER_SET_SATURATION failed");

			/* scale VDPAU: -PI ~ PI   to SUNXI: 0 ~ 100 */
			h = args[2] = (((os->hue / M_PI) + 1.0) * 50.0) + 0.5;
			if (ioctl(q->target->fd, DISP_CMD_LAYER_SET_HUE, args))
				VDPAU_LOG(LERR, "DISP_CMD_LAYER_SET_HUE failed");

			if (ioctl(q->target->fd, DISP_CMD_LAYER_ENHANCE_ON, args))
				VDPAU_LOG(LERR, "DISP_CMD_LAYER_ENHANCE_ON failed");

			VDPAU_LOG(LINFO, "Presentation queue csc change");
			VDPAU_LOG(LINFO, "display driver -> bright: %d, contrast: %d, saturation: %d, hue: %d", b, c, s, h);
			os->csc_change = 0;
		}
	}
	else /* No video surface present. Close the layer. */
	{
		if (q->device->flags & DEVICE_FLAG_VLAYEROPEN)
		{
			uint32_t args[4] = { 0, q->target->layer, 0, 0 };
			if (ioctl(q->target->fd, DISP_CMD_LAYER_CLOSE, args))
				VDPAU_LOG(LERR, "Video Layer close failed");
			else
			{
				VDPAU_LOG(LINFO, "Video Layer closed");
				q->device->flags &= ~DEVICE_FLAG_VLAYEROPEN;
			}
		}
	}

skip_video:
	/* OSD is disabled, so skip OSD displaying. */
	if (!(q->device->flags & DEVICE_FLAG_OSD))
		goto skip_osd;

	/*
	 * Display the OSD layer
	 */
	if (os->rgba.flags & RGBA_FLAG_NEEDS_CLEAR)
		rgba_clear(&os->rgba);

	if (os->rgba.flags & RGBA_FLAG_DIRTY) /* rgba surface is dirty */
	{
		rgba_flush(&os->rgba);
		uint32_t args[4] = { 0, q->target->layer_top, 0, 0 };

		__disp_layer_info_t layer_info;
		memset(&layer_info, 0, sizeof(layer_info));

		args[2] = (unsigned long)(&layer_info);
		if (ioctl(q->target->fd, DISP_CMD_LAYER_GET_PARA, args))
			VDPAU_LOG(LERR, "DISP_CMD_LAYER_GET_PARA failed");

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
		if (ioctl(q->target->fd, DISP_CMD_LAYER_SET_PARA, args))
			VDPAU_LOG(LERR, "DISP_CMD_LAYER_SET_PARA failed");

		if (!(q->device->flags & DEVICE_FLAG_RLAYEROPEN))
		{
			args[2] = 0;
			if (ioctl(q->target->fd, DISP_CMD_LAYER_OPEN, args))
				VDPAU_LOG(LERR, "OSD Layer open failed");
			else
			{
				VDPAU_LOG(LINFO, "OSD Layer opened");
				q->device->flags |= DEVICE_FLAG_RLAYEROPEN;
			}
		}
	}
	else
	{
		if (q->device->flags & DEVICE_FLAG_RLAYEROPEN)
		{
			uint32_t args[4] = { 0, q->target->layer_top, 0, 0 };
			if (ioctl(q->target->fd, DISP_CMD_LAYER_CLOSE, args))
				VDPAU_LOG(LERR, "OSD Layer close failed");
			else
			{
				VDPAU_LOG(LINFO, "OSD Layer closed");
				q->device->flags &= ~DEVICE_FLAG_RLAYEROPEN;
			}
		}
	}

skip_osd:
	return VDP_STATUS_OK;
}

/*
 * Some dumb, busy loop, that waits for a filled queue,
 * until max_surfaces is reached or timeout (us) expires.
 */
int rebuild_buffer(int max_surfaces, int interval, int timeout)
{
	int breakout = 0;
	while (q_length(Queue) < max_surfaces)
	{
		if (timeout && breakout > timeout)
			return q_length(Queue);
		usleep(interval);
		breakout += interval;
	}
	return max_surfaces;
}

static void *presentation_thread(void *param)
{
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

	smart queue_target_ctx_t *qt = (queue_target_ctx_t *)param;

	output_surface_ctx_t *os_prev = NULL;
	output_surface_ctx_t *os_cur = NULL;

	int processed;
#ifdef USE_KVSYNC
	uint64_t lastvsync = get_last_vsync(qt);
#ifdef DEBUG_TIME
	uint64_t oldvsync = lastvsync;
	int skipped = 0;
	VdpTime start, stop;
	start = 0;
	stop = 0;
#endif
#else
	VdpTime lastvsync = 0;
#ifdef DEBUG_TIME
	int skipped = 0;
	VdpTime start, stop, oldvsync;
	start = 0;
	stop = 0;
	oldvsync = 0;
#endif
#endif
	/* Initially fill the queue with MAX_SURFACES, wait for max. 2s */
	rebuild_buffer(MAX_SURFACES, 1 * 1000, 2 * 1000 * 1000);
	VDPAU_LOG(LINFO, "Queue initially filled with %d tasks", q_length(Queue));

	while (!(qt->device->flags & DEVICE_FLAG_EXIT)) {
		if (Queue)
		{
			VDPAU_LOG(LALL, "Queue length %d", q_length(Queue));
			task_t *task = NULL;
			processed = 0;

			/*
			 * We have a task in the queue to display,
			 * so start the main part of the loop.
			 * We check, if a display init is needed
			 * and then push the frame to the framebuffer.
			 */
			if (!q_isEmpty(Queue))
			{
				/* Remove task from queue */
				q_pop_head(Queue, (void *)&task);

				/* Check for a control flag */
				switch (task->control) {
					case CONTROL_REINIT_DISPLAY:
						VDPAU_LOG(LINFO, "Control task received, restarting display engine ...");
						break;
					case CONTROL_DISABLE_VIDEO:
						VDPAU_LOG(LINFO, "Control task received, disable video picture ...");
						break;
					case CONTROL_END_THREAD:
						VDPAU_LOG(LINFO, "Control task received, ending presentation thread ...");
						qt->device->flags |= DEVICE_FLAG_EXIT;
						sfree(os_cur);
						os_cur = NULL;
						sfree(os_prev);
						os_prev = NULL;
						free(task);
						goto skip_display;
					case CONTROL_NULL:
					default:
						break;
				}

				/* Rotate the surfaces (previous becomes current) */
				sfree(os_prev);
				os_prev = os_cur;
				os_cur = sref(task->surface);

				if (!os_cur)
					VDPAU_LOG(LERR, "Error getting surface");

				/* Compare current and last surface and check if display init must be triggered */
				/* Trigger display init, if the video rect size has changed */
				if (os_cur && os_prev)
					if ((rect_changed(os_cur->video_dst_rect, os_prev->video_dst_rect)) ||
					    (rect_changed(os_cur->video_src_rect, os_prev->video_src_rect)))
					{
						VDPAU_LOG(LINFO, "Video rect changed, init triggered.");
						task->control = CONTROL_REINIT_DISPLAY;
					}

				/* Trigger display init, if the video surface has changed */
				if (os_cur && os_prev)
					if ((video_surface_changed(os_prev->vs, os_cur->vs)))
					{
						VDPAU_LOG(LINFO, "Video surface changed, init triggered.");
						task->control = CONTROL_REINIT_DISPLAY;
					}

				/* Trigger display init, if the video surface is the first frame after video mixer was created */
				if (os_cur->vs && (os_cur->vs->first_frame))
				{
					VDPAU_LOG(LINFO, "Received first video surface, init triggered.");
					task->control = CONTROL_REINIT_DISPLAY;
					os_cur->vs->first_frame = 0;
				}

				/*
				 * Main part: display the task, meaning:
				 * push the frame to the address and then wait for the vsync
				 */
#ifdef DEBUG_TIME
				start = get_vdp_time();
#endif
				if (do_presentation_queue_display(task))
					VDPAU_LOG(LERR, "Something went wrong while displaying ...");
				else
					VDPAU_LOG(LDBG, "Processed surface %d", os_cur->id);
#ifdef DEBUG_TIME
				stop = get_vdp_time();
#endif

				/* Wait for the next VSync, and get the timestamp */
				if (wait_for_vsync(qt))
					VDPAU_LOG(LWARN, "VSync failed");
#ifdef USE_KVSYNC
				lastvsync = get_last_vsync(qt);
#else
				lastvsync = get_vdp_time();
#endif
				/* Set the status flags after the vsync was done */
				/* This is the actually displayed surface */
				if (os_cur)
				{
					if (os_cur->first_presentation_time == 0)
						os_cur->first_presentation_time = lastvsync;
					os_cur->status = VDP_PRESENTATION_QUEUE_STATUS_VISIBLE;
				}

				/* This is the previously displayed surface */
				if (os_prev)
				{
					if (os_prev->yuv)
						yuv_unref(os_prev->yuv);
					os_prev->yuv = NULL;
					sfree(os_prev->vs);
					os_prev->vs = NULL;

					pthread_mutex_lock(&os_prev->mutex);
					if (os_prev->status != VDP_PRESENTATION_QUEUE_STATUS_IDLE)
					{
						os_prev->status = VDP_PRESENTATION_QUEUE_STATUS_IDLE;
						pthread_cond_signal(&os_prev->cond);
					}
					pthread_mutex_unlock(&os_prev->mutex);
				}

				processed = 1;
			}

			/* Free the popped task, if any */
			if (task)
			{
				sfree(task->surface);
				sfree(task->queue);
				free(task);
			}
#ifdef DEBUG_TIME
			/*
			 * We do some time debugging ...
			 */
			if (oldvsync && (((lastvsync - oldvsync) / 1000) > 21 * 1000)) /* Only correct for 50Hz */
				VDPAU_TIME(LPQ2, "VSync diff: %" PRIu64 ", (%" PRIu64 ", %" PRIu64 ", %" PRIu64 "), q_len %d, skipped: %d",
				          ((lastvsync - oldvsync) / 1000), (lastvsync / 1000), (lastvsync / 1000),
					  ((stop - start) / 1000), q_length(Queue), skipped++);
			oldvsync = lastvsync;
#endif
			if (!processed)
				usleep(1000);
skip_display:
			;
		}
	}
	VDPAU_LOG(LINFO, "Thread ended");

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

	qt->device = sref(dev);

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

#ifdef USE_KVSYNC
	/* Enable kernel vsync */
	enable_vsync(qt);
#endif

	/* initialize queue and launch presentation thread */
	if (!Queue)
		Queue = q_queue_init();

	if (!(dev->flags & DEVICE_FLAG_THREAD)) {
		dev->flags &= ~DEVICE_FLAG_EXIT;
		pthread_create(&presentation_thread_id, NULL, presentation_thread, sref(qt));
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

	thread_control(CONTROL_END_THREAD);

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

	pthread_mutex_lock(&os->mutex);
	if (!(q->device->flags & DEVICE_FLAG_EXIT) && (os->status != VDP_PRESENTATION_QUEUE_STATUS_IDLE))
		pthread_cond_wait(&os->cond, &os->mutex);
	pthread_mutex_unlock(&os->mutex);

	*first_presentation_time = os->first_presentation_time;

#ifdef DEBUG_TIME
	os->start = get_vdp_time();
	VDPAU_LOG(LDBG, "Last surface %d was unblocked at %" PRIu64 ", Diff: %" PRIu64 "", os->id, os->start, (os->start - os->stop) / 1000);
#endif

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
