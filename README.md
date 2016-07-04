# About

libvdpau-sunxi is a [VDPAU] (ftp://download.nvidia.com/XFree86/vdpau/doxygen/html/index.html) backend driver
for Allwinner based (sunxi) SoCs.

It is based on the [reverse engineering effort] (http://linux-sunxi.org/Cedrus) of the [linux-sunxi] (http://linux-sunxi.org) community.
It does neither depend on code, which was released by Allwinner, nor does it act like a wrapper around some precompiled binary libraries.
libvdpau-sunxi is a clean implementation, that is based on reverse engineering.

It currently supports decoding of MPEG1 and MPEG2, some limited MPEG4 types and H.264. On H3/A64 it also decodes H.265.
It also supports all the basic features of the VDPAU API - including presentation.
As this is **W**ork**I**n**P**rogress, not all features are implemented yet.
Some of them probably will never get fully supported due to hardware specific limitations.

# Requirements:

* libvdpau >= 1.1
* libcedrus (https://github.com/linux-sunxi/libcedrus)
* pixman (http://www.pixman.org)

# Installation:
```
$ make
$ make install
```

# Usage:
```
$ export VDPAU_DRIVER=sunxi
$ mpv --vo=vdpau --hwdec=vdpau --hwdec-codecs=all [filename]
```

Note: Make sure that you have write access to both `/dev/disp` and `/dev/cedar_dev`

# OSD Support:

OSD support is available either 
* via G2D mixer processor (hardware accelerated) on A10/A20 or
* via pixman (CPU/Neon based) on H3/A33/A80/A64.

To enable OSD support for e.g. subtitles or GUI, set VDPAU_OSD environment variable to 1:
```
$ export VDPAU_OSD=1
```

To disable G2D mixer processor usage (for debugging purposes and forcing pixman usage on A10/A20), set VDPAU_DISABLE_G2D environment variable to 1:
```
$ export VDPAU_DISABLE_G2D=1
```

If using G2D (A10/A20), make sure to have write access to `/dev/g2d`.

# Limitations:

* Output bypasses X video driver by opening own disp layers. You can't use Xv from fbturbo at the same time, and on H3 the video is always on top and can't be overlapped by other windows.
* OSD partly breaks X11 integration due to hardware limitations. The video area can't be overlapped by other windows. For fullscreen use this is no problem.
* There is no [OpenGL interoperation feature] (https://www.opengl.org/registry/specs/NV/vdpau_interop.txt) because we are on ARM and only have OpenGL/ES available.
