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
#include "rgba.h"

#ifdef GRAB
#include <sys/ioctl.h>
#include "g2d_driver.h"
#include "tiled_yuv.h"
#include "ve.h"
#endif

static void cleanup_output_surface(void *ptr, void *meta)
{
	output_surface_ctx_t *surface = ptr;

	rgba_destroy(&surface->rgba);
#ifdef GRAB
	rgba_destroy(&surface->grab_rgba);
#endif
	if (surface->yuv)
		yuv_unref(surface->yuv);

	pthread_cond_destroy(&surface->cond);
	pthread_mutex_destroy(&surface->mutex);

	sfree(surface->vs);
}

VdpStatus vdp_output_surface_create(VdpDevice device,
                                    VdpRGBAFormat rgba_format,
                                    uint32_t width,
                                    uint32_t height,
                                    VdpOutputSurface *surface)
{
	int ret = VDP_STATUS_OK;

	if (!surface)
		return VDP_STATUS_INVALID_POINTER;

	smart device_ctx_t *dev = handle_get(device);
	if (!dev)
		return VDP_STATUS_INVALID_HANDLE;

	smart output_surface_ctx_t *out = handle_alloc(sizeof(*out), cleanup_output_surface);
	if (!out)
		return VDP_STATUS_RESOURCES;

	out->contrast = 1.0;
	out->saturation = 1.0;
	out->first_presentation_time = 0;
	out->status = VDP_PRESENTATION_QUEUE_STATUS_IDLE;

	pthread_mutex_init(&out->mutex, NULL);
	pthread_cond_init(&out->cond, NULL);

	ret = rgba_create(&out->rgba, dev, width, height, rgba_format);
	if (ret != VDP_STATUS_OK)
		return ret;

#ifdef GRAB
	ret = rgba_create(&out->grab_rgba, dev, width, height, rgba_format);
	if (ret != VDP_STATUS_OK)
	{
		rgba_destroy(&out->rgba);
		return ret;
	}
#endif

	return handle_create(surface, out);
}

VdpStatus vdp_output_surface_get_parameters(VdpOutputSurface surface,
                                            VdpRGBAFormat *rgba_format,
                                            uint32_t *width,
                                            uint32_t *height)
{
	smart output_surface_ctx_t *out = handle_get(surface);
	if (!out)
		return VDP_STATUS_INVALID_HANDLE;

	if (rgba_format)
		*rgba_format = out->rgba.format;

	if (width)
		*width = out->rgba.width;

	if (height)
		*height = out->rgba.height;

	return VDP_STATUS_OK;
}

