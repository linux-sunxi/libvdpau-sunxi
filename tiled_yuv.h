/*
 * Copyright (c) 2014 Jens Kuske <jenskuske@gmail.com>
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

#ifndef __TILED_YUV_H__
#define __TILED_YUV_H__

void tiled_to_planar(void *src, void *dst, unsigned int dst_pitch,
                     unsigned int width, unsigned int height);

void tiled_deinterleave_to_planar(void *src, void *dst1, void *dst2,
                                  unsigned int dst_pitch,
                                  unsigned int width, unsigned int height);

#endif
