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
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <cedrus/cedrus.h>
#include <sys/ioctl.h>
#include "rgba.h"
#include "sunxi_disp.h"
#include "queue.h"

static void *presentation_thread(void *param);

typedef struct task
{
	VdpTime			when;
	uint32_t		clip_width;
	uint32_t		clip_height;
	output_surface_ctx_t	*surface;
	int			exit_thread;
} task_t;

static uint64_t get_time(void)
{
	struct timespec tp;

	if (clock_gettime(CLOCK_MONOTONIC, &tp) == -1)
		return 0;

	return (uint64_t)tp.tv_sec * 1000000000ULL + (uint64_t)tp.tv_nsec;
}

static void cleanup_presentation_queue_target(void *ptr, void *meta)
{
	queue_target_ctx_t *target = ptr;
	sfree(target->device);

	target->disp->close(target->disp);
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

	qt->drawable = drawable;
	XSetWindowBackground(dev->display, drawable, 0x000102);

	qt->disp = sunxi_disp_open(dev->osd_enabled);

	if (!qt->disp)
		qt->disp = sunxi_disp2_open(dev->osd_enabled);

	if (!qt->disp)
		qt->disp = sunxi_disp1_5_open(dev->osd_enabled);

	if (!qt->disp)
		return VDP_STATUS_ERROR;

	qt->device = sref(dev);

	return handle_create(target, qt);
}

static void cleanup_presentation_queue(void *ptr, void *meta)
{
	queue_ctx_t *q = ptr;

	sfree(q->target);
	sfree(q->device);
}

VdpStatus vdp_presentation_queue_destroy(VdpPresentationQueue presentation_queue)
{
	smart queue_ctx_t *q = handle_get(presentation_queue);
	if (!q)
		return VDP_STATUS_INVALID_HANDLE;

	task_t *task = (task_t *)calloc(1, sizeof(task_t));
	task->exit_thread = 1;
	if (q_push_tail(q->queue, task))
		free(task);

	pthread_join(q->presentation_thread_id, NULL);

	q_queue_free(q->queue, 0);
	q->queue = NULL;

	return handle_destroy(presentation_queue);
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

	q->target = sref(qt);
	q->device = sref(dev);

	q->queue = q_queue_init();
	if (!q->queue)
		return VDP_STATUS_RESOURCES;

	pthread_create(&q->presentation_thread_id, NULL, presentation_thread, sref(q));

	return handle_create(presentation_queue, q);
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

	*current_time = get_time();
	return VDP_STATUS_OK;
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
	task->exit_thread = 0;
	os->first_presentation_time = 0;
	os->status = VDP_PRESENTATION_QUEUE_STATUS_QUEUED;

	if (q_push_tail(q->queue, task))
	{
		VDPAU_DBG("Error inserting task");
		sfree(task->surface);
		free(task);
		return VDP_STATUS_ERROR;
	}

	return VDP_STATUS_OK;
}

static VdpStatus do_presentation_queue_display(queue_ctx_t *q, task_t *task)
{
	output_surface_ctx_t *os = task->surface;

	uint32_t clip_width = task->clip_width;
	uint32_t clip_height = task->clip_height;

	Window c;
	int x,y;
	XTranslateCoordinates(q->device->display, q->target->drawable, RootWindow(q->device->display, q->device->screen), 0, 0, &x, &y, &c);
	XClearWindow(q->device->display, q->target->drawable);

	if (os->vs)
		q->target->disp->set_video_layer(q->target->disp, x, y, clip_width, clip_height, os);
	else
		q->target->disp->close_video_layer(q->target->disp);

	if (!q->device->osd_enabled)
		return VDP_STATUS_OK;

	if (os->rgba.flags & RGBA_FLAG_NEEDS_CLEAR)
		rgba_clear(&os->rgba);

	if (os->rgba.flags & RGBA_FLAG_DIRTY)
	{
		rgba_flush(&os->rgba);

		q->target->disp->set_osd_layer(q->target->disp, x, y, clip_width, clip_height, os);
	}
	else
	{
		q->target->disp->close_osd_layer(q->target->disp);
	}

	return VDP_STATUS_OK;
}

int rebuild_buffer(QUEUE* queue, int max_surface_buffer, int interval, int timeout)
{
	int breakout = 0;
	while (q_length(queue) < max_surface_buffer)
	{
		if (timeout && breakout > timeout)
			return q_length(queue);
		usleep(interval);
		breakout += interval;
	}
	return max_surface_buffer;
}

static void *presentation_thread(void *param)
{
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

	smart queue_ctx_t *q = (queue_ctx_t *)param;

	output_surface_ctx_t *os_prev = NULL;
	output_surface_ctx_t *os_cur = NULL;

	int processed = 0;

	VdpTime lastvsync = 0;

	rebuild_buffer(q->queue, MAX_SURFACE_BUFFER, 1 * 1000, 2 * 1000 * 1000);

	while (1)
	{
		task_t *task = NULL;
		processed = 0;

		if (!q_isEmpty(q->queue))
		{
			q_pop_head(q->queue, (void *)&task);

			if (task->exit_thread)
			{
				sfree(os_cur);
				sfree(os_prev);
				os_cur = NULL;
				os_prev = NULL;
				free(task);
				break;
			}

			sfree(os_prev);
			os_prev = os_cur;
			os_cur = sref(task->surface);

			do_presentation_queue_display(q, task);

			q->target->disp->wait_for_vsync(q->target->disp);
			lastvsync = get_time();

			if (os_cur)
			{
				os_cur->first_presentation_time = lastvsync;
				os_cur->status = VDP_PRESENTATION_QUEUE_STATUS_VISIBLE;
			}

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

		if (task)
		{
			sfree(task->surface);
			free(task);
		}

		if (!processed)
			usleep(1000);
	}
	return NULL;
}



VdpStatus vdp_presentation_queue_block_until_surface_idle(VdpPresentationQueue presentation_queue,
                                                          VdpOutputSurface surface,
                                                          VdpTime *first_presentation_time)
{
	smart queue_ctx_t *q = handle_get(presentation_queue);
	if (!q)
		return VDP_STATUS_INVALID_HANDLE;

	smart output_surface_ctx_t *out = handle_get(surface);
	if (!out)
		return VDP_STATUS_INVALID_HANDLE;

	pthread_mutex_lock(&out->mutex);
	if (out->status != VDP_PRESENTATION_QUEUE_STATUS_IDLE)
		pthread_cond_wait(&out->cond, &out->mutex);
	pthread_mutex_unlock(&out->mutex);

	*first_presentation_time = out->first_presentation_time;

	return VDP_STATUS_OK;
}

VdpStatus vdp_presentation_queue_query_surface_status(VdpPresentationQueue presentation_queue,
                                                      VdpOutputSurface surface,
                                                      VdpPresentationQueueStatus *status,
                                                      VdpTime *first_presentation_time)
{
	smart queue_ctx_t *q = handle_get(presentation_queue);
	if (!q)
		return VDP_STATUS_INVALID_HANDLE;

	smart output_surface_ctx_t *out = handle_get(surface);
	if (!out)
		return VDP_STATUS_INVALID_HANDLE;

	*status = out->status;
	*first_presentation_time = out->first_presentation_time;

	return VDP_STATUS_OK;
}
