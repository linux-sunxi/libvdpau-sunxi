/*
 * Copyright (c) 2015 Andreas Baierl <ichgeh@imkreisrum.de>
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

#ifndef __VDPAU_LOG_H__
#define __VDPAU_LOG_H__

#define LFATAL	(1)
#define LERR	(2)
#define LWARN	(3)
#define LINFO	(4)
#define LDBG	(5)

#ifdef DEBUG
#ifndef DEBUG_LEVEL
#define DEBUG_LEVEL LDBG
#endif
#include <stdio.h>

#define VDPAU_LOG(level, ...) do { \
				if (level <= DEBUG_LEVEL) \
				{ \
				fprintf(stderr, "[VDPAU"); \
					switch (level) { \
					case LFATAL: \
					    fprintf(stderr, " FATAL  ] "); \
					    break; \
					case LERR: \
					    fprintf(stderr, " ERROR  ] "); \
					    break; \
					case LWARN: \
					    fprintf(stderr, " WARNING] "); \
					    break; \
					case LINFO: \
					    fprintf(stderr, " INFO   ] "); \
					    break; \
					case LDBG: \
					    fprintf(stderr, " DEBUG  ] "); \
					    break; \
					default: \
					    fprintf(stderr, "        ] "); \
					    break; \
					} \
				fprintf(stderr, __VA_ARGS__); \
				fprintf(stderr, "\n"); \
				} \
			} while (0)
#else
#define VDPAU_LOG(level, ...)
#endif
#endif
