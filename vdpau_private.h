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

#ifndef __VDPAU_PRIVATE_H__
#define __VDPAU_PRIVATE_H__

#define DEBUG
#define MAX_HANDLES 64
#define VBV_SIZE (1 * 1024 * 1024)

#include <stdlib.h>
#include <vdpau/vdpau.h>
#include <X11/Xlib.h>

#define INTERNAL_YCBCR_FORMAT (VdpYCbCrFormat)0xffff

typedef struct
{
	Display *display;
	int screen;
	VdpPreemptionCallback *preemption_callback;
	void *preemption_callback_context;
	int fd;
} device_ctx_t;

typedef struct video_surface_ctx_struct
{
	device_ctx_t *device;
	uint32_t width, height;
	VdpChromaType chroma_type;
	VdpYCbCrFormat source_format;
	void *data;
	int plane_size;
	void *decoder_private;
	void (*decoder_private_free)(struct video_surface_ctx_struct *surface);
} video_surface_ctx_t;

typedef struct decoder_ctx_struct
{
	uint32_t width, height;
	VdpDecoderProfile profile;
	void *data;
	device_ctx_t *device;
	VdpStatus (*decode)(struct decoder_ctx_struct *decoder, VdpPictureInfo const *info, const int len, video_surface_ctx_t *output);
	void *private;
	void (*private_free)(struct decoder_ctx_struct *decoder);
} decoder_ctx_t;

typedef struct
{
	Drawable drawable;
	int fd;
	int layer;
} queue_target_ctx_t;

typedef struct
{
	queue_target_ctx_t *target;
	VdpColor background;
	device_ctx_t *device;
} queue_ctx_t;

typedef struct
{
	device_ctx_t *device;
	int csc_change;
	float brightness;
	float contrast;
	float saturation;
	float hue;
} mixer_ctx_t;

typedef struct
{
	device_ctx_t *device;
	VdpRGBAFormat rgba_format;
	uint32_t width, height;
	video_surface_ctx_t *vs;
	VdpRect video_src_rect, video_dst_rect;
	int csc_change;
	float brightness;
	float contrast;
	float saturation;
	float hue;
} output_surface_ctx_t;

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof((a)) / sizeof((a)[0]))
#endif

#ifdef DEBUG
#include <stdio.h>
#define VDPAU_DBG(format, ...) fprintf(stderr, "[VDPAU SUNXI] " format "\n", ##__VA_ARGS__)
#define VDPAU_DBG_ONCE(format, ...) do { static uint8_t __once; if (!__once) { fprintf(stderr, "[VDPAU SUNXI] " format "\n", ##__VA_ARGS__); __once = 1; } } while(0)
#else
#define VDPAU_DBG(format, ...)
#define VDPAU_DBG_ONCE(format, ...)
#endif

VdpStatus new_decoder_mpeg12(decoder_ctx_t *decoder);
VdpStatus new_decoder_h264(decoder_ctx_t *decoder);

int handle_create(void *data);
void *handle_get(int handle);
void handle_destroy(int handle);

VdpStatus vdp_imp_device_create_x11(Display *display, int screen, VdpDevice *device, VdpGetProcAddress **get_proc_address);
VdpStatus vdp_device_destroy(VdpDevice device);
VdpStatus vdp_preemption_callback_register(VdpDevice device, VdpPreemptionCallback callback, void *context);

VdpStatus vdp_get_proc_address(VdpDevice device, VdpFuncId function_id, void **function_pointer);

char const *vdp_get_error_string(VdpStatus status);
VdpStatus vdp_get_api_version(uint32_t *api_version);
VdpStatus vdp_get_information_string(char const **information_string);


VdpStatus vdp_presentation_queue_target_create_x11(VdpDevice device, Drawable drawable, VdpPresentationQueueTarget *target);
VdpStatus vdp_presentation_queue_target_destroy(VdpPresentationQueueTarget presentation_queue_target);
VdpStatus vdp_presentation_queue_create(VdpDevice device, VdpPresentationQueueTarget presentation_queue_target, VdpPresentationQueue *presentation_queue);
VdpStatus vdp_presentation_queue_destroy(VdpPresentationQueue presentation_queue);
VdpStatus vdp_presentation_queue_set_background_color(VdpPresentationQueue presentation_queue, VdpColor *const background_color);
VdpStatus vdp_presentation_queue_get_background_color(VdpPresentationQueue presentation_queue, VdpColor *const background_color);
VdpStatus vdp_presentation_queue_get_time(VdpPresentationQueue presentation_queue, VdpTime *current_time);
VdpStatus vdp_presentation_queue_display(VdpPresentationQueue presentation_queue, VdpOutputSurface surface, uint32_t clip_width, uint32_t clip_height, VdpTime earliest_presentation_time);
VdpStatus vdp_presentation_queue_block_until_surface_idle(VdpPresentationQueue presentation_queue, VdpOutputSurface surface, VdpTime *first_presentation_time);
VdpStatus vdp_presentation_queue_query_surface_status(VdpPresentationQueue presentation_queue, VdpOutputSurface surface, VdpPresentationQueueStatus *status, VdpTime *first_presentation_time);

