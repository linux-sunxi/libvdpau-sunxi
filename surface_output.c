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

#include <stropts.h>
#include "vdpau_private.h"
#include "ve.h"
#include "g2d_driver.h"

VdpStatus vdp_output_surface_create(VdpDevice device,
                                    VdpRGBAFormat rgba_format,
                                    uint32_t width,
                                    uint32_t height,
                                    VdpOutputSurface *surface)
{
	if (!surface)
		return VDP_STATUS_INVALID_POINTER;

	if (rgba_format != VDP_RGBA_FORMAT_B8G8R8A8 && rgba_format != VDP_RGBA_FORMAT_R8G8B8A8)
		return VDP_STATUS_INVALID_RGBA_FORMAT;

	if (width < 1 || width > 8192 || height < 1 || height > 8192)
		return VDP_STATUS_INVALID_SIZE;

	device_ctx_t *dev = handle_get(device);
	if (!dev)
		return VDP_STATUS_INVALID_HANDLE;

	output_surface_ctx_t *out = calloc(1, sizeof(output_surface_ctx_t));
	if (!out)
		return VDP_STATUS_RESOURCES;

	out->width = width;
	out->height = height;
	out->rgba_format = rgba_format;
	out->contrast = 1.0;
	out->saturation = 1.0;
	out->device = dev;

	if (out->device->osd_enabled)
	{
		out->data = ve_malloc(width * height * 4);
		if (!out->data)
		{
			free(out);
			return VDP_STATUS_RESOURCES;
		}

		g2d_fillrect args;

		args.flag = G2D_FIL_NONE;
		args.dst_image.addr[0] = ve_virt2phys(out->data) + 0x40000000;
		args.dst_image.w = width;
		args.dst_image.h = height;
		args.dst_image.format = G2D_FMT_ARGB_AYUV8888;
		args.dst_image.pixel_seq = G2D_SEQ_NORMAL;
		args.dst_rect.x = 0;
		args.dst_rect.y = 0;
		args.dst_rect.w = width;
		args.dst_rect.h = height;
		args.color = 0;
		args.alpha = 0;

		ioctl(dev->g2d_fd, G2D_CMD_FILLRECT, &args);
	}

	int handle = handle_create(out);
	if (handle == -1)
	{
		ve_free(out->data);
		free(out);
		return VDP_STATUS_RESOURCES;
	}

	*surface = handle;

	return VDP_STATUS_OK;
}

VdpStatus vdp_output_surface_destroy(VdpOutputSurface surface)
{
	output_surface_ctx_t *out = handle_get(surface);
	if (!out)
		return VDP_STATUS_INVALID_HANDLE;

	ve_free(out->data);

	handle_destroy(surface);
	free(out);

	return VDP_STATUS_OK;
}

VdpStatus vdp_output_surface_get_parameters(VdpOutputSurface surface,
                                            VdpRGBAFormat *rgba_format,
                                            uint32_t *width,
                                            uint32_t *height)
{
	output_surface_ctx_t *out = handle_get(surface);
	if (!out)
		return VDP_STATUS_INVALID_HANDLE;

	if (rgba_format)
		*rgba_format = out->rgba_format;

	if (width)
		*width = out->width;

	if (height)
		*height = out->height;

	return VDP_STATUS_OK;
}

VdpStatus vdp_output_surface_get_bits_native(VdpOutputSurface surface,
                                             VdpRect const *source_rect,
                                             void *const *destination_data,
                                             uint32_t const *destination_pitches)
{
	output_surface_ctx_t *out = handle_get(surface);
	if (!out)
		return VDP_STATUS_INVALID_HANDLE;



	return VDP_STATUS_ERROR;
}

VdpStatus vdp_output_surface_put_bits_native(VdpOutputSurface surface,
                                             void const *const *source_data,
                                             uint32_t const *source_pitches,
                                             VdpRect const *destination_rect)
{
	output_surface_ctx_t *out = handle_get(surface);
	if (!out)
		return VDP_STATUS_INVALID_HANDLE;

	VDPAU_DBG_ONCE("%s called but unimplemented!", __func__);



	return VDP_STATUS_OK;
}

