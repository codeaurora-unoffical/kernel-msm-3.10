find_package(PkgConfig)

set(GST_VERSION_REQ 1.4)

pkg_check_modules(GST REQUIRED gstreamer-1.0>=${GST_VERSION_REQ}
                            gstreamer-base-1.0>=${GST_VERSION_REQ}
                            gstreamer-controller-1.0>=${GST_VERSION_REQ}
                            gstreamer-video-1.0>=${GST_VERSION_REQ})

add_library( qcomcamera SHARED
    src/qcomcambufferpool.c
    src/qcomcamera.c
    src/qcomgbmallocator.c
    src/qcomhal3src.c
    src/qcomstream.c
    src/qcomhal3cam.c
)

# Libraries to compile
target_link_libraries(qcomcamera
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
target_compile_options ( qcomcamera PUBLIC ${GST_CFLAGS} )

# Include Paths
target_include_directories ( qcomcamera PUBLIC ${GST_INCLUDE_DIRS} )

# Example of how to include custom paths using ENV variables - note that usr/include is ignored by CMAKE.
set(QCOM_STREAM_CUSTOM_INC "$ENV{PKG_CONFIG_SYSROOT_DIR}/usr/include")
target_include_directories ( qcomcamera PUBLIC ${QCOM_STREAM_CUSTOM_INC} )

# Custom compile defines
add_definitions(-DHAVE_CONFIG_H)

install(TARGETS qcomcamera LIBRARY DESTINATION /usr/lib64/gstreamer-1.0/ )