VdpStatus vdp_output_surface_get_bits_native(VdpOutputSurface surface,
                                             VdpRect const *source_rect,
                                             void *const *destination_data,
                                             uint32_t const *destination_pitches)
{
	smart output_surface_ctx_t *out = handle_get(surface);
	if (!out)
		return VDP_STATUS_INVALID_HANDLE;

#ifdef GRAB
	g2d_blt args;

	void *vs_data[2];
	void *data;
	uint32_t vs_destination_pitches[2];
	uint32_t width;
	uint32_t height;

	width = out->vs->width;
	height = out->vs->height;

	data = ve_malloc(width * height + ((width + 1) / 2) * ((height + 1) / 2));
	vs_data[0] = data;
	vs_data[1] = data + (width * height);
	vs_destination_pitches[0] = width;
	vs_destination_pitches[1] = width / 2;

	/* Convert tiled yuv into planar yuv YV12 */
	tiled_to_planar(out->vs->yuv->data, vs_data[0], vs_destination_pitches[0], out->vs->width, out->vs->height);
	tiled_to_planar(out->vs->yuv->data + out->vs->luma_size, vs_data[1], vs_destination_pitches[1], out->vs->width, out->vs->height / 2);

	/* Blit the video frame into the surface */
	args.flag = 0;
	args.src_image.addr[0] = ve_virt2phys(vs_data[0]) + DRAM_OFFSET;
	args.src_image.addr[1] = ve_virt2phys(vs_data[1]) + DRAM_OFFSET;
	args.src_image.w = width;
	args.src_image.h = height;
	args.src_image.format = G2D_FMT_PYUV420UVC;
	args.src_image.pixel_seq = G2D_SEQ_NORMAL;
	args.src_rect.x = source_rect->x0;
	args.src_rect.y = source_rect->y0;
	args.src_rect.w = source_rect->x1 - source_rect->x0;
	args.src_rect.h = source_rect->y1 - source_rect->y0;
	args.dst_image.addr[0] = ve_virt2phys(out->grab_rgba.data) + DRAM_OFFSET;
	args.dst_image.format = G2D_FMT_ARGB_AYUV8888;
	args.dst_image.pixel_seq = G2D_SEQ_NORMAL;
	args.dst_x = 0;
	args.dst_y = 0;
	args.dst_image.w = width;
	args.dst_image.h = height;
	args.flag |= G2D_BLT_PIXEL_ALPHA;

	ioctl(out->vs->device->g2d_fd, G2D_CMD_BITBLT, &args);

	/* Blit the osd frame into the surface */
	args.flag = 0;
	args.src_image.addr[0] = ve_virt2phys(out->rgba.data) + DRAM_OFFSET;
	args.src_image.w = width;
	args.src_image.h = height;
	args.src_image.format = G2D_FMT_ARGB_AYUV8888;
	args.src_image.pixel_seq = G2D_SEQ_NORMAL;
	args.src_rect.x = source_rect->x0;
	args.src_rect.y = source_rect->y0;
	args.src_rect.w = source_rect->x1 - source_rect->x0;
	args.src_rect.h = source_rect->y1 - source_rect->y0;
	args.dst_image.addr[0] = ve_virt2phys(out->grab_rgba.data) + DRAM_OFFSET;
	args.dst_image.format = G2D_FMT_ARGB_AYUV8888;
	args.dst_image.pixel_seq = G2D_SEQ_NORMAL;
	args.dst_x = 0;
	args.dst_y = 0;
	args.dst_image.w = width;
	args.dst_image.h = height;
	args.flag |= G2D_BLT_PIXEL_ALPHA;

	ioctl(out->vs->device->g2d_fd, G2D_CMD_BITBLT, &args);
	ve_free(data);

	return rgba_get_bits_native(&out->grab_rgba, source_rect, destination_data, destination_pitches);
#endif
	/*
	 * This is wrong, because we only get the rgba surface (OSD) back.
	 * In order to get both, video and OSD, we have to put them together
	 * into one surface maybe via blitting, and then grab the whole surface
	 * back into the buffer.
	 */

	return rgba_get_bits_native(&out->rgba, source_rect, destination_data, destination_pitches);
}

VdpStatus vdp_output_surface_put_bits_native(VdpOutputSurface surface,
                                             void const *const *source_data,
                                             uint32_t const *source_pitches,
                                             VdpRect const *destination_rect)
{
	smart output_surface_ctx_t *out = handle_get(surface);
	if (!out)
		return VDP_STATUS_INVALID_HANDLE;

	return rgba_put_bits_native(&out->rgba, source_data, source_pitches, destination_rect);
}

VdpStatus vdp_output_surface_put_bits_indexed(VdpOutputSurface surface,
                                              VdpIndexedFormat source_indexed_format,
                                              void const *const *source_data,
                                              uint32_t const *source_pitch,
                                              VdpRect const *destination_rect,
                                              VdpColorTableFormat color_table_format,
                                              void const *color_table)
{
	smart output_surface_ctx_t *out = handle_get(surface);
	if (!out)
		return VDP_STATUS_INVALID_HANDLE;

	return rgba_put_bits_indexed(&out->rgba, source_indexed_format, source_data, source_pitch,
					destination_rect, color_table_format, color_table);
}

VdpStatus vdp_output_surface_put_bits_y_cb_cr(VdpOutputSurface surface,
                                              VdpYCbCrFormat source_ycbcr_format,
                                              void const *const *source_data,
                                              uint32_t const *source_pitches,
                                              VdpRect const *destination_rect,
                                              VdpCSCMatrix const *csc_matrix)
{
	smart output_surface_ctx_t *out = handle_get(surface);
	if (!out)
		return VDP_STATUS_INVALID_HANDLE;

	return VDP_STATUS_ERROR;
}