VdpStatus vdp_output_surface_put_bits_indexed(VdpOutputSurface surface,
                                              VdpIndexedFormat source_indexed_format,
                                              void const *const *source_data,
                                              uint32_t const *source_pitch,
                                              VdpRect const *destination_rect,
                                              VdpColorTableFormat color_table_format,
                                              void const *color_table)
{
	if (color_table_format != VDP_COLOR_TABLE_FORMAT_B8G8R8X8)
		return VDP_STATUS_INVALID_COLOR_TABLE_FORMAT;

	output_surface_ctx_t *out = handle_get(surface);
	if (!out)
		return VDP_STATUS_INVALID_HANDLE;

	if (!out->device->osd_enabled)
		return VDP_STATUS_OK;

	int x, y, dest_width, dest_height;
	const uint32_t *colormap = color_table;
	const uint8_t *src_ptr = source_data[0];
	uint32_t *dst_ptr = out->data;

	if (destination_rect)
	{
		dest_width = destination_rect->x1 - destination_rect->x0;
		dest_height = destination_rect->y1 - destination_rect->y0;

		dst_ptr += destination_rect->y0 * out->width;
		dst_ptr += destination_rect->x0;
	}
	else
	{
		dest_width = out->width;
		dest_height = out->height;
	}

	for (y = 0; y < dest_height; y++)
	{
		for (x = 0; x < dest_width; x++)
		{
			uint8_t i, a;
			switch (source_indexed_format)
			{
			case VDP_INDEXED_FORMAT_I8A8:
				i = src_ptr[x * 2];
				a = src_ptr[x * 2 + 1];
				break;
			case VDP_INDEXED_FORMAT_A8I8:
				a = src_ptr[x * 2];
				i = src_ptr[x * 2 + 1];
				break;
			default:
				return VDP_STATUS_INVALID_INDEXED_FORMAT;
			}
			dst_ptr[x] = (colormap[i] & 0x00ffffff) | (a << 24);
		}
		src_ptr += source_pitch[0];
		dst_ptr += out->width;
	}

	ve_flush_cache(out->data, out->width * out->height * 4);

	return VDP_STATUS_OK;
}

VdpStatus vdp_output_surface_put_bits_y_cb_cr(VdpOutputSurface surface,
                                              VdpYCbCrFormat source_ycbcr_format,
                                              void const *const *source_data,
                                              uint32_t const *source_pitches,
                                              VdpRect const *destination_rect,
                                              VdpCSCMatrix const *csc_matrix)
{
	output_surface_ctx_t *out = handle_get(surface);
	if (!out)
		return VDP_STATUS_INVALID_HANDLE;

	VDPAU_DBG_ONCE("%s called but unimplemented!", __func__);



	return VDP_STATUS_OK;
}

