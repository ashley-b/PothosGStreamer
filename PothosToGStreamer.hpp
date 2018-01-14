/// Copyright (c) 2017-2018 Ashley Brighthope
/// SPDX-License-Identifier: BSL-1.0

#pragma once

#include "GStreamerSubWorker.hpp"
#include <gst/gstelement.h>

namespace PothosToGStreamer
{
    GStreamerSubWorker* makeIfType(GStreamer* gstreamerBlock, GstElement* gstElement);
}  // namespace PothosToGStreamer
