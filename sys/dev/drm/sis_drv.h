/* sis_drv.h -- Private header for sis driver -*- linux-c -*-
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 * 
 * $FreeBSD$
 */

#ifndef _SIS_DRV_H_
#define _SIS_DRV_H_

#include "dev/drm/sis_ds.h"

typedef struct drm_sis_private {
	drm_map_t *buffers;

	memHeap_t *AGPHeap;
	memHeap_t *FBHeap;
} drm_sis_private_t;

extern int sis_fb_alloc( DRM_IOCTL_ARGS );
extern int sis_fb_free( DRM_IOCTL_ARGS );
extern int sis_ioctl_agp_init( DRM_IOCTL_ARGS );
extern int sis_ioctl_agp_alloc( DRM_IOCTL_ARGS );
extern int sis_ioctl_agp_free( DRM_IOCTL_ARGS );
extern int sis_fb_init( DRM_IOCTL_ARGS );

#endif