VdpStatus vdp_video_surface_create(VdpDevice device, VdpChromaType chroma_type, uint32_t width, uint32_t height, VdpVideoSurface *surface);
VdpStatus vdp_video_surface_destroy(VdpVideoSurface surface);
VdpStatus vdp_video_surface_get_parameters(VdpVideoSurface surface, VdpChromaType *chroma_type, uint32_t *width, uint32_t *height);
VdpStatus vdp_video_surface_get_bits_y_cb_cr(VdpVideoSurface surface, VdpYCbCrFormat destination_ycbcr_format, void *const *destination_data, uint32_t const *destination_pitches);
VdpStatus vdp_video_surface_put_bits_y_cb_cr(VdpVideoSurface surface, VdpYCbCrFormat source_ycbcr_format, void const *const *source_data, uint32_t const *source_pitches);
VdpStatus vdp_video_surface_query_capabilities(VdpDevice device, VdpChromaType surface_chroma_type, VdpBool *is_supported, uint32_t *max_width, uint32_t *max_height);
VdpStatus vdp_video_surface_query_get_put_bits_y_cb_cr_capabilities(VdpDevice device, VdpChromaType surface_chroma_type, VdpYCbCrFormat bits_ycbcr_format, VdpBool *is_supported);

VdpStatus vdp_output_surface_create(VdpDevice device, VdpRGBAFormat rgba_format, uint32_t width, uint32_t height, VdpOutputSurface  *surface);
VdpStatus vdp_output_surface_destroy(VdpOutputSurface surface);
VdpStatus vdp_output_surface_get_parameters(VdpOutputSurface surface, VdpRGBAFormat *rgba_format, uint32_t *width, uint32_t *height);
VdpStatus vdp_output_surface_get_bits_native(VdpOutputSurface surface, VdpRect const *source_rect, void *const *destination_data, uint32_t const *destination_pitches);
VdpStatus vdp_output_surface_put_bits_native(VdpOutputSurface surface, void const *const *source_data, uint32_t const *source_pitches, VdpRect const *destination_rect);
VdpStatus vdp_output_surface_put_bits_indexed(VdpOutputSurface surface, VdpIndexedFormat source_indexed_format, void const *const *source_data, uint32_t const *source_pitch, VdpRect const *destination_rect, VdpColorTableFormat color_table_format, void const *color_table);
VdpStatus vdp_output_surface_put_bits_y_cb_cr(VdpOutputSurface surface, VdpYCbCrFormat source_ycbcr_format, void const *const *source_data, uint32_t const *source_pitches, VdpRect const *destination_rect, VdpCSCMatrix const *csc_matrix);
VdpStatus vdp_output_surface_render_output_surface(VdpOutputSurface destination_surface, VdpRect const *destination_rect, VdpOutputSurface source_surface, VdpRect const *source_rect, VdpColor const *colors, VdpOutputSurfaceRenderBlendState const *blend_state, uint32_t flags);
VdpStatus vdp_output_surface_render_bitmap_surface(VdpOutputSurface destination_surface, VdpRect const *destination_rect, VdpBitmapSurface source_surface, VdpRect const *source_rect, VdpColor const *colors, VdpOutputSurfaceRenderBlendState const *blend_state, uint32_t flags);
VdpStatus vdp_output_surface_query_capabilities(VdpDevice device, VdpRGBAFormat surface_rgba_format, VdpBool *is_supported, uint32_t *max_width, uint32_t *max_height);
VdpStatus vdp_output_surface_query_get_put_bits_native_capabilities(VdpDevice device, VdpRGBAFormat surface_rgba_format, VdpBool *is_supported);
VdpStatus vdp_output_surface_query_put_bits_indexed_capabilities(VdpDevice device, VdpRGBAFormat surface_rgba_format, VdpIndexedFormat bits_indexed_format, VdpColorTableFormat color_table_format, VdpBool *is_supported);
VdpStatus vdp_output_surface_query_put_bits_y_cb_cr_capabilities(VdpDevice device, VdpRGBAFormat surface_rgba_format, VdpYCbCrFormat bits_ycbcr_format, VdpBool *is_supported);

