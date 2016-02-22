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

#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <cedrus/cedrus.h>
#include "vdpau_private.h"

VdpStatus vdp_imp_device_create_x11(Display *display,
                                    int screen,
                                    VdpDevice *device,
                                    VdpGetProcAddress **get_proc_address)
{
	if (!display || !device || !get_proc_address)
		return VDP_STATUS_INVALID_POINTER;

	device_ctx_t *dev = handle_create(sizeof(*dev), device);
	if (!dev)
		return VDP_STATUS_RESOURCES;

	dev->display = XOpenDisplay(XDisplayString(display));
	dev->screen = screen;

	dev->cedrus = cedrus_open();
	if (!dev->cedrus)
	{
		XCloseDisplay(dev->display);
		handle_destroy(*device);
		return VDP_STATUS_ERROR;
	}

	VDPAU_DBG("VE version 0x%04x opened", cedrus_get_ve_version(dev->cedrus));
	*get_proc_address = vdp_get_proc_address;

	char *env_vdpau_osd = getenv("VDPAU_OSD");
	char *env_vdpau_g2d = getenv("VDPAU_DISABLE_G2D");
	if (env_vdpau_osd && strncmp(env_vdpau_osd, "1", 1) == 0)
		dev->osd_enabled = 1;
	else
	{
		VDPAU_DBG("OSD disabled!");
		return VDP_STATUS_OK;
	}

	if (!env_vdpau_g2d || strncmp(env_vdpau_g2d, "1", 1) !=0)
	{
		dev->g2d_fd = open("/dev/g2d", O_RDWR);
		if (dev->g2d_fd != -1)
		{
			dev->g2d_enabled = 1;
			VDPAU_DBG("OSD enabled, using G2D!");
		}
	}

	if (!dev->g2d_enabled)
		VDPAU_DBG("OSD enabled, using pixman");

	return VDP_STATUS_OK;
}

VdpStatus vdp_device_destroy(VdpDevice device)
{
	device_ctx_t *dev = handle_get(device);
	if (!dev)
		return VDP_STATUS_INVALID_HANDLE;

	if (dev->g2d_enabled)
		close(dev->g2d_fd);
	cedrus_close(dev->cedrus);
	XCloseDisplay(dev->display);

	handle_destroy(device);

	return VDP_STATUS_OK;
}

VdpStatus vdp_preemption_callback_register(VdpDevice device,
                                           VdpPreemptionCallback callback,
                                           void *context)
{
	if (!callback)
		return VDP_STATUS_INVALID_POINTER;

	device_ctx_t *dev = handle_get(device);
	if (!dev)
		return VDP_STATUS_INVALID_HANDLE;

	dev->preemption_callback = callback;
	dev->preemption_callback_context = context;

	return VDP_STATUS_OK;
}

