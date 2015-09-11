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

#define MAX_HANDLES 64
#define VBV_SIZE (1 * 1024 * 1024)
#define DRAM_OFFSET (0x40000000)

#define DEBUG
#define DEBUG_LEVEL LINFO
//#define DEBUG_TIME
#define DEBUG_LEVEL_TIME LDEC

/*
 * Set this to 1 if you want csc conversion to the full range 0~255 (TV use)
 * If set to 0, csc conversion will use the range 16~235 (PC use)
 */
#define CSC_FULL_RANGE 1
//#define GRAB /* Uncomment this to try experimental get_bits_native support */

#include <stdlib.h>
#include <csptr/smart_ptr.h>
#include <vdpau/vdpau.h>
#include <vdpau/vdpau_x11.h>
#include <X11/Xlib.h>

#include "vdpau_log.h"

#define INTERNAL_YCBCR_FORMAT (VdpYCbCrFormat)0xffff

/* Flags for device */
#define DEVICE_FLAG_OSD (1 << 0)
#define DEVICE_FLAG_DEINT (1 << 1)
#define DEVICE_FLAG_VSYNC (1 << 2)
#define DEVICE_FLAG_THREAD (1 << 3)
#define DEVICE_FLAG_EXIT (1 << 4)
#define DEVICE_FLAG_VLAYEROPEN (1 << 5)
#define DEVICE_FLAG_RLAYEROPEN (1 << 6)

typedef struct
{
	Display *display;
	int screen;
	VdpPreemptionCallback *preemption_callback;
	void *preemption_callback_context;
	int fb_fd;
	int g2d_fd;
	uint32_t flags;
} device_ctx_t;

typedef struct
{
	int ref_count;
	void *data;
} yuv_data_t;

typedef struct video_surface_ctx_struct
{
	device_ctx_t *device;
	uint32_t width, height;
	VdpChromaType chroma_type;
	VdpYCbCrFormat source_format;
	yuv_data_t *yuv;
	int luma_size;
	void *decoder_private;
	void (*decoder_private_free)(struct video_surface_ctx_struct *surface);
	VdpColor background;
	int first_frame;
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
	int layer_top;
	int x, y;
	int drawable_change;
	int drawable_unmap;
	int drawable_x;
	int drawable_y;
	int drawable_width;
	int drawable_height;
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
	int start_stream;
	int csc_change;
	float brightness;
	float contrast;
	float saturation;
	float hue;
	int deinterlace;
	int deint_supported;
	int custom_csc;
	VdpCSCMatrix csc_matrix;
	VdpChromaType chroma_type;
	int video_width;
	int video_height;
	int layers;
	VdpColor background;
	int bg_change;
} mixer_ctx_t;

/* Flags for rgba surface */
#define RGBA_FLAG_DIRTY (1 << 0)
#define RGBA_FLAG_NEEDS_FLUSH (1 << 1)
#define RGBA_FLAG_NEEDS_CLEAR (1 << 2)
#define RGBA_FLAG_CHANGED (1 << 3)
#define RGBA_FLAG_RENDERED (1 << 4)

typedef struct
{
	device_ctx_t *device;
	VdpRGBAFormat format;
	uint32_t width, height;
	void *data;
	VdpRect dirty;
	uint32_t flags;
	uint32_t id;
} rgba_surface_t;

typedef struct
{
	rgba_surface_t rgba, prev_rgba;
#ifdef GRAB
	rgba_surface_t grab_rgba;
#endif
	video_surface_ctx_t *vs;
	yuv_data_t *yuv;
	VdpRect video_src_rect, video_dst_rect;
	int csc_change;
	int bg_change;
	uint32_t rgba_cnt;
	float brightness;
	float contrast;
	float saturation;
	float hue;
	int video_deinterlace, video_field;
	VdpTime first_presentation_time;
	VdpPresentationQueueStatus status;
	uint32_t id;
	VdpTime start, stop;
} output_surface_ctx_t;

typedef struct
{
	rgba_surface_t rgba;
	VdpBool frequently_accessed;
} bitmap_surface_ctx_t;

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof((a)) / sizeof((a)[0]))
#endif

#define max(a, b) \
	({ __typeof__ (a) _a = (a); \
	   __typeof__ (b) _b = (b); \
	  _a > _b ? _a : _b; })

#define min(a, b) \
	({ __typeof__ (a) _a = (a); \
	   __typeof__ (b) _b = (b); \
	  _a < _b ? _a : _b; })

#define min_nz(a, b) \
        ({ __typeof__ (a) _a = (a); \
           __typeof__ (b) _b = (b); \
           _a < _b ? (_a == 0 ? _b : _a) : (_b == 0 ? _a : _b); })

#define ALIGN(x, a) (((x) + ((typeof(x))(a) - 1)) & ~((typeof(x))(a) - 1))

#define EXPORT __attribute__ ((visibility ("default")))

VdpStatus new_decoder_mpeg12(decoder_ctx_t *decoder);
VdpStatus new_decoder_h264(decoder_ctx_t *decoder);
VdpStatus new_decoder_mpeg4(decoder_ctx_t *decoder);

void yuv_unref(yuv_data_t *yuv);
yuv_data_t *yuv_ref(yuv_data_t *yuv);
VdpStatus yuv_prepare(video_surface_ctx_t *video_surface);
void set_csc_matrix(mixer_ctx_t *mix, VdpColorStandard standard);

