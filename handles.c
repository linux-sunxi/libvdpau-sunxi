/*
 * Copyright (c) 2013-2014 Jens Kuske <jenskuske@gmail.com>
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

#include <stdlib.h>
#include <string.h>
#include "vdpau_private.h"

#define INITIAL_SIZE 16

static struct
{
	void **data;
	size_t size;
} ht;

void *handle_create(size_t size, VdpHandle *handle)
{
	unsigned int index;

	for (index = 0; index < ht.size; index++)
		if (ht.data[index] == NULL)
			break;

	if (index >= ht.size)
	{
		int new_size = ht.size ? ht.size * 2 : INITIAL_SIZE;
		void **new_data = realloc(ht.data, new_size * sizeof(void *));
		if (!new_data)
			goto err;

		memset(new_data + ht.size, 0, (new_size - ht.size) * sizeof(void *));
		ht.data = new_data;
		ht.size = new_size;
	}

	void *data = calloc(1, size);
	if (!data)
		goto err;

	ht.data[index] = data;
	*handle = index + 1;
	return data;

err:
	*handle = VDP_INVALID_HANDLE;
	return NULL;
}

void *handle_get(VdpHandle handle)
{
	if (handle == VDP_INVALID_HANDLE)
		return NULL;

	unsigned int index = handle - 1;
	if (index < ht.size)
		return ht.data[index];

	return NULL;
}

void handle_destroy(VdpHandle handle)
{
	unsigned int index = handle - 1;

	if (index < ht.size)
	{
		free(ht.data[index]);
		ht.data[index] = NULL;
	}
}
