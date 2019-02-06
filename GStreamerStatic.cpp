/// Copyright (c) 2017-2018 Ashley Brighthope
/// SPDX-License-Identifier: BSL-1.0

#include "GStreamerStatic.hpp"
#include "GStreamerTypes.hpp"
#include <gst/gst.h>

namespace {

    struct GStreamerStatic
    {
        GstTypes::GErrorPtr initError;

        GStreamerStatic(const GStreamerStatic &) = delete;
        GStreamerStatic& operator=(const GStreamerStatic &) = delete;

        GStreamerStatic(GStreamerStatic &&) = delete;
        GStreamerStatic& operator=(GStreamerStatic &&) = delete;

        GStreamerStatic() :
            initError( )
        {
            if ( GstTypes::debug_extra )
            {
                poco_information(GstTypes::logger(), "GStreamerStatic::GStreamerStatic() - gst_is_initialized() = " + GstTypes::boolToString( gst_is_initialized() ) );
            }
            // gst_segtrap_set_enabled(FALSE);

            // Run GStreamer registry update in this thread
            gst_registry_fork_set_enabled( FALSE );

            if ( gst_init_check( nullptr, nullptr, GstTypes::uniqueOutArg( initError ) ) == FALSE )
            {
               poco_error( GstTypes::logger(), "GStreamerStatic::GStreamerStatic(): gst_init_check error: " + GstTypes::gerrorToString( initError.get() ) );
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
                poco_information(GstTypes::logger(), "GStreamerStatic::~GStreamerStatic() - gst_is_initialized() = " + GstTypes::boolToString( gst_is_initialized() ) );
            }
            // Only deinit GStreamer if it was initialized
            if ( gst_is_initialized() == TRUE )
            {
                gst_deinit();
            }
        }
    };  // struct GStreamerStatic

}  // namespace

static GStreamerStatic gstreamerStatic;

namespace GstStatic
{
    GError* getInitError()
    {
        return gstreamerStatic.initError.get();
    }

    std::string getVersion()
    {
        return GstTypes::gcharToString( GstTypes::GCharPtr( gst_version_string() ).get() ).value();
    }
}  // namespace GstStatic