VdpStatus vdp_output_surface_render_output_surface(VdpOutputSurface destination_surface,
                                                   VdpRect const *destination_rect,
                                                   VdpOutputSurface source_surface,
                                                   VdpRect const *source_rect,
                                                   VdpColor const *colors,
                                                   VdpOutputSurfaceRenderBlendState const *blend_state,
                                                   uint32_t flags)
{
	output_surface_ctx_t *out = handle_get(destination_surface);
	if (!out)
		return VDP_STATUS_INVALID_HANDLE;

	if (!out->device->osd_enabled)
		return VDP_STATUS_OK;

	output_surface_ctx_t *in = handle_get(source_surface);

	if (colors || flags)
		VDPAU_DBG_ONCE("%s: colors and flags not implemented!", __func__);

	g2d_blt args;

	// set up source/destination rects using defaults where required
	VdpRect s_rect = {0, 0, 0, 0};
	VdpRect d_rect = {0, 0, out->width, out->height};
	s_rect.x1 = in ? in->width : 1;
	s_rect.y1 = in ? in->height : 1;

	if (source_rect)
		s_rect = *source_rect;
	if (destination_rect)
		d_rect = *destination_rect;

	if (!in)
	{
		g2d_fillrect args;

		args.flag = G2D_FIL_PIXEL_ALPHA;
		args.dst_image.addr[0] = ve_virt2phys(out->data) + 0x40000000;
		args.dst_image.w = out->width;
		args.dst_image.h = out->height;
		args.dst_image.format = G2D_FMT_ARGB_AYUV8888;
		args.dst_image.pixel_seq = G2D_SEQ_NORMAL;
		args.dst_rect.x = d_rect.x0;
		args.dst_rect.y = d_rect.y0;
		args.dst_rect.w = d_rect.x1 - d_rect.x0;
		args.dst_rect.h = d_rect.y1 - d_rect.y0;
		args.color = 0xFFFFFFFF;
		args.alpha = 0xFFFFFFFF;

		ioctl(out->device->g2d_fd, G2D_CMD_FILLRECT, &args);
	}
	else
	{
		args.flag = G2D_BLT_PIXEL_ALPHA;
		args.src_image.addr[0] = ve_virt2phys(in->data) + 0x40000000;
		args.src_image.w = in->width;
		args.src_image.h = in->height;
		args.src_image.format = G2D_FMT_ARGB_AYUV8888;
		args.src_image.pixel_seq = G2D_SEQ_NORMAL;
		args.src_rect.x = s_rect.x0;
		args.src_rect.y = s_rect.y0;
		args.src_rect.w = s_rect.x1 - s_rect.x0;
		args.src_rect.h = s_rect.y1 - s_rect.y0;
		args.dst_image.addr[0] = ve_virt2phys(out->data) + 0x40000000;
		args.dst_image.w = out->width;
		args.dst_image.h = out->height;
		args.dst_image.format = G2D_FMT_ARGB_AYUV8888;
		args.dst_image.pixel_seq = G2D_SEQ_NORMAL;
		args.dst_x = d_rect.x0;
		args.dst_y = d_rect.y0;
		args.color = 0;
		args.alpha = 0;

		ioctl(out->device->g2d_fd, G2D_CMD_BITBLT, &args);
	}

	return VDP_STATUS_OK;
}

VdpStatus vdp_output_surface_render_bitmap_surface(VdpOutputSurface destination_surface,
                                                   VdpRect const *destination_rect,
                                                   VdpBitmapSurface source_surface,
                                                   VdpRect const *source_rect,
                                                   VdpColor const *colors,
                                                   VdpOutputSurfaceRenderBlendState const *blend_state,
                                                   uint32_t flags)
{
	output_surface_ctx_t *out = handle_get(destination_surface);
	if (!out)
		return VDP_STATUS_INVALID_HANDLE;

	if (!out->device->osd_enabled)
		return VDP_STATUS_OK;

	bitmap_surface_ctx_t *in = handle_get(source_surface);

	if (colors || flags)
		VDPAU_DBG_ONCE("%s: colors and flags not implemented!", __func__);

	g2d_blt args;

	// set up source/destination rects using defaults where required
	VdpRect s_rect = {0, 0, 0, 0};
	VdpRect d_rect = {0, 0, out->width, out->height};
	s_rect.x1 = in ? in->width : 1;
	s_rect.y1 = in ? in->height : 1;

	if (source_rect)
		s_rect = *source_rect;
	if (destination_rect)
		d_rect = *destination_rect;

	if (!in)
	{
		g2d_fillrect args;

		args.flag = G2D_FIL_PIXEL_ALPHA;
		args.dst_image.addr[0] = ve_virt2phys(out->data) + 0x40000000;
		args.dst_image.w = out->width;
		args.dst_image.h = out->height;
		args.dst_image.format = G2D_FMT_ARGB_AYUV8888;
		args.dst_image.pixel_seq = G2D_SEQ_NORMAL;
		args.dst_rect.x = d_rect.x0;
		args.dst_rect.y = d_rect.y0;
		args.dst_rect.w = d_rect.x1 - d_rect.x0;
		args.dst_rect.h = d_rect.y1 - d_rect.y0;
		args.color = 0xFFFFFFFF;
		args.alpha = 0xFFFFFFFF;

		ioctl(out->device->g2d_fd, G2D_CMD_FILLRECT, &args);
	}
	else
	{
		args.flag = G2D_BLT_PIXEL_ALPHA;
		args.src_image.addr[0] = ve_virt2phys(in->data) + 0x40000000;
		args.src_image.w = in->width;
		args.src_image.h = in->height;
		args.src_image.format = G2D_FMT_ARGB_AYUV8888;
		args.src_image.pixel_seq = G2D_SEQ_NORMAL;
		args.src_rect.x = s_rect.x0;
		args.src_rect.y = s_rect.y0;
		args.src_rect.w = s_rect.x1 - s_rect.x0;
		args.src_rect.h = s_rect.y1 - s_rect.y0;
		args.dst_image.addr[0] = ve_virt2phys(out->data) + 0x40000000;
		args.dst_image.w = out->width;
		args.dst_image.h = out->height;
		args.dst_image.format = G2D_FMT_ARGB_AYUV8888;
		args.dst_image.pixel_seq = G2D_SEQ_NORMAL;
		args.dst_x = d_rect.x0;
		args.dst_y = d_rect.y0;
		args.color = 0;
		args.alpha = 0;

		ioctl(out->device->g2d_fd, G2D_CMD_BITBLT, &args);
	}
	return VDP_STATUS_OK;
}

