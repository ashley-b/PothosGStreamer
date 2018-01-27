/// Copyright (c) 2017-2018 Ashley Brighthope
/// SPDX-License-Identifier: BSL-1.0

#pragma once

#include "GStreamerSubWorker.hpp"
#include <gst/gstelement.h>
#include <memory>

namespace PothosToGStreamer
{
    std::unique_ptr< GStreamerSubWorker > makeIfType(GStreamer* gstreamerBlock, GstElement* gstElement);
}  // namespace PothosToGStreamer
