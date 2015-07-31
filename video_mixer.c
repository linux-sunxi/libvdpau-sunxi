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
#include <string.h>
#include "vdpau_private.h"
#include "ve.h"
#include "rgba.h"
#include "csc.h"

/*
 * Make a global variable, that inherits the actual color standard.
 * This is written within vdp_generate_csc_matrix and read in set_csc_matrix.
 */
static VdpColorStandard color_standard;

static void cleanup_video_mixer(void *ptr, void *meta)
{
	mixer_ctx_t *mixer = ptr;

	sfree(mixer->device);
}

VdpStatus vdp_video_mixer_create(VdpDevice device,
                                 uint32_t feature_count,
                                 VdpVideoMixerFeature const *features,
                                 uint32_t parameter_count,
                                 VdpVideoMixerParameter const *parameters,
                                 void const *const *parameter_values,
                                 VdpVideoMixer *mixer)
{
	smart device_ctx_t *dev = handle_get(device);
	if (!dev)
		return VDP_STATUS_INVALID_HANDLE;

	smart mixer_ctx_t *mix = handle_alloc(sizeof(*mix), cleanup_video_mixer);
	if (!mix)
		return VDP_STATUS_RESOURCES;

	mix->device = sref(dev);
	mix->brightness = 0.0;
	mix->contrast = 1.0;
	mix->saturation = 1.0;
	mix->hue = 0.0;
	mix->start_stream = 1;

	/* CSC: Use BT601 at initalization time */
	mix->custom_csc = 0;
	color_standard = VDP_COLOR_STANDARD_ITUR_BT_601;
	vdp_generate_csc_matrix(NULL, color_standard, &mix->csc_matrix);
	set_csc_matrix(mix, color_standard);

	int i;

	if (mix->device->flags & DEVICE_FLAG_DEINT)
	{
		for (i = 0; i < feature_count; i++)
		{
			switch (features[i])
			{
			case VDP_VIDEO_MIXER_FEATURE_DEINTERLACE_TEMPORAL:
				mix->deinterlace = 1;
				break;
			}
		}
	}

	return handle_create(mixer, mix);
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
	smart mixer_ctx_t *mix = handle_get(mixer);
	if (!mix)
		return VDP_STATUS_INVALID_HANDLE;

	if (background_surface != VDP_INVALID_HANDLE)
		VDPAU_LOG(LINFO, "Requested unimplemented background_surface");

	smart output_surface_ctx_t *os = handle_get(destination_surface);
	if (!os)
		return VDP_STATUS_INVALID_HANDLE;

	if (os->yuv)
		yuv_unref(os->yuv);

	sfree(os->vs);
	os->vs = handle_get(video_surface_current);
	if (!(os->vs))
		return VDP_STATUS_INVALID_HANDLE;

	os->yuv = yuv_ref(os->vs->yuv);

	if (mix->device->flags & DEVICE_FLAG_DEINT)
	{
		os->video_deinterlace = (current_picture_structure == VDP_VIDEO_MIXER_PICTURE_STRUCTURE_FRAME ? 0 : 1);
		os->video_field = current_picture_structure;
	}

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

	/*
	 * If we don't do this, we possibly overwrite os->vs->start_flag with a second video_mixer_render
	 * and the presentation routine will never be restarted.
	 */
	if (!os->vs->start_flag)
		os->vs->start_flag = mix->start_stream;
	mix->start_stream = 0;
	mix->csc_change = 0;

	if ((mix->device->flags & DEVICE_FLAG_OSD) && (os->rgba.flags & RGBA_FLAG_DIRTY))
		os->rgba.flags |= RGBA_FLAG_NEEDS_CLEAR;

	if (layer_count != 0)
		VDPAU_LOG(LINFO, "Requested unimplemented additional layers");

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

	smart mixer_ctx_t *mix = handle_get(mixer);
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

	smart mixer_ctx_t *mix = handle_get(mixer);
	if (!mix)
		return VDP_STATUS_INVALID_HANDLE;

	int i;

	if (mix->device->flags & DEVICE_FLAG_DEINT)
	{
		for (i = 0; i < feature_count; i++)
		{
			switch (features[i])
			{
			case VDP_VIDEO_MIXER_FEATURE_DEINTERLACE_TEMPORAL:
				mix->deinterlace = feature_enables[i];
				break;
			}
		}
	}

	return VDP_STATUS_OK;
}

