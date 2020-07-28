find_package(PkgConfig)

set(GST_VERSION_REQ 1.4)

pkg_check_modules(GST REQUIRED gstreamer-1.0>=${GST_VERSION_REQ}
                            gstreamer-base-1.0>=${GST_VERSION_REQ}
                            gstreamer-controller-1.0>=${GST_VERSION_REQ}
                            gstreamer-video-1.0>=${GST_VERSION_REQ})

add_library( supercamera SHARED
    src/supercambufferpool.c
    src/supercamera.c
    src/supergbmallocator.c
    src/superhal3src.c
    src/superstream.c
    src/superhal3cam.c
)

# Libraries to compile
target_link_libraries(supercamera
                        ${GST_LIBRARIES}
                        gstallocators-1.0
                        dl
						log
                        camera_metadata
                        utils
                        cutils
                        gbm
                        pthread
                        )

# Compiler options
target_compile_options ( supercamera PUBLIC ${GST_CFLAGS} )

# Include Paths
target_include_directories ( supercamera PUBLIC ${GST_INCLUDE_DIRS} )

# Example of how to include custom paths using ENV variables - note that usr/include is ignored by CMAKE.
set(SUPER_STREAM_CUSTOM_INC "$ENV{PKG_CONFIG_SYSROOT_DIR}/usr/include")
target_include_directories ( supercamera PUBLIC ${SUPER_STREAM_CUSTOM_INC} )

# Custom compile defines
add_definitions(-DHAVE_CONFIG_H)

install(TARGETS supercamera LIBRARY DESTINATION /usr/lib64/gstreamer-1.0/ )
