/// Copyright (c) 2017-2018 Ashley Brighthope
/// SPDX-License-Identifier: BSL-1.0

#include "GStreamerSubWorker.hpp"

GStreamerSubWorker::GStreamerSubWorker(GStreamer *gstreamerBlock, const std::string &name) :
    m_gstreamerBlock( gstreamerBlock ),
    m_name( name )
{
}

GStreamerSubWorker::~GStreamerSubWorker() = default;

GStreamer* GStreamerSubWorker::gstreamerBlock() const
{
    return m_gstreamerBlock;
}

const std::string& GStreamerSubWorker::name() const
{
    return m_name;
}

std::string GStreamerSubWorker::funcName(const std::string &funcName) const
{
    return funcName + "_" + name();
}

void GStreamerSubWorker::activate()
{
}

void GStreamerSubWorker::deactivate()
{
}

bool GStreamerSubWorker::blocking()
{
    return false;
}