VdpStatus vdp_video_mixer_get_feature_enables(VdpVideoMixer mixer,
                                              uint32_t feature_count,
                                              VdpVideoMixerFeature const *features,
                                              VdpBool *feature_enables)
{
	if (!features || !feature_enables)
		return VDP_STATUS_INVALID_POINTER;

	smart mixer_ctx_t *mix = handle_get(mixer);
	if (!mix)
		return VDP_STATUS_INVALID_HANDLE;

	int i;

	if (mix->device->flags & DEVICE_FLAG_DEINT)
	{
		for (i = 0; i < feature_count; i++)
		{
			switch (features[i])
			{
			case VDP_VIDEO_MIXER_FEATURE_DEINTERLACE_TEMPORAL:
				feature_enables[i] = mix->deinterlace;
				break;
			}
		}
	}

	return VDP_STATUS_OK;
}

void set_csc_matrix(mixer_ctx_t *mix, VdpColorStandard standard)
{
	float asin;
	static const csc_m *cstd;

	mix->csc_change = 1;
	switch (standard) {
		case VDP_COLOR_STANDARD_ITUR_BT_709:
			cstd = &cs_bt709;
			break;
		case VDP_COLOR_STANDARD_SMPTE_240M:
			cstd = &cs_smpte_240m;
			break;
		case VDP_COLOR_STANDARD_ITUR_BT_601:
		default:
			cstd = &cs_bt601;
			break;
	}
	VdpCSCMatrix *matrix = &mix->csc_matrix;

	if ((*matrix)[1][0] == 0 && (*matrix)[1][1] == 0 && (*matrix)[1][2] == 0)
	{
		/* At least contrast was 0.0f. Set Hue and saturation to default. They cannot be guessed... */
		mix->contrast = 0.0f;
		mix->hue = 0.0f;
		mix->saturation = 1.0f;
	}
	else
	{
		/* Contrast */
		mix->contrast = (*matrix)[0][0] / (*cstd)[0][0];

		if ((*matrix)[1][1] == 0 && (*matrix)[1][2] == 0)
		{
			/* Saturation was 0.0f. Set Hue to default. This cannot be guessed... */
			mix->hue = 0.0f;
			mix->saturation = 0.0f;
		}
		else
		{
			/* Hue */
			asin = asinf(sqrtf(pow(((*matrix)[1][1] * (*cstd)[1][2] - (*matrix)[1][2] * (*cstd)[1][1]), 2.0) /
			       (pow((-(*matrix)[1][1] * (*cstd)[1][1] - (*matrix)[1][2] * (*cstd)[1][2]), 2.0) +
			        pow(((*matrix)[1][1] * (*cstd)[1][2] - (*matrix)[1][2] * (*cstd)[1][1]), 2.0))));

			if (((*matrix)[2][1] < 0 && (*cstd)[2][1] < 0) || ((*matrix)[2][1] > 0 && (*cstd)[2][1] > 0))
				if (((*matrix)[0][1] < 0 && (*matrix)[0][2] > 0) || ((*matrix)[0][1] > 0 && (*matrix)[0][2] < 0))
					mix->hue = asin;
				else
					mix->hue = - asin;
			else
				if (((*matrix)[0][1] < 0 && (*matrix)[0][2] > 0) || ((*matrix)[0][1] > 0 && (*matrix)[0][2] < 0))
					mix->hue = - M_PI + asin;
				else
					mix->hue = M_PI - asin;

			/* Check, if Hue was M_PI or -M_PI */
			if ((fabs(fabs(mix->hue) - M_PI)) < 0.00001f)
				mix->hue = - mix->hue;

			/* Saturation */
			mix->saturation = (*matrix)[1][1] / (mix->contrast * ((*cstd)[1][1] * cosf(mix->hue) - (*cstd)[1][2] * sinf(mix->hue)));
		}

		/* Brightness */
		mix->brightness = ((*matrix)[1][3] -
		                  (*cstd)[1][1] * mix->contrast * mix->saturation * (cbbias * cosf(mix->hue) + crbias * sinf(mix->hue)) -
		                  (*cstd)[1][2] * mix->contrast * mix->saturation * (crbias * cosf(mix->hue) - cbbias * sinf(mix->hue)) -
		                  (*cstd)[1][3] - (*cstd)[1][0] * mix->contrast * ybias) / (*cstd)[1][0];
	}

	VDPAU_LOG(LINFO, "Setting mixer value from following color standard: %d", standard);
	VDPAU_LOG(LINFO, ">mix->bright: %2.3f, mix->contrast: %2.3f, mix->saturation: %2.3f, mix->hue: %2.3f",
	          (double)mix->brightness, (double)mix->contrast,
	          (double)mix->saturation, (double)mix->hue);
}

