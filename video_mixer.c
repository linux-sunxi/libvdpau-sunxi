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

#include <math.h>
#include <cedrus/cedrus.h>
#include "vdpau_private.h"
#include "rgba.h"

VdpStatus vdp_video_mixer_create(VdpDevice device,
                                 uint32_t feature_count,
                                 VdpVideoMixerFeature const *features,
                                 uint32_t parameter_count,
                                 VdpVideoMixerParameter const *parameters,
                                 void const *const *parameter_values,
                                 VdpVideoMixer *mixer)
{
	device_ctx_t *dev = handle_get(device);
	if (!dev)
		return VDP_STATUS_INVALID_HANDLE;

	mixer_ctx_t *mix = handle_create(sizeof(*mix), mixer);
	if (!mix)
		return VDP_STATUS_RESOURCES;

	mix->device = dev;
	mix->contrast = 1.0;
	mix->saturation = 1.0;

	return VDP_STATUS_OK;
}

VdpStatus vdp_video_mixer_destroy(VdpVideoMixer mixer)
{
	mixer_ctx_t *mix = handle_get(mixer);
	if (!mix)
		return VDP_STATUS_INVALID_HANDLE;

	handle_destroy(mixer);

	return VDP_STATUS_OK;
}

VdpStatus vdp_video_mixer_render(VdpVideoMixer mixer,
                                 VdpOutputSurface background_surface,
                                 VdpRect const *background_source_rect,
                                 VdpVideoMixerPictureStructure current_picture_structure,
                                 uint32_t video_surface_past_count,
                                 VdpVideoSurface const *video_surface_past,
                                 VdpVideoSurface video_surface_current,
                                 uint32_t video_surface_future_count,
                                 VdpVideoSurface const *video_surface_future,
                                 VdpRect const *video_source_rect,
                                 VdpOutputSurface destination_surface,
                                 VdpRect const *destination_rect,
                                 VdpRect const *destination_video_rect,
                                 uint32_t layer_count,
                                 VdpLayer const *layers)
{
	mixer_ctx_t *mix = handle_get(mixer);
	if (!mix)
		return VDP_STATUS_INVALID_HANDLE;

	if (background_surface != VDP_INVALID_HANDLE)
		VDPAU_DBG_ONCE("Requested unimplemented background_surface");


	if (current_picture_structure != VDP_VIDEO_MIXER_PICTURE_STRUCTURE_FRAME)
		VDPAU_DBG_ONCE("Requested unimplemented picture_structure");



	output_surface_ctx_t *os = handle_get(destination_surface);
	if (!os)
		return VDP_STATUS_INVALID_HANDLE;

	if (os->yuv)
		yuv_unref(os->yuv);

	os->vs = handle_get(video_surface_current);
	if (!(os->vs))
		return VDP_STATUS_INVALID_HANDLE;

	os->yuv = yuv_ref(os->vs->yuv);

	if (video_source_rect)
	{
		os->video_src_rect = *video_source_rect;
	}
	else
	{
		os->video_src_rect.x0 = os->video_src_rect.y0 = 0;
		os->video_src_rect.x1 = os->vs->width;
		os->video_src_rect.y1 = os->vs->height;
	}
	if (destination_video_rect)
	{
		os->video_dst_rect = *destination_video_rect;
	}
	else
	{
		os->video_dst_rect.x0 = os->video_dst_rect.y0 = 0;
		os->video_dst_rect.x1 = os->video_src_rect.x1 - os->video_src_rect.x0;
		os->video_dst_rect.y1 = os->video_src_rect.y1 - os->video_src_rect.y0;
	}

	os->csc_change = mix->csc_change;
	os->brightness = mix->brightness;
	os->contrast = mix->contrast;
	os->saturation = mix->saturation;
	os->hue = mix->hue;
	mix->csc_change = 0;

	if (mix->device->osd_enabled && (os->rgba.flags & RGBA_FLAG_DIRTY))
		os->rgba.flags |= RGBA_FLAG_NEEDS_CLEAR;

	if (layer_count != 0)
		VDPAU_DBG_ONCE("Requested unimplemented additional layers");


	return VDP_STATUS_OK;
}

VdpStatus vdp_video_mixer_get_feature_support(VdpVideoMixer mixer,
                                              uint32_t feature_count,
                                              VdpVideoMixerFeature const *features,
                                              VdpBool *feature_supports)
{
	if (feature_count == 0)
		return VDP_STATUS_OK;

	if (!features || !feature_supports)
		return VDP_STATUS_INVALID_POINTER;

	mixer_ctx_t *mix = handle_get(mixer);
	if (!mix)
		return VDP_STATUS_INVALID_HANDLE;


	return VDP_STATUS_ERROR;
}