VdpStatus vdp_output_surface_query_capabilities(VdpDevice device,
                                                VdpRGBAFormat surface_rgba_format,
                                                VdpBool *is_supported,
                                                uint32_t *max_width,
                                                uint32_t *max_height)
{
	if (!is_supported || !max_width || !max_height)
		return VDP_STATUS_INVALID_POINTER;

	device_ctx_t *dev = handle_get(device);
	if (!dev)
		return VDP_STATUS_INVALID_HANDLE;

	*is_supported = (surface_rgba_format == VDP_RGBA_FORMAT_R8G8B8A8 || surface_rgba_format == VDP_RGBA_FORMAT_B8G8R8A8);
	*max_width = 8192;
	*max_height = 8192;

	return VDP_STATUS_OK;
}

VdpStatus vdp_output_surface_query_get_put_bits_native_capabilities(VdpDevice device,
                                                                    VdpRGBAFormat surface_rgba_format,
                                                                    VdpBool *is_supported)
{
	if (!is_supported)
		return VDP_STATUS_INVALID_POINTER;

	device_ctx_t *dev = handle_get(device);
	if (!dev)
		return VDP_STATUS_INVALID_HANDLE;

	*is_supported = VDP_FALSE;

	return VDP_STATUS_OK;
}

VdpStatus vdp_output_surface_query_put_bits_indexed_capabilities(VdpDevice device,
                                                                 VdpRGBAFormat surface_rgba_format,
                                                                 VdpIndexedFormat bits_indexed_format,
                                                                 VdpColorTableFormat color_table_format,
                                                                 VdpBool *is_supported)
{
	if (!is_supported)
		return VDP_STATUS_INVALID_POINTER;

	device_ctx_t *dev = handle_get(device);
	if (!dev)
		return VDP_STATUS_INVALID_HANDLE;

	*is_supported = VDP_FALSE;

	return VDP_STATUS_OK;
}

VdpStatus vdp_output_surface_query_put_bits_y_cb_cr_capabilities(VdpDevice device,
                                                                 VdpRGBAFormat surface_rgba_format,
                                                                 VdpYCbCrFormat bits_ycbcr_format,
                                                                 VdpBool *is_supported)
{
	if (!is_supported)
		return VDP_STATUS_INVALID_POINTER;

	device_ctx_t *dev = handle_get(device);
	if (!dev)
		return VDP_STATUS_INVALID_HANDLE;

	*is_supported = VDP_FALSE;

	return VDP_STATUS_OK;
}