VdpStatus vdp_video_mixer_set_attribute_values(VdpVideoMixer mixer,
                                               uint32_t attribute_count,
                                               VdpVideoMixerAttribute const *attributes,
                                               void const *const *attribute_values)
{
	if (!attributes || !attribute_values)
		return VDP_STATUS_INVALID_POINTER;

	smart mixer_ctx_t *mix = handle_get(mixer);
	if (!mix)
		return VDP_STATUS_INVALID_HANDLE;

	uint32_t i;
	for (i = 0; i < attribute_count; i++) {
		switch (attributes[i]) {
			case VDP_VIDEO_MIXER_ATTRIBUTE_CSC_MATRIX:
				mix->custom_csc = !!attribute_values[i];
				if (!attribute_values[i])
				{
					/* CSC: Use BT601 if not set */
					color_standard = VDP_COLOR_STANDARD_ITUR_BT_601;
					vdp_generate_csc_matrix(NULL, color_standard, &mix->csc_matrix);
				}
				else
					memcpy(mix->csc_matrix, attribute_values[i], sizeof(mix->csc_matrix));
				set_csc_matrix(mix, color_standard);
				break;
			default:
				return VDP_STATUS_INVALID_VIDEO_MIXER_ATTRIBUTE;
		}
	}

	return VDP_STATUS_OK;
}

VdpStatus vdp_video_mixer_get_parameter_values(VdpVideoMixer mixer,
                                               uint32_t parameter_count,
                                               VdpVideoMixerParameter const *parameters,
                                               void *const *parameter_values)
{
	if (!parameters || !parameter_values)
		return VDP_STATUS_INVALID_POINTER;

	smart mixer_ctx_t *mix = handle_get(mixer);
	if (!mix)
		return VDP_STATUS_INVALID_HANDLE;

	return VDP_STATUS_ERROR;
}

VdpStatus vdp_video_mixer_get_attribute_values(VdpVideoMixer mixer,
                                               uint32_t attribute_count,
                                               VdpVideoMixerAttribute const *attributes,
                                               void *const *attribute_values)
{
	int i;
	VdpCSCMatrix **vdp_csc;
	
	if (!attributes || !attribute_values)
		return VDP_STATUS_INVALID_POINTER;

	smart mixer_ctx_t *mix = handle_get(mixer);
	if (!mix)
		return VDP_STATUS_INVALID_HANDLE;

	for (i = 0; i < attribute_count; i++) {
		switch (attributes[i])
		{
			case VDP_VIDEO_MIXER_ATTRIBUTE_CSC_MATRIX:
				vdp_csc = attribute_values[i];
				if (!mix->custom_csc)
					*vdp_csc = NULL;
				else
					memcpy(*vdp_csc, mix->csc_matrix, sizeof(VdpCSCMatrix));
				break;
			case VDP_VIDEO_MIXER_ATTRIBUTE_BACKGROUND_COLOR:
			case VDP_VIDEO_MIXER_ATTRIBUTE_NOISE_REDUCTION_LEVEL:
			case VDP_VIDEO_MIXER_ATTRIBUTE_LUMA_KEY_MIN_LUMA:
			case VDP_VIDEO_MIXER_ATTRIBUTE_LUMA_KEY_MAX_LUMA:
			case VDP_VIDEO_MIXER_ATTRIBUTE_SHARPNESS_LEVEL:
			case VDP_VIDEO_MIXER_ATTRIBUTE_SKIP_CHROMA_DEINTERLACE:
				break;
			default:
				return VDP_STATUS_INVALID_VIDEO_MIXER_ATTRIBUTE;
		}
	}

	return VDP_STATUS_OK;
}