VdpStatus vdp_video_mixer_set_feature_enables(VdpVideoMixer mixer,
                                              uint32_t feature_count,
                                              VdpVideoMixerFeature const *features,
                                              VdpBool const *feature_enables)
{
	if (feature_count == 0)
		return VDP_STATUS_OK;

	if (!features || !feature_enables)
		return VDP_STATUS_INVALID_POINTER;

	mixer_ctx_t *mix = handle_get(mixer);
	if (!mix)
		return VDP_STATUS_INVALID_HANDLE;


	return VDP_STATUS_OK;
}

VdpStatus vdp_video_mixer_get_feature_enables(VdpVideoMixer mixer,
                                              uint32_t feature_count,
                                              VdpVideoMixerFeature const *features,
                                              VdpBool *feature_enables)
{
	if (!features || !feature_enables)
		return VDP_STATUS_INVALID_POINTER;

	mixer_ctx_t *mix = handle_get(mixer);
	if (!mix)
		return VDP_STATUS_INVALID_HANDLE;


	return VDP_STATUS_ERROR;
}

static void set_csc_matrix(mixer_ctx_t *mix, const VdpCSCMatrix *matrix)
{
	mix->csc_change = 1;
	// default contrast for full-range has 1.0 as luma coefficients
	mix->contrast = ((*matrix)[0][0] + (*matrix)[1][0] + (*matrix)[2][0]) / 3;
	// the way brightness and contrast work with this driver, brightness
	// is the brightness of a "black" pixel
	mix->brightness = ((*matrix)[0][1] + (*matrix)[1][1] + (*matrix)[2][1]) / 2 +
	                  ((*matrix)[0][2] + (*matrix)[1][2] + (*matrix)[2][2]) / 2 +
	                  (*matrix)[0][3] + (*matrix)[1][3] + (*matrix)[2][3];
	mix->brightness /= 3;

	float sin = (*matrix)[0][1] + (*matrix)[2][2];
	float cos = (*matrix)[0][2] + (*matrix)[2][1];
	float e = 0.001;
	if (-e < cos && cos < e) mix->hue = M_PI;
	else mix->hue = atanf(sin/cos);
	mix->saturation = sqrtf(sin * sin + cos * cos) / (1.403 + 1.773);
}

VdpStatus vdp_video_mixer_set_attribute_values(VdpVideoMixer mixer,
                                               uint32_t attribute_count,
                                               VdpVideoMixerAttribute const *attributes,
                                               void const *const *attribute_values)
{
	if (!attributes || !attribute_values)
		return VDP_STATUS_INVALID_POINTER;

	mixer_ctx_t *mix = handle_get(mixer);
	if (!mix)
		return VDP_STATUS_INVALID_HANDLE;

	uint32_t i;
	for (i = 0; i < attribute_count; i++)
		if (attributes[i] == VDP_VIDEO_MIXER_ATTRIBUTE_CSC_MATRIX)
			set_csc_matrix(mix, (const VdpCSCMatrix *)attribute_values[i]);

	return VDP_STATUS_OK;
}

VdpStatus vdp_video_mixer_get_parameter_values(VdpVideoMixer mixer,
                                               uint32_t parameter_count,
                                               VdpVideoMixerParameter const *parameters,
                                               void *const *parameter_values)
{
	if (!parameters || !parameter_values)
		return VDP_STATUS_INVALID_POINTER;

	mixer_ctx_t *mix = handle_get(mixer);
	if (!mix)
		return VDP_STATUS_INVALID_HANDLE;


	return VDP_STATUS_ERROR;
}

VdpStatus vdp_video_mixer_get_attribute_values(VdpVideoMixer mixer,
                                               uint32_t attribute_count,
                                               VdpVideoMixerAttribute const *attributes,
                                               void *const *attribute_values)
{
	if (!attributes || !attribute_values)
		return VDP_STATUS_INVALID_POINTER;

	mixer_ctx_t *mix = handle_get(mixer);
	if (!mix)
		return VDP_STATUS_INVALID_HANDLE;


	return VDP_STATUS_ERROR;
}

