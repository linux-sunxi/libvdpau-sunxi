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

#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include "vdpau_private.h"
#include "ve.h"

#define DEVICE "/dev/cedar_dev"
#define PAGE_OFFSET (0xc0000000) // from kernel
#define PAGE_SIZE (4096)

enum IOCTL_CMD
{
	IOCTL_UNKOWN = 0x100,
	IOCTL_GET_ENV_INFO,
	IOCTL_WAIT_VE,
	IOCTL_RESET_VE,
	IOCTL_ENABLE_VE,
	IOCTL_DISABLE_VE,
	IOCTL_SET_VE_FREQ,

	IOCTL_CONFIG_AVS2 = 0x200,
	IOCTL_GETVALUE_AVS2 ,
	IOCTL_PAUSE_AVS2 ,
	IOCTL_START_AVS2 ,
	IOCTL_RESET_AVS2 ,
	IOCTL_ADJUST_AVS2,
	IOCTL_ENGINE_REQ,
	IOCTL_ENGINE_REL,
	IOCTL_ENGINE_CHECK_DELAY,
	IOCTL_GET_IC_VER,
	IOCTL_ADJUST_AVS2_ABS,
	IOCTL_FLUSH_CACHE
};

struct ve_info
{
	uint32_t reserved_mem;
	int reserved_mem_size;
	uint32_t registers;
};

struct cedarv_cache_range
{
	long start;
	long end;
};

struct memchunk_t
{
	uint32_t phys_addr;
	int size;
	void *virt_addr;
	struct memchunk_t *next;
};

static struct
{
	int fd;
	void *regs;
	int version;
	struct memchunk_t first_memchunk;
	pthread_rwlock_t memory_lock;
	pthread_mutex_t device_lock;
} ve = { .fd = -1, .memory_lock = PTHREAD_RWLOCK_INITIALIZER, .device_lock = PTHREAD_MUTEX_INITIALIZER };

int ve_open(void)
{
	if (ve.fd != -1)
		return 0;

	struct ve_info info;

	ve.fd = open(DEVICE, O_RDWR);
	if (ve.fd == -1)
		return 0;

	if (ioctl(ve.fd, IOCTL_GET_ENV_INFO, (void *)(&info)) == -1)
		goto err;

	ve.regs = mmap(NULL, 0x800, PROT_READ | PROT_WRITE, MAP_SHARED, ve.fd, info.registers);
	if (ve.regs == MAP_FAILED)
		goto err;

	ve.first_memchunk.phys_addr = info.reserved_mem - PAGE_OFFSET;
	ve.first_memchunk.size = info.reserved_mem_size;

	ioctl(ve.fd, IOCTL_ENGINE_REQ, 0);
	ioctl(ve.fd, IOCTL_ENABLE_VE, 0);
	ioctl(ve.fd, IOCTL_SET_VE_FREQ, 320);
	ioctl(ve.fd, IOCTL_RESET_VE, 0);

	writel(0x00130007, ve.regs + VE_CTRL);

	ve.version = readl(ve.regs + VE_VERSION) >> 16;
	VDPAU_LOG(LINFO, "VE version 0x%04x opened.\n", ve.version);

	return 1;

err:
	close(ve.fd);
	ve.fd = -1;
	return 0;
}

void ve_close(void)
{
	if (ve.fd == -1)
		return;

	ioctl(ve.fd, IOCTL_DISABLE_VE, 0);
	ioctl(ve.fd, IOCTL_ENGINE_REL, 0);

	munmap(ve.regs, 0x800);
	ve.regs = NULL;

	close(ve.fd);
	ve.fd = -1;
}

int ve_get_version(void)
{
	return ve.version;
}

int ve_wait(int timeout)
{
	if (ve.fd == -1)
		return 0;

	return ioctl(ve.fd, IOCTL_WAIT_VE, timeout);
}

void *ve_get(int engine, uint32_t flags)
{
	if (pthread_mutex_lock(&ve.device_lock))
		return NULL;

	writel(0x00130000 | (engine & 0xf) | (flags & ~0xf), ve.regs + VE_CTRL);

	return ve.regs;
}

void ve_put(void)
{
	writel(0x00130007, ve.regs + VE_CTRL);
	pthread_mutex_unlock(&ve.device_lock);
}

void *ve_malloc(int size)
{
	if (ve.fd == -1)
		return NULL;

	if (pthread_rwlock_wrlock(&ve.memory_lock))
		return NULL;

	void *addr = NULL;

	size = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
	struct memchunk_t *c, *best_chunk = NULL;
	for (c = &ve.first_memchunk; c != NULL; c = c->next)
	{
		if(c->virt_addr == NULL && c->size >= size)
		{
			if (best_chunk == NULL || c->size < best_chunk->size)
				best_chunk = c;

			if (c->size == size)
				break;
		}
	}

	if (!best_chunk)
		goto out;

	int left_size = best_chunk->size - size;

	addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, ve.fd, best_chunk->phys_addr + PAGE_OFFSET);
	if (addr == MAP_FAILED)
	{
		addr = NULL;
		goto out;
	}

	best_chunk->virt_addr = addr;
	best_chunk->size = size;

	if (left_size > 0)
	{
		c = malloc(sizeof(struct memchunk_t));
		c->phys_addr = best_chunk->phys_addr + size;
		c->size = left_size;
		c->virt_addr = NULL;
		c->next = best_chunk->next;
		best_chunk->next = c;
	}

out:
	pthread_rwlock_unlock(&ve.memory_lock);
	return addr;
}

void ve_free(void *ptr)
{
	if (ve.fd == -1)
		return;

	if (ptr == NULL)
		return;

	if (pthread_rwlock_wrlock(&ve.memory_lock))
		return;

	struct memchunk_t *c;
	for (c = &ve.first_memchunk; c != NULL; c = c->next)
	{
		if (c->virt_addr == ptr)
		{
			munmap(ptr, c->size);
			c->virt_addr = NULL;
			break;
		}
	}

	for (c = &ve.first_memchunk; c != NULL; c = c->next)
	{
		if (c->virt_addr == NULL)
		{
			while (c->next != NULL && c->next->virt_addr == NULL)
			{
				struct memchunk_t *n = c->next;
				c->size += n->size;
				c->next = n->next;
				free(n);
			}
		}
	}

	pthread_rwlock_unlock(&ve.memory_lock);
}

uint32_t ve_virt2phys(void *ptr)
{
	if (ve.fd == -1)
		return 0;

	if (pthread_rwlock_rdlock(&ve.memory_lock))
		return 0;

	uint32_t addr = 0;

	struct memchunk_t *c;
	for (c = &ve.first_memchunk; c != NULL; c = c->next)
	{
		if (c->virt_addr == NULL)
			continue;

		if (c->virt_addr == ptr)
		{
			addr = c->phys_addr;
			break;
		}
		else if (ptr > c->virt_addr && ptr < (c->virt_addr + c->size))
		{
			addr = c->phys_addr + (ptr - c->virt_addr);
			break;
		}
	}

	pthread_rwlock_unlock(&ve.memory_lock);
	return addr;
}

void ve_flush_cache(void *start, int len)
{
	if (ve.fd == -1)
		return;

	struct cedarv_cache_range range =
	{
		.start = (int)start,
		.end = (int)(start + len)
	};

	ioctl(ve.fd, IOCTL_FLUSH_CACHE, (void*)(&range));
}