VdpStatus vdp_video_mixer_create(VdpDevice device, uint32_t feature_count, VdpVideoMixerFeature const *features, uint32_t parameter_count, VdpVideoMixerParameter const *parameters, void const *const *parameter_values, VdpVideoMixer *mixer);
VdpStatus vdp_video_mixer_destroy(VdpVideoMixer mixer);
VdpStatus vdp_video_mixer_render(VdpVideoMixer mixer, VdpOutputSurface background_surface, VdpRect const *background_source_rect, VdpVideoMixerPictureStructure current_picture_structure, uint32_t video_surface_past_count, VdpVideoSurface const *video_surface_past, VdpVideoSurface video_surface_current, uint32_t video_surface_future_count, VdpVideoSurface const *video_surface_future, VdpRect const *video_source_rect, VdpOutputSurface destination_surface, VdpRect const *destination_rect, VdpRect const *destination_video_rect, uint32_t layer_count, VdpLayer const *layers);
VdpStatus vdp_video_mixer_get_feature_support(VdpVideoMixer mixer, uint32_t feature_count, VdpVideoMixerFeature const *features, VdpBool *feature_supports);
VdpStatus vdp_video_mixer_set_feature_enables(VdpVideoMixer mixer, uint32_t feature_count, VdpVideoMixerFeature const *features, VdpBool const *feature_enables);
VdpStatus vdp_video_mixer_get_feature_enables(VdpVideoMixer mixer, uint32_t feature_count, VdpVideoMixerFeature const *features, VdpBool *feature_enables);
VdpStatus vdp_video_mixer_set_attribute_values(VdpVideoMixer mixer, uint32_t attribute_count, VdpVideoMixerAttribute const *attributes, void const *const *attribute_values);
VdpStatus vdp_video_mixer_get_parameter_values(VdpVideoMixer mixer, uint32_t parameter_count, VdpVideoMixerParameter const *parameters, void *const *parameter_values);
VdpStatus vdp_video_mixer_get_attribute_values(VdpVideoMixer mixer, uint32_t attribute_count, VdpVideoMixerAttribute const *attributes, void *const *attribute_values);
VdpStatus vdp_video_mixer_query_feature_support(VdpDevice device, VdpVideoMixerFeature feature, VdpBool *is_supported);
VdpStatus vdp_video_mixer_query_parameter_support(VdpDevice device, VdpVideoMixerParameter parameter, VdpBool *is_supported);
VdpStatus vdp_video_mixer_query_parameter_value_range(VdpDevice device, VdpVideoMixerParameter parameter, void *min_value, void *max_value);
VdpStatus vdp_video_mixer_query_attribute_support(VdpDevice device, VdpVideoMixerAttribute attribute, VdpBool *is_supported);
VdpStatus vdp_video_mixer_query_attribute_value_range(VdpDevice device, VdpVideoMixerAttribute attribute, void *min_value, void *max_value);
VdpStatus vdp_generate_csc_matrix(VdpProcamp *procamp, VdpColorStandard standard, VdpCSCMatrix *csc_matrix);

VdpStatus vdp_decoder_create(VdpDevice device, VdpDecoderProfile profile, uint32_t width, uint32_t height, uint32_t max_references, VdpDecoder *decoder);
VdpStatus vdp_decoder_destroy(VdpDecoder decoder);
VdpStatus vdp_decoder_get_parameters(VdpDecoder decoder, VdpDecoderProfile *profile, uint32_t *width, uint32_t *height);
VdpStatus vdp_decoder_render(VdpDecoder decoder, VdpVideoSurface target, VdpPictureInfo const *picture_info, uint32_t bitstream_buffer_count, VdpBitstreamBuffer const *bitstream_buffers);
VdpStatus vdp_decoder_query_capabilities(VdpDevice device, VdpDecoderProfile profile, VdpBool *is_supported, uint32_t *max_level, uint32_t *max_macroblocks, uint32_t *max_width, uint32_t *max_height);

VdpStatus vdp_bitmap_surface_create(VdpDevice device, VdpRGBAFormat rgba_format, uint32_t width, uint32_t height, VdpBool frequently_accessed, VdpBitmapSurface *surface);
VdpStatus vdp_bitmap_surface_destroy(VdpBitmapSurface surface);
VdpStatus vdp_bitmap_surface_get_parameters(VdpBitmapSurface surface, VdpRGBAFormat *rgba_format, uint32_t *width, uint32_t *height, VdpBool *frequently_accessed);
VdpStatus vdp_bitmap_surface_put_bits_native(VdpBitmapSurface surface, void const *const *source_data, uint32_t const *source_pitches, VdpRect const *destination_rect);
VdpStatus vdp_bitmap_surface_query_capabilities(VdpDevice device, VdpRGBAFormat surface_rgba_format, VdpBool *is_supported, uint32_t *max_width, uint32_t *max_height);

#endif