VdpStatus vdp_video_mixer_query_feature_support(VdpDevice device,
                                                VdpVideoMixerFeature feature,
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

VdpStatus vdp_video_mixer_query_parameter_support(VdpDevice device,
                                                  VdpVideoMixerParameter parameter,
                                                  VdpBool *is_supported)
{
	if (!is_supported)
		return VDP_STATUS_INVALID_POINTER;

	device_ctx_t *dev = handle_get(device);
	if (!dev)
		return VDP_STATUS_INVALID_HANDLE;

	switch (parameter)
	{
	case VDP_VIDEO_MIXER_PARAMETER_CHROMA_TYPE:
	case VDP_VIDEO_MIXER_PARAMETER_LAYERS:
	case VDP_VIDEO_MIXER_PARAMETER_VIDEO_SURFACE_HEIGHT:
	case VDP_VIDEO_MIXER_PARAMETER_VIDEO_SURFACE_WIDTH:
		*is_supported = VDP_TRUE;
		break;
	default:
		*is_supported = VDP_FALSE;
		break;
	}

	return VDP_STATUS_OK;
}

VdpStatus vdp_video_mixer_query_parameter_value_range(VdpDevice device,
                                                      VdpVideoMixerParameter parameter,
                                                      void *min_value,
                                                      void *max_value)
{
	if (!min_value || !max_value)
		return VDP_STATUS_INVALID_POINTER;

	device_ctx_t *dev = handle_get(device);
	if (!dev)
		return VDP_STATUS_INVALID_HANDLE;

	switch (parameter)
	{
	case VDP_VIDEO_MIXER_PARAMETER_LAYERS:
		*(uint32_t *)min_value = 0;
		*(uint32_t *)max_value = 0;
		return VDP_STATUS_OK;
	case VDP_VIDEO_MIXER_PARAMETER_VIDEO_SURFACE_HEIGHT:
	case VDP_VIDEO_MIXER_PARAMETER_VIDEO_SURFACE_WIDTH:
		*(uint32_t *)min_value = 0;
		*(uint32_t *)max_value = 8192;
		return VDP_STATUS_OK;
	}

	return VDP_STATUS_ERROR;
}

VdpStatus vdp_video_mixer_query_attribute_support(VdpDevice device,
                                                  VdpVideoMixerAttribute attribute,
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

VdpStatus vdp_video_mixer_query_attribute_value_range(VdpDevice device,
                                                      VdpVideoMixerAttribute attribute,
                                                      void *min_value,
                                                      void *max_value)
{
	if (!min_value || !max_value)
		return VDP_STATUS_INVALID_POINTER;

	device_ctx_t *dev = handle_get(device);
	if (!dev)
		return VDP_STATUS_INVALID_HANDLE;

	switch (attribute)
	{
	case VDP_VIDEO_MIXER_ATTRIBUTE_BACKGROUND_COLOR:
	case VDP_VIDEO_MIXER_ATTRIBUTE_CSC_MATRIX:
		return VDP_STATUS_ERROR;

	case VDP_VIDEO_MIXER_ATTRIBUTE_LUMA_KEY_MAX_LUMA:
	case VDP_VIDEO_MIXER_ATTRIBUTE_LUMA_KEY_MIN_LUMA:
	case VDP_VIDEO_MIXER_ATTRIBUTE_NOISE_REDUCTION_LEVEL:
		*(float *)min_value = 0.0;
		*(float *)max_value = 1.0;
		return VDP_STATUS_OK;

	case VDP_VIDEO_MIXER_ATTRIBUTE_SHARPNESS_LEVEL:
		*(float *)min_value = -1.0;
		*(float *)max_value = 1.0;
		return VDP_STATUS_OK;

	case VDP_VIDEO_MIXER_ATTRIBUTE_SKIP_CHROMA_DEINTERLACE:
		*(uint8_t *)min_value = 0;
		*(uint8_t *)max_value = 1;
		return VDP_STATUS_OK;
	}

	return VDP_STATUS_ERROR;
}

VdpStatus vdp_generate_csc_matrix(VdpProcamp *procamp,
                                  VdpColorStandard standard,
                                  VdpCSCMatrix *csc_matrix)
{
	if (!csc_matrix || !procamp)
		return VDP_STATUS_INVALID_POINTER;

	if (procamp->struct_version > VDP_PROCAMP_VERSION)
		return VDP_STATUS_INVALID_STRUCT_VERSION;

	// BT.601 table
	(*csc_matrix)[0][1] =  0.000;
	(*csc_matrix)[0][2] =  1.403;

	(*csc_matrix)[1][1] = -0.344;
	(*csc_matrix)[1][2] = -0.714;

	(*csc_matrix)[2][1] =  1.773;
	(*csc_matrix)[2][2] =  0.000;

	float uvcos = procamp->saturation * cosf(procamp->hue);
	float uvsin = procamp->saturation * sinf(procamp->hue);
	int i;
	for (i = 0; i < 3; i++) {
		(*csc_matrix)[i][0] = procamp->contrast;
		float u = (*csc_matrix)[i][1] * uvcos + (*csc_matrix)[i][2] * uvsin;
		float v = (*csc_matrix)[i][1] * uvsin + (*csc_matrix)[i][2] * uvcos;
		(*csc_matrix)[i][1] = u;
		(*csc_matrix)[i][2] = v;
		(*csc_matrix)[i][3] = - (u + v) / 2;
		(*csc_matrix)[i][3] += 0.5 - procamp->contrast / 2;
		(*csc_matrix)[i][3] += procamp->brightness;
	}

	return VDP_STATUS_OK;
}
