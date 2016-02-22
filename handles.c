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

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include "vdpau_private.h"

#define INITIAL_SIZE 16

static struct
{
	void **data;
	size_t size;
	pthread_rwlock_t lock;
} ht = { .lock = PTHREAD_RWLOCK_INITIALIZER };

void *handle_alloc(size_t size, f_destructor destructor)
{
	void *data = smalloc(size, 0, SHARED, destructor);
	if (data)
		memset(data, 0, size);

	return data;
}

VdpStatus handle_create(VdpHandle *handle, void *data)
{
	unsigned int index;
	VdpStatus ret = VDP_STATUS_ERROR;
	*handle = VDP_INVALID_HANDLE;

	if (!data)
		return ret;

	if (pthread_rwlock_wrlock(&ht.lock))
		return ret;

	for (index = 0; index < ht.size; index++)
		if (ht.data[index] == NULL)
			break;

	if (index >= ht.size)
	{
		int new_size = ht.size ? ht.size * 2 : INITIAL_SIZE;
		void **new_data = realloc(ht.data, new_size * sizeof(void *));
		if (!new_data)
			goto out;

		memset(new_data + ht.size, 0, (new_size - ht.size) * sizeof(void *));
		ht.data = new_data;
		ht.size = new_size;
	}

	ht.data[index] = sref(data);
	*handle = index + 1;
	ret = VDP_STATUS_OK;

out:
	pthread_rwlock_unlock(&ht.lock);

	return ret;
}

void *handle_get(VdpHandle handle)
{
	unsigned int index = handle - 1;
	void *data = NULL;

	if (pthread_rwlock_rdlock(&ht.lock))
		return NULL;

	if (index < ht.size && ht.data[index])
		data = sref(ht.data[index]);

	pthread_rwlock_unlock(&ht.lock);

	return data;
}

VdpStatus handle_destroy(VdpHandle handle)
{
	VdpStatus ret = VDP_STATUS_INVALID_HANDLE;
	unsigned int index = handle - 1;
	void *data = NULL;

	if (pthread_rwlock_wrlock(&ht.lock))
		return VDP_STATUS_ERROR;

	if (index < ht.size && ht.data[index])
	{
		data = ht.data[index];
		ht.data[index] = NULL;
		ret = VDP_STATUS_OK;
	}

	pthread_rwlock_unlock(&ht.lock);

	sfree(data);

	return ret;
}
