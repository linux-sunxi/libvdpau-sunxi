/*
 * Copyright (c) 2015-2016 Andreas Baierl <ichgeh@imkreisrum.de>
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

#ifndef __CSC_H__
#define __CSC_H__

#include "vdpau_private.h"

typedef float csc_m[3][4];

#ifdef CSC_FULL_RANGE
/*
 * This matrizes are from vl_csc.c from the mesa project
 * The calculation routines are there, too.
 */

/* Full range: RGB is in 0~255 */
static const csc_m cs_bt601 = {
	{ 1.164f,  0.0f,     1.596f,   0.0f, },
	{ 1.164f,  -0.391f,  -0.813f,  0.0f, },
	{ 1.164f,  2.018f,   0.0f,     0.0f, }
};
static const csc_m cs_bt709 = {
	{ 1.164f,  0.0f,     1.793f,   0.0f, },
	{ 1.164f,  -0.213f,  -0.534f,  0.0f, },
	{ 1.164f,  2.115f,   0.0f,     0.0f, }
};
static const csc_m cs_smpte_240m = {
	{ 1.164f,  0.0f,     1.794f,   0.0f, },
	{ 1.164f,  -0.258f,  -0.543f,  0.0f, },
	{ 1.164f,  2.079f,   0.0f,     0.0f, }
};
const float ybias = -16.0f / 255.0f;
#else
/* Normal range: RGB is in 16~235 */
static const csc_m cs_bt601 = {
	{ 1.0f,    0.0f,     1.371f,   0.0f, },
	{ 1.0f,    -0.336f,  -0.698f,  0.0f, },
	{ 1.0f,    1.732f,   0.0f,     0.0f, }
};
static const csc_m cs_bt709 = {
	{ 1.0f,    0.0f,     1.540f,   0.0f, },
	{ 1.0f,    -0.183f,  -0.459f,  0.0f, },
	{ 1.0f,    1.816f,   0.0f,     0.0f, }
};
static const csc_m cs_smpte_240m = {
	{ 1.0f,    0.0f,     1.582f,   0.0f, },
	{ 1.0f,    -0.228f,  -0.478f,  0.0f, },
	{ 1.0f,    1.833f,   0.0f,     0.0f, }
};
static const float ybias = 0.0f;
#endif
const float cbbias = -128.0f / 255.0f;
const float crbias = -128.0f / 255.0f;

#endif