VdpStatus vdp_output_surface_render_output_surface(VdpOutputSurface destination_surface,
                                                   VdpRect const *destination_rect,
                                                   VdpOutputSurface source_surface,
                                                   VdpRect const *source_rect,
                                                   VdpColor const *colors,
                                                   VdpOutputSurfaceRenderBlendState const *blend_state,
                                                   uint32_t flags)
{
	smart output_surface_ctx_t *out = handle_get(destination_surface);
	if (!out)
		return VDP_STATUS_INVALID_HANDLE;

	smart output_surface_ctx_t *in = handle_get(source_surface);
	if (!in)
		return rgba_render_surface(&out->rgba, destination_rect, NULL, source_rect,
			colors, blend_state, flags);

	return rgba_render_surface(&out->rgba, destination_rect, &in->rgba, source_rect,
		colors, blend_state, flags);
}

VdpStatus vdp_output_surface_render_bitmap_surface(VdpOutputSurface destination_surface,
                                                   VdpRect const *destination_rect,
                                                   VdpBitmapSurface source_surface,
                                                   VdpRect const *source_rect,
                                                   VdpColor const *colors,
                                                   VdpOutputSurfaceRenderBlendState const *blend_state,
                                                   uint32_t flags)
{
	smart output_surface_ctx_t *out = handle_get(destination_surface);
	if (!out)
		return VDP_STATUS_INVALID_HANDLE;

	smart bitmap_surface_ctx_t *in = handle_get(source_surface);
	if (!in)
		return rgba_render_surface(&out->rgba, destination_rect, NULL, source_rect,
			colors, blend_state, flags);

	return rgba_render_surface(&out->rgba, destination_rect, &in->rgba, source_rect,
		colors, blend_state, flags);

}

VdpStatus vdp_output_surface_query_capabilities(VdpDevice device,
                                                VdpRGBAFormat surface_rgba_format,
                                                VdpBool *is_supported,
                                                uint32_t *max_width,
                                                uint32_t *max_height)
{
	if (!is_supported || !max_width || !max_height)
		return VDP_STATUS_INVALID_POINTER;

	smart device_ctx_t *dev = handle_get(device);
	if (!dev)
		return VDP_STATUS_INVALID_HANDLE;

	*is_supported = (surface_rgba_format == VDP_RGBA_FORMAT_R8G8B8A8 ||
			 surface_rgba_format == VDP_RGBA_FORMAT_B8G8R8A8 ||
			 surface_rgba_format == VDP_RGBA_FORMAT_A8) ? VDP_TRUE : VDP_FALSE;
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

	smart device_ctx_t *dev = handle_get(device);
	if (!dev)
		return VDP_STATUS_INVALID_HANDLE;

#ifdef GRAB
	*is_supported = (surface_rgba_format == VDP_RGBA_FORMAT_R8G8B8A8 ||
			 surface_rgba_format == VDP_RGBA_FORMAT_B8G8R8A8 ||
			 surface_rgba_format == VDP_RGBA_FORMAT_A8) ? VDP_TRUE : VDP_FALSE;
#else
	*is_supported = VDP_FALSE;
#endif

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

	smart device_ctx_t *dev = handle_get(device);
	if (!dev)
		return VDP_STATUS_INVALID_HANDLE;

	*is_supported = VDP_FALSE;

	if (color_table_format != VDP_COLOR_TABLE_FORMAT_B8G8R8X8)
		return VDP_STATUS_OK;

	if (surface_rgba_format == VDP_RGBA_FORMAT_R8G8B8A8 ||
	    surface_rgba_format == VDP_RGBA_FORMAT_B8G8R8A8 ||
	    surface_rgba_format == VDP_RGBA_FORMAT_A8)
	{
		*is_supported = (bits_indexed_format == VDP_INDEXED_FORMAT_I8A8 ||
				 bits_indexed_format == VDP_INDEXED_FORMAT_A8I8) ? VDP_TRUE : VDP_FALSE;
	}

	return VDP_STATUS_OK;
}

VdpStatus vdp_output_surface_query_put_bits_y_cb_cr_capabilities(VdpDevice device,
                                                                 VdpRGBAFormat surface_rgba_format,
                                                                 VdpYCbCrFormat bits_ycbcr_format,
                                                                 VdpBool *is_supported)
{
	if (!is_supported)
		return VDP_STATUS_INVALID_POINTER;

	smart device_ctx_t *dev = handle_get(device);
	if (!dev)
		return VDP_STATUS_INVALID_HANDLE;

	*is_supported = VDP_FALSE;

	return VDP_STATUS_OK;
}
