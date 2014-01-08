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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stropts.h>
#include <sys/mman.h>
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

static int fd = -1;
static void *regs = NULL;
static int version = 0;

struct memchunk_t
{
	uint32_t phys_addr;
	int size;
	void *virt_addr;
	struct memchunk_t *next;
};

static struct memchunk_t first_memchunk = { .phys_addr = 0x0, .size = 0, .virt_addr = NULL, .next = NULL };

int ve_open(void)
{
	if (fd != -1)
		return 0;

	struct ve_info ve;

	fd = open(DEVICE, O_RDWR);
	if (fd == -1)
		return 0;

	if (ioctl(fd, IOCTL_GET_ENV_INFO, (void *)(&ve)) == -1)
	{
		close(fd);
		fd = -1;
		return 0;
	}

	regs = mmap(NULL, 0x800, PROT_READ | PROT_WRITE, MAP_SHARED, fd, ve.registers);
	first_memchunk.phys_addr = ve.reserved_mem - PAGE_OFFSET;
	first_memchunk.size = ve.reserved_mem_size;

	ioctl(fd, IOCTL_ENGINE_REQ, 0);
	ioctl(fd, IOCTL_ENABLE_VE, 0);
	ioctl(fd, IOCTL_SET_VE_FREQ, 320);
	ioctl(fd, IOCTL_RESET_VE, 0);

	writel(0x00130007, regs + VE_CTRL);

	version = readl(regs + VE_VERSION) >> 16;
	printf("[VDPAU SUNXI] VE version 0x%04x opened.\n", version);

	return 1;
}

void ve_close(void)
{
	if (fd == -1)
		return;

	ioctl(fd, IOCTL_DISABLE_VE, 0);
	ioctl(fd, IOCTL_ENGINE_REL, 0);

	munmap(regs, 0x800);

	close(fd);
	fd = -1;
}

void ve_flush_cache(void *start, int len)
{
	if (fd == -1)
		return;

	struct cedarv_cache_range range =
	{
		.start = (int)start,
		.end = (int)(start + len)
	};

	ioctl(fd, IOCTL_FLUSH_CACHE, (void*)(&range));
}

void *ve_get_regs(void)
{
	if (fd == -1)
		return NULL;

	return regs;
}

int ve_get_version(void)
{
	return version;
}

int ve_wait(int timeout)
{
	if (fd == -1)
		return 0;

	return ioctl(fd, IOCTL_WAIT_VE, timeout);
}

void *ve_malloc(int size)
{
	if (fd == -1)
		return NULL;

	size = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
	struct memchunk_t *c, *best_chunk = NULL;
	for (c = &first_memchunk; c != NULL; c = c->next)
		if(c->virt_addr == NULL && c->size >= size)
		{
			if (best_chunk == NULL || c->size < best_chunk->size)
				best_chunk = c;

			if (c->size == size)
				break;
		}

	if (!best_chunk)
		return NULL;

	int left_size = best_chunk->size - size;

	best_chunk->virt_addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, best_chunk->phys_addr + PAGE_OFFSET);
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

	return best_chunk->virt_addr;
}

void ve_free(void *ptr)
{
	if (fd == -1)
		return;

	if (ptr == NULL)
		return;

	struct memchunk_t *c;
	for (c = &first_memchunk; c != NULL; c = c->next)
		if (c->virt_addr == ptr)
		{
			munmap(ptr, c->size);
			c->virt_addr = NULL;
			break;
		}

	for (c = &first_memchunk; c != NULL; c = c->next)
		if (c->virt_addr == NULL)
			while (c->next != NULL && c->next->virt_addr == NULL)
			{
				struct memchunk_t *n = c->next;
				c->size += n->size;
				c->next = n->next;
				free(n);
			}
}

uint32_t ve_virt2phys(void *ptr)
{
	if (fd == -1)
		return 0;

	struct memchunk_t *c;
	for (c = &first_memchunk; c != NULL; c = c->next)
	{
		if (c->virt_addr == NULL)
			continue;

		if (c->virt_addr == ptr)
			return c->phys_addr;
		else if (ptr > c->virt_addr && ptr < (c->virt_addr + c->size))
			return c->phys_addr + (ptr - c->virt_addr);
	}

	return 0;
}
