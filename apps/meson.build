dl_dep = dependency('dl', required : true)
libcamera_dep = dependency('libcamera', required : true)
opencv_dep = dependency('opencv4', required: true)  # OpenCV 의존성 추가
curl_dep = dependency('libcurl', required: true)  # libcurl 의존성 추가

rpicam_honeybee = executable('rpicam-honeybee', files('rpicam_honeybee.cpp'),
                          include_directories : include_directories('..'),
                          dependencies: [libcamera_dep, boost_dep, opencv_dep, curl_dep],
                          link_with : rpicam_app,
                          install : true)

rpicam_still = executable('rpicam-still', files('rpicam_still.cpp'),
                          include_directories : include_directories('..'),
                          dependencies: [libcamera_dep, boost_dep],
                          link_with : rpicam_app,
                          install : true)

rpicam_vid = executable('rpicam-vid', files('rpicam_vid.cpp'),
                        include_directories : include_directories('..'),
                        dependencies: [libcamera_dep, boost_dep],
                        link_with : rpicam_app,
                        install : true)

rpicam_hello = executable('rpicam-hello', files('rpicam_hello.cpp'),
                          include_directories : include_directories('..'),
                          dependencies: libcamera_dep,
                          link_with : rpicam_app,
                          install : true)

rpicam_raw = executable('rpicam-raw', files('rpicam_raw.cpp'),
                        include_directories : include_directories('..'),
                        dependencies: [libcamera_dep, boost_dep],
                        link_with : rpicam_app,
                        install : true)

rpicam_jpeg = executable('rpicam-jpeg', files('rpicam_jpeg.cpp'),
                         include_directories : include_directories('..'),
                         dependencies: [libcamera_dep, boost_dep],
                         link_with : rpicam_app,
                         install : true)

# Install symlinks to the old app names for legacy purposes.

install_symlink('libcamera-honeybee',
                install_dir: get_option('bindir'),
                pointing_to: 'rpicam-honeybee')

install_symlink('libcamera-still',
                install_dir: get_option('bindir'),
                pointing_to: 'rpicam-still')

install_symlink('libcamera-vid',
                install_dir: get_option('bindir'),
                pointing_to: 'rpicam-vid')

install_symlink('libcamera-hello',
                install_dir: get_option('bindir'),
                pointing_to: 'rpicam-hello')

install_symlink('libcamera-raw',
                install_dir: get_option('bindir'),
                pointing_to: 'rpicam-raw')

install_symlink('libcamera-jpeg',
                install_dir: get_option('bindir'),
                pointing_to: 'rpicam-jpeg')    

if enable_tflite
    rpicam_detect = executable('rpicam-detect', files('rpicam_detect.cpp'),
                               include_directories : include_directories('..'),
                               dependencies: [libcamera_dep, boost_dep],
                               link_with : rpicam_app,
                               install : true)

    install_symlink('libcamera-detect',
                    install_dir: get_option('bindir'),
                    pointing_to: 'rpicam-detect')
endif
