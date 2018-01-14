/// Copyright (c) 2017-2018 Ashley Brighthope
/// SPDX-License-Identifier: BSL-1.0

#include "GStreamerSubWorker.hpp"

GStreamerSubWorker::GStreamerSubWorker(GStreamer *gstreamerBlock, const std::string &name) :
    m_gstreamerBlock( gstreamerBlock ),
    m_name( name )
{
}

GStreamerSubWorker::~GStreamerSubWorker(void)
{
}

GStreamer* GStreamerSubWorker::gstreamerBlock(void) const
{
    return m_gstreamerBlock;
}

const std::string& GStreamerSubWorker::name(void) const
{
    return m_name;
}

void GStreamerSubWorker::activate(void)
{
}

void GStreamerSubWorker::deactivate(void)
{
}

bool GStreamerSubWorker::blocking(void)
{
    return false;
}