static void *const functions[] =
{
	[VDP_FUNC_ID_GET_ERROR_STRING]                                      = vdp_get_error_string,
	[VDP_FUNC_ID_GET_PROC_ADDRESS]                                      = vdp_get_proc_address,
	[VDP_FUNC_ID_GET_API_VERSION]                                       = vdp_get_api_version,
	[VDP_FUNC_ID_GET_INFORMATION_STRING]                                = vdp_get_information_string,
	[VDP_FUNC_ID_DEVICE_DESTROY]                                        = vdp_device_destroy,
	[VDP_FUNC_ID_GENERATE_CSC_MATRIX]                                   = vdp_generate_csc_matrix,
	[VDP_FUNC_ID_VIDEO_SURFACE_QUERY_CAPABILITIES]                      = vdp_video_surface_query_capabilities,
	[VDP_FUNC_ID_VIDEO_SURFACE_QUERY_GET_PUT_BITS_Y_CB_CR_CAPABILITIES] = vdp_video_surface_query_get_put_bits_y_cb_cr_capabilities,
	[VDP_FUNC_ID_VIDEO_SURFACE_CREATE]                                  = vdp_video_surface_create,
	[VDP_FUNC_ID_VIDEO_SURFACE_DESTROY]                                 = vdp_video_surface_destroy,
	[VDP_FUNC_ID_VIDEO_SURFACE_GET_PARAMETERS]                          = vdp_video_surface_get_parameters,
	[VDP_FUNC_ID_VIDEO_SURFACE_GET_BITS_Y_CB_CR]                        = vdp_video_surface_get_bits_y_cb_cr,
	[VDP_FUNC_ID_VIDEO_SURFACE_PUT_BITS_Y_CB_CR]                        = vdp_video_surface_put_bits_y_cb_cr,
	[VDP_FUNC_ID_OUTPUT_SURFACE_QUERY_CAPABILITIES]                     = vdp_output_surface_query_capabilities,
	[VDP_FUNC_ID_OUTPUT_SURFACE_QUERY_GET_PUT_BITS_NATIVE_CAPABILITIES] = vdp_output_surface_query_get_put_bits_native_capabilities,
	[VDP_FUNC_ID_OUTPUT_SURFACE_QUERY_PUT_BITS_INDEXED_CAPABILITIES]    = vdp_output_surface_query_put_bits_indexed_capabilities,
	[VDP_FUNC_ID_OUTPUT_SURFACE_QUERY_PUT_BITS_Y_CB_CR_CAPABILITIES]    = vdp_output_surface_query_put_bits_y_cb_cr_capabilities,
	[VDP_FUNC_ID_OUTPUT_SURFACE_CREATE]                                 = vdp_output_surface_create,
	[VDP_FUNC_ID_OUTPUT_SURFACE_DESTROY]                                = vdp_output_surface_destroy,
	[VDP_FUNC_ID_OUTPUT_SURFACE_GET_PARAMETERS]                         = vdp_output_surface_get_parameters,
	[VDP_FUNC_ID_OUTPUT_SURFACE_GET_BITS_NATIVE]                        = vdp_output_surface_get_bits_native,
	[VDP_FUNC_ID_OUTPUT_SURFACE_PUT_BITS_NATIVE]                        = vdp_output_surface_put_bits_native,
	[VDP_FUNC_ID_OUTPUT_SURFACE_PUT_BITS_INDEXED]                       = vdp_output_surface_put_bits_indexed,
	[VDP_FUNC_ID_OUTPUT_SURFACE_PUT_BITS_Y_CB_CR]                       = vdp_output_surface_put_bits_y_cb_cr,
	[VDP_FUNC_ID_BITMAP_SURFACE_QUERY_CAPABILITIES]                     = vdp_bitmap_surface_query_capabilities,
	[VDP_FUNC_ID_BITMAP_SURFACE_CREATE]                                 = vdp_bitmap_surface_create,
	[VDP_FUNC_ID_BITMAP_SURFACE_DESTROY]                                = vdp_bitmap_surface_destroy,
	[VDP_FUNC_ID_BITMAP_SURFACE_GET_PARAMETERS]                         = vdp_bitmap_surface_get_parameters,
	[VDP_FUNC_ID_BITMAP_SURFACE_PUT_BITS_NATIVE]                        = vdp_bitmap_surface_put_bits_native,
	[VDP_FUNC_ID_OUTPUT_SURFACE_RENDER_OUTPUT_SURFACE]                  = vdp_output_surface_render_output_surface,
	[VDP_FUNC_ID_OUTPUT_SURFACE_RENDER_BITMAP_SURFACE]                  = vdp_output_surface_render_bitmap_surface,
	[VDP_FUNC_ID_OUTPUT_SURFACE_RENDER_VIDEO_SURFACE_LUMA]              = NULL,
	[VDP_FUNC_ID_DECODER_QUERY_CAPABILITIES]                            = vdp_decoder_query_capabilities,
	[VDP_FUNC_ID_DECODER_CREATE]                                        = vdp_decoder_create,
	[VDP_FUNC_ID_DECODER_DESTROY]                                       = vdp_decoder_destroy,
	[VDP_FUNC_ID_DECODER_GET_PARAMETERS]                                = vdp_decoder_get_parameters,
	[VDP_FUNC_ID_DECODER_RENDER]                                        = vdp_decoder_render,
	[VDP_FUNC_ID_VIDEO_MIXER_QUERY_FEATURE_SUPPORT]                     = vdp_video_mixer_query_feature_support,
	[VDP_FUNC_ID_VIDEO_MIXER_QUERY_PARAMETER_SUPPORT]                   = vdp_video_mixer_query_parameter_support,
	[VDP_FUNC_ID_VIDEO_MIXER_QUERY_ATTRIBUTE_SUPPORT]                   = vdp_video_mixer_query_attribute_support,
	[VDP_FUNC_ID_VIDEO_MIXER_QUERY_PARAMETER_VALUE_RANGE]               = vdp_video_mixer_query_parameter_value_range,
	[VDP_FUNC_ID_VIDEO_MIXER_QUERY_ATTRIBUTE_VALUE_RANGE]               = vdp_video_mixer_query_attribute_value_range,
	[VDP_FUNC_ID_VIDEO_MIXER_CREATE]                                    = vdp_video_mixer_create,
	[VDP_FUNC_ID_VIDEO_MIXER_SET_FEATURE_ENABLES]                       = vdp_video_mixer_set_feature_enables,
	[VDP_FUNC_ID_VIDEO_MIXER_SET_ATTRIBUTE_VALUES]                      = vdp_video_mixer_set_attribute_values,
	[VDP_FUNC_ID_VIDEO_MIXER_GET_FEATURE_SUPPORT]                       = vdp_video_mixer_get_feature_support,
	[VDP_FUNC_ID_VIDEO_MIXER_GET_FEATURE_ENABLES]                       = vdp_video_mixer_get_feature_enables,
	[VDP_FUNC_ID_VIDEO_MIXER_GET_PARAMETER_VALUES]                      = vdp_video_mixer_get_parameter_values,
	[VDP_FUNC_ID_VIDEO_MIXER_GET_ATTRIBUTE_VALUES]                      = vdp_video_mixer_get_attribute_values,
	[VDP_FUNC_ID_VIDEO_MIXER_DESTROY]                                   = vdp_video_mixer_destroy,
	[VDP_FUNC_ID_VIDEO_MIXER_RENDER]                                    = vdp_video_mixer_render,
	[VDP_FUNC_ID_PRESENTATION_QUEUE_TARGET_DESTROY]                     = vdp_presentation_queue_target_destroy,
	[VDP_FUNC_ID_PRESENTATION_QUEUE_CREATE]                             = vdp_presentation_queue_create,
	[VDP_FUNC_ID_PRESENTATION_QUEUE_DESTROY]                            = vdp_presentation_queue_destroy,
	[VDP_FUNC_ID_PRESENTATION_QUEUE_SET_BACKGROUND_COLOR]               = vdp_presentation_queue_set_background_color,
	[VDP_FUNC_ID_PRESENTATION_QUEUE_GET_BACKGROUND_COLOR]               = vdp_presentation_queue_get_background_color,
	[VDP_FUNC_ID_PRESENTATION_QUEUE_GET_TIME]                           = vdp_presentation_queue_get_time,
	[VDP_FUNC_ID_PRESENTATION_QUEUE_DISPLAY]                            = vdp_presentation_queue_display,
	[VDP_FUNC_ID_PRESENTATION_QUEUE_BLOCK_UNTIL_SURFACE_IDLE]           = vdp_presentation_queue_block_until_surface_idle,
	[VDP_FUNC_ID_PRESENTATION_QUEUE_QUERY_SURFACE_STATUS]               = vdp_presentation_queue_query_surface_status,
	[VDP_FUNC_ID_PREEMPTION_CALLBACK_REGISTER]                          = vdp_preemption_callback_register,
};

