########################################################################
# Project setup
########################################################################
cmake_minimum_required(VERSION 2.8)
project(PothosGStreamer)
enable_language(CXX)

find_package(Pothos "0.6.0" CONFIG REQUIRED)

find_package(PkgConfig REQUIRED)

set(GST_VER "1.0")
PKG_CHECK_MODULES(PC_GSTREAMER REQUIRED
    gstreamer-${GST_VER}
    gstreamer-app-${GST_VER}
    gstreamer-audio-${GST_VER}
)

find_program(pothos_util PothosUtil)
if (pothos_util)
    message(STATUS "Found ${pothos_util}. Adding test target")
    enable_testing()
    set(test_list
        "test_gstreamer_types_gvalue_to_object"
        "test_gstreamer_types_if_key_extract_or_default"
        "test_gstreamer_types_gchar_ptr"
        "test_gstreamer_types_gerror_ptr"
        "test_gstreamer_types_unique_out_arg"
        "test_gstreamer_source"
        "test_gstreamer_tag_sink"
        "test_gstreamer_sink"
        "test_gstreamer_create_destroy"
        "test_gstreamer_passthrough"
    )

    foreach(test_name IN LISTS test_list)
        add_test( NAME ${test_name} COMMAND "${pothos_util}" "--self-test1=/media/tests/${test_name}")
    endforeach(test_name)
endif()


########################################################################
# Module setup
########################################################################
POTHOS_MODULE_UTIL(
    TARGET GStreamer
    SOURCES
        GStreamer.cpp
        GStreamerStatic.cpp
        GStreamerSubWorker.cpp
        PothosToGStreamer.cpp
        GStreamerToPothos.cpp
        GStreamerTypes.cpp
        TestGStreamer.cpp
    DESTINATION media
    LIBRARIES ${PC_GSTREAMER_LIBRARIES}

    ENABLE_DOCS
)

target_include_directories( GStreamer SYSTEM PRIVATE ${PC_GSTREAMER_INCLUDE_DIRS} )
