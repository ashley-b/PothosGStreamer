/// Copyright (c) 2017-2018 Ashley Brighthope
/// SPDX-License-Identifier: BSL-1.0

#include <gst/gst.h>
#include "GStreamerTypes.hpp"
#include "GStreamerStatic.hpp"

namespace {  // anonymous namespace

    struct GStreamerStatic
    {
        GstTypes::GErrorPtr initError;

        GStreamerStatic() :
            initError( )
        {
            if ( GstTypes::debug_extra )
            {
                poco_information(GstTypes::logger(), "GStreamerStatic::GStreamerStatic() - gst_is_initialized() = " + GstTypes::boolToString( gst_is_initialized() ) );
            }
            // gst_segtrap_set_enabled(false);

            // Run GStreamer registry update in this thread
            gst_registry_fork_set_enabled( false );

            if (  !gst_init_check( nullptr, nullptr, GstTypes::uniquePtrRef( initError ) ) )
            {
               GstTypes::logger().error( "GStreamerStatic::GStreamerStatic(): gst_init_check error: " + GstTypes::gerrorToString( initError.get() ) );
            }

            if ( GstTypes::debug_extra )
            {
                poco_information( GstTypes::logger(), "GStreamer version: " + GstStatic::getVersion() );
            }
        }

        ~GStreamerStatic()
        {
            if ( GstTypes::debug_extra )
            {
                poco_information(GstTypes::logger(), "GStreamerStatic::GStreamerStatic() - gst_is_initialized() = " + GstTypes::boolToString( gst_is_initialized() ) );
            }
            // Only deinit GStreamer if it was initialized
            if ( gst_is_initialized() )
            {
                gst_deinit();
            }
        }
    };

}  // anonymous namespace

static GStreamerStatic gstreamerStatic;

namespace GstStatic
{
    GError* getInitError()
    {
        return gstreamerStatic.initError.get();
    }

    std::string getVersion()
    {
        return GstTypes::gcharToString( GstTypes::GCharPtr( gst_version_string() ).get() );
    }
}