VdpStatus vdp_get_proc_address(VdpDevice device_handle,
                               VdpFuncId function_id,
                               void **function_pointer)
{
	if (!function_pointer)
		return VDP_STATUS_INVALID_POINTER;

	device_ctx_t *device = handle_get(device_handle);
	if (!device)
		return VDP_STATUS_INVALID_HANDLE;

	if (function_id < ARRAY_SIZE(functions))
	{
		*function_pointer = functions[function_id];
		if (*function_pointer == NULL)
			return VDP_STATUS_INVALID_FUNC_ID;
		else
			return VDP_STATUS_OK;
	}
	else if (function_id == VDP_FUNC_ID_BASE_WINSYS)
	{
		*function_pointer = &vdp_presentation_queue_target_create_x11;

		return VDP_STATUS_OK;
	}

	return VDP_STATUS_INVALID_FUNC_ID;
}

char const *vdp_get_error_string(VdpStatus status)
{
	switch (status)
	{
	case VDP_STATUS_OK:
		return "No error.";
	case VDP_STATUS_NO_IMPLEMENTATION:
		return "No backend implementation could be loaded.";
	case VDP_STATUS_DISPLAY_PREEMPTED:
		return "The display was preempted, or a fatal error occurred. The application must re-initialize VDPAU.";
	case VDP_STATUS_INVALID_HANDLE:
		return "An invalid handle value was provided.";
	case VDP_STATUS_INVALID_POINTER:
		return "An invalid pointer was provided.";
	case VDP_STATUS_INVALID_CHROMA_TYPE:
		return "An invalid/unsupported VdpChromaType value was supplied.";
	case VDP_STATUS_INVALID_Y_CB_CR_FORMAT:
		return "An invalid/unsupported VdpYCbCrFormat value was supplied.";
	case VDP_STATUS_INVALID_RGBA_FORMAT:
		return "An invalid/unsupported VdpRGBAFormat value was supplied.";
	case VDP_STATUS_INVALID_INDEXED_FORMAT:
		return "An invalid/unsupported VdpIndexedFormat value was supplied.";
	case VDP_STATUS_INVALID_COLOR_STANDARD:
		return "An invalid/unsupported VdpColorStandard value was supplied.";
	case VDP_STATUS_INVALID_COLOR_TABLE_FORMAT:
		return "An invalid/unsupported VdpColorTableFormat value was supplied.";
	case VDP_STATUS_INVALID_BLEND_FACTOR:
		return "An invalid/unsupported VdpOutputSurfaceRenderBlendFactor value was supplied.";
	case VDP_STATUS_INVALID_BLEND_EQUATION:
		return "An invalid/unsupported VdpOutputSurfaceRenderBlendEquation value was supplied.";
	case VDP_STATUS_INVALID_FLAG:
		return "An invalid/unsupported flag value/combination was supplied.";
	case VDP_STATUS_INVALID_DECODER_PROFILE:
		return "An invalid/unsupported VdpDecoderProfile value was supplied.";
	case VDP_STATUS_INVALID_VIDEO_MIXER_FEATURE:
		return "An invalid/unsupported VdpVideoMixerFeature value was supplied.";
	case VDP_STATUS_INVALID_VIDEO_MIXER_PARAMETER:
		return "An invalid/unsupported VdpVideoMixerParameter value was supplied.";
	case VDP_STATUS_INVALID_VIDEO_MIXER_ATTRIBUTE:
		return "An invalid/unsupported VdpVideoMixerAttribute value was supplied.";
	case VDP_STATUS_INVALID_VIDEO_MIXER_PICTURE_STRUCTURE:
		return "An invalid/unsupported VdpVideoMixerPictureStructure value was supplied.";
	case VDP_STATUS_INVALID_FUNC_ID:
		return "An invalid/unsupported VdpFuncId value was supplied.";
	case VDP_STATUS_INVALID_SIZE:
		return "The size of a supplied object does not match the object it is being used with.";
	case VDP_STATUS_INVALID_VALUE:
		return "An invalid/unsupported value was supplied.";
	case VDP_STATUS_INVALID_STRUCT_VERSION:
		return "An invalid/unsupported structure version was specified in a versioned structure.";
	case VDP_STATUS_RESOURCES:
		return "The system does not have enough resources to complete the requested operation at this time.";
	case VDP_STATUS_HANDLE_DEVICE_MISMATCH:
		return "The set of handles supplied are not all related to the same VdpDevice.";
	case VDP_STATUS_ERROR:
		return "A catch-all error, used when no other error code applies.";
	default:
		return "Unknown Error";
   }
}

VdpStatus vdp_get_api_version(uint32_t *api_version)
{
	if (!api_version)
		return VDP_STATUS_INVALID_POINTER;

	*api_version = 1;
	return VDP_STATUS_OK;
}

VdpStatus vdp_get_information_string(char const **information_string)
{
	if (!information_string)
		return VDP_STATUS_INVALID_POINTER;

	*information_string = "sunxi VDPAU Driver";
	return VDP_STATUS_OK;
}