VdpStatus vdp_video_mixer_query_feature_support(VdpDevice device,
                                                VdpVideoMixerFeature feature,
                                                VdpBool *is_supported)
{
	if (!is_supported)
		return VDP_STATUS_INVALID_POINTER;

	smart device_ctx_t *dev = handle_get(device);
	if (!dev)
		return VDP_STATUS_INVALID_HANDLE;

	switch (feature)
	{
	case VDP_VIDEO_MIXER_FEATURE_DEINTERLACE_TEMPORAL:
		*is_supported = VDP_TRUE;
		break;
	default:
		*is_supported = VDP_FALSE;
	}

	return VDP_STATUS_OK;
}

VdpStatus vdp_video_mixer_query_parameter_support(VdpDevice device,
                                                  VdpVideoMixerParameter parameter,
                                                  VdpBool *is_supported)
{
	if (!is_supported)
		return VDP_STATUS_INVALID_POINTER;

	smart device_ctx_t *dev = handle_get(device);
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

	smart device_ctx_t *dev = handle_get(device);
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

	smart device_ctx_t *dev = handle_get(device);
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

	smart device_ctx_t *dev = handle_get(device);
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
	if (!csc_matrix)
		return VDP_STATUS_INVALID_POINTER;

	if (procamp && procamp->struct_version > VDP_PROCAMP_VERSION)
		return VDP_STATUS_INVALID_STRUCT_VERSION;

	static const csc_m *cstd;
	color_standard = standard;

	switch (standard) {
		case VDP_COLOR_STANDARD_ITUR_BT_709:
			cstd = &cs_bt709;
			break;
		case VDP_COLOR_STANDARD_SMPTE_240M:
			cstd = &cs_smpte_240m;
			break;
		case VDP_COLOR_STANDARD_ITUR_BT_601:
		default:
			cstd = &cs_bt601;
			break;
	}

	float b = procamp ? procamp->brightness : 0.0f;
	float c = procamp ? procamp->contrast : 1.0f;
	float s = procamp ? procamp->saturation : 1.0f;
	float h = procamp ? procamp->hue : 0.0f;

	int i;
	for (i = 0; i < 3; i++) {
		(*csc_matrix)[i][0] = c * (*cstd)[i][0];
		(*csc_matrix)[i][1] = c * (*cstd)[i][1] * s * cosf(h) - c * (*cstd)[i][2] * s * sinf(h);
		(*csc_matrix)[i][2] = c * (*cstd)[i][2] * s * cosf(h) + c * (*cstd)[i][1] * s * sinf(h);
		(*csc_matrix)[i][3] = (*cstd)[i][3] + (*cstd)[i][0] * (b + c * ybias) +
		                      (*cstd)[i][1] * (c * cbbias * s * cosf(h) + c * crbias * s * sinf(h)) +
		                      (*cstd)[i][2] * (c * crbias * s * cosf(h) - c * cbbias * s * sinf(h));
	}

	VDPAU_LOG(LINFO, "Generate CSC matrix from following color standard: %d", standard);
	VDPAU_LOG(LINFO, ">procamp->bright: %2.3f, procamp->contrast: %2.3f, procamp->saturation: %2.3f, procamp->hue: %2.3f",
	          (double)b, (double)c, (double)s, (double)h);

	return VDP_STATUS_OK;
}
