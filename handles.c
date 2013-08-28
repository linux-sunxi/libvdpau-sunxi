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

static void *handle_data[MAX_HANDLES];

int handle_create(void *data)
{
	if (!data)
		return -1;

	int i, handle = -1;

	for (i = 0; i < MAX_HANDLES; i++)
		if (handle_data[i] == NULL)
		{
			handle_data[i] = data;
			handle = i;
			break;
		}

	return handle;
}

void *handle_get(int handle)
{
	void *data = NULL;
	if (handle < MAX_HANDLES)
		data = handle_data[handle];

	return data;
}

void handle_destroy(int handle)
{
	if (handle < MAX_HANDLES)
		handle_data[handle] = NULL;
}
