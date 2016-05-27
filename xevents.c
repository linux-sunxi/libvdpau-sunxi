/*
 * Copyright (c) 2016 Andreas Baierl <ichgeh@imkreisrum.de>
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
#include "xevents.h"

/*
 * Check for XEvents like position and dimension changes,
 * unmapping and mapping of the window
 * FIXME: not correct position if no surface is in queue
 */

int check_for_xevents(task_t *task)
{
	queue_ctx_t *q = task->queue;
	int i = 0;
	int ret_flags = 0;

	while (XPending(q->device->display) && i++ < 20)
	{
		XEvent ev;
		XNextEvent(q->device->display, &ev);

		switch(ev.type) {
		/*
		 * Window was unmapped.
		 * This closes both layers.
		 */
		case UnmapNotify:
			ret_flags &= ~(XEVENTS_DRAWABLE_CHANGE | XEVENTS_REINIT);
			ret_flags |= XEVENTS_DRAWABLE_UNMAP;
			break;
		/*
		 * Window was mapped.
		 * This restarts the displaying routines without extra resizing.
		 */
		case MapNotify:
			ret_flags |= XEVENTS_REINIT;
			ret_flags &= ~(XEVENTS_DRAWABLE_CHANGE | XEVENTS_DRAWABLE_UNMAP);
			break;
		/*
		 * Window dimension or position has changed.
		 * Reset x, y, width and height without restarting the whole displaying routines.
		 */
		case ConfigureNotify:
			if (ev.xconfigure.x != q->target->drawable_x ||
			    ev.xconfigure.y != q->target->drawable_y ||
			    ev.xconfigure.width != q->target->drawable_width ||
			    ev.xconfigure.height != q->target->drawable_height)
			{
				q->target->drawable_x = ev.xconfigure.x;
				q->target->drawable_y = ev.xconfigure.y;
				q->target->drawable_width = ev.xconfigure.width;
				q->target->drawable_height = ev.xconfigure.height;

				ret_flags |= XEVENTS_DRAWABLE_CHANGE | XEVENTS_REINIT;
				ret_flags &= ~XEVENTS_DRAWABLE_UNMAP;
			}
			break;
		default:
			break;
		}
	}
	return ret_flags;
}
