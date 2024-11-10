# rpicam-apps
This is a small suite of libcamera-based applications to drive the cameras on a Raspberry Pi platform.

>[!WARNING]
>These applications and libraries have been renamed from `libcamera-*` to `rpicam-*`. Symbolic links are installed to allow users to keep using the old application names, but these will be deprecated soon. Users are encouraged to adopt the new application and library names as soon as possible.

Build
-----
For usage and build instructions, see the official Raspberry Pi documentation pages [here.](https://www.raspberrypi.com/documentation/computers/camera_software.html#building-libcamera-and-rpicam-apps)        

How to build
-----
기본적으로 위 링크의 설명을 따라하면 된다.  
주의할 점은     
1. clone하는 링크를 본 레포지토리로
2. meson setup 시 `meson setup build -Denable_libav=enabled -Denable_drm=enabled -Denable_egl=enabled -Denable_qt=enabled -Denable_opencv=enabled -Denable_tflite=disabled -Denable_hailo=disabled`으로 
3. 컴파일 시 `meson compile -C build -j 1`으로
4. rpicam-still을 실행한다.

https://docs.google.com/presentation/d/1AZBrW1k9EJjtDUi_Hu6-GYSPejTnApzl8qoSITEDNNA/edit#slide=id.g30b5154426f_0_106

License
-------

The source code is made available under the simplified [BSD 2-Clause license](https://spdx.org/licenses/BSD-2-Clause.html).

Status
------

[![ToT libcamera build/run test](https://github.com/raspberrypi/rpicam-apps/actions/workflows/rpicam-test.yml/badge.svg)](https://github.com/raspberrypi/rpicam-apps/actions/workflows/rpicam-test.yml)
