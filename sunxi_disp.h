/*
 * Copyright (c) 2015 Jens Kuske <jenskuske@gmail.com>
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

#ifndef SUNXI_DISP_H_
#define SUNXI_DISP_H_

typedef struct output_surface_ctx_struct output_surface_ctx_t;

struct sunxi_disp
{
	void (*close)(struct sunxi_disp *sunxi_disp);
	int (*set_video_layer)(struct sunxi_disp *sunxi_disp, int x, int y, int width, int height, output_surface_ctx_t *surface);
	void (*close_video_layer)(struct sunxi_disp *sunxi_disp);
	int (*set_osd_layer)(struct sunxi_disp *sunxi_disp, int x, int y, int width, int height, output_surface_ctx_t *surface);
	void (*close_osd_layer)(struct sunxi_disp *sunxi_disp);
};

struct sunxi_disp *sunxi_disp_open(int osd_enabled);
struct sunxi_disp *sunxi_disp2_open(int osd_enabled);
struct sunxi_disp *sunxi_disp1_5_open(int osd_enabled);

#endif