typedef uint32_t VdpHandle;
typedef float csc_m[3][4];
VdpTime get_vdp_time(void);

/* Flags for thread control */
#define CONTROL_NULL			0
#define CONTROL_REINIT_DISPLAY		1
#define CONTROL_DISABLE_VIDEO		2
#define CONTROL_END_THREAD		3
int thread_control(uint32_t flag);

__attribute__((malloc)) void *handle_alloc(size_t size, f_destructor destructor);
VdpStatus handle_create(VdpHandle *handle, void *data);
void *handle_get(VdpHandle handle);
VdpStatus handle_destroy(VdpHandle handle);

EXPORT VdpDeviceCreateX11 vdp_imp_device_create_x11;
VdpPreemptionCallbackRegister vdp_preemption_callback_register;

VdpGetProcAddress vdp_get_proc_address;

VdpGetErrorString vdp_get_error_string;
VdpGetApiVersion vdp_get_api_version;
VdpGetInformationString vdp_get_information_string;

VdpPresentationQueueTargetCreateX11 vdp_presentation_queue_target_create_x11;
VdpPresentationQueueCreate vdp_presentation_queue_create;
VdpPresentationQueueDestroy vdp_presentation_queue_destroy;
VdpPresentationQueueSetBackgroundColor vdp_presentation_queue_set_background_color;
VdpPresentationQueueGetBackgroundColor vdp_presentation_queue_get_background_color;
VdpPresentationQueueGetTime vdp_presentation_queue_get_time;
VdpPresentationQueueDisplay vdp_presentation_queue_display;
VdpPresentationQueueBlockUntilSurfaceIdle vdp_presentation_queue_block_until_surface_idle;
VdpPresentationQueueQuerySurfaceStatus vdp_presentation_queue_query_surface_status;

VdpVideoSurfaceCreate vdp_video_surface_create;
VdpVideoSurfaceGetParameters vdp_video_surface_get_parameters;
VdpVideoSurfaceGetBitsYCbCr vdp_video_surface_get_bits_y_cb_cr;
VdpVideoSurfacePutBitsYCbCr vdp_video_surface_put_bits_y_cb_cr;
VdpVideoSurfaceQueryCapabilities vdp_video_surface_query_capabilities;
VdpVideoSurfaceQueryGetPutBitsYCbCrCapabilities vdp_video_surface_query_get_put_bits_y_cb_cr_capabilities;

VdpOutputSurfaceCreate vdp_output_surface_create;
VdpOutputSurfaceGetParameters vdp_output_surface_get_parameters;
VdpOutputSurfaceGetBitsNative vdp_output_surface_get_bits_native;
VdpOutputSurfacePutBitsNative vdp_output_surface_put_bits_native;
VdpOutputSurfacePutBitsIndexed vdp_output_surface_put_bits_indexed;
VdpOutputSurfacePutBitsYCbCr vdp_output_surface_put_bits_y_cb_cr;
VdpOutputSurfaceRenderOutputSurface vdp_output_surface_render_output_surface;
VdpOutputSurfaceRenderBitmapSurface vdp_output_surface_render_bitmap_surface;
VdpOutputSurfaceQueryCapabilities vdp_output_surface_query_capabilities;
VdpOutputSurfaceQueryGetPutBitsNativeCapabilities vdp_output_surface_query_get_put_bits_native_capabilities;
VdpOutputSurfaceQueryPutBitsIndexedCapabilities vdp_output_surface_query_put_bits_indexed_capabilities;
VdpOutputSurfaceQueryPutBitsYCbCrCapabilities vdp_output_surface_query_put_bits_y_cb_cr_capabilities;

VdpVideoMixerCreate vdp_video_mixer_create;
VdpVideoMixerRender vdp_video_mixer_render;
VdpVideoMixerGetFeatureSupport vdp_video_mixer_get_feature_support;
VdpVideoMixerSetFeatureEnables vdp_video_mixer_set_feature_enables;
VdpVideoMixerGetFeatureEnables vdp_video_mixer_get_feature_enables;
VdpVideoMixerSetAttributeValues vdp_video_mixer_set_attribute_values;
VdpVideoMixerGetParameterValues vdp_video_mixer_get_parameter_values;
VdpVideoMixerGetAttributeValues vdp_video_mixer_get_attribute_values;
VdpVideoMixerQueryFeatureSupport vdp_video_mixer_query_feature_support;
VdpVideoMixerQueryParameterSupport vdp_video_mixer_query_parameter_support;
VdpVideoMixerQueryParameterValueRange vdp_video_mixer_query_parameter_value_range;
VdpVideoMixerQueryAttributeSupport vdp_video_mixer_query_attribute_support;
VdpVideoMixerQueryAttributeValueRange vdp_video_mixer_query_attribute_value_range;
VdpGenerateCSCMatrix vdp_generate_csc_matrix;

VdpDecoderCreate vdp_decoder_create;
VdpDecoderGetParameters vdp_decoder_get_parameters;
VdpDecoderRender vdp_decoder_render;
VdpDecoderQueryCapabilities vdp_decoder_query_capabilities;

VdpBitmapSurfaceCreate vdp_bitmap_surface_create;
VdpBitmapSurfaceGetParameters vdp_bitmap_surface_get_parameters;
VdpBitmapSurfacePutBitsNative vdp_bitmap_surface_put_bits_native;
VdpBitmapSurfaceQueryCapabilities vdp_bitmap_surface_query_capabilities;

#endif
