/// Copyright (c) 2017-2018 Ashley Brighthope
/// SPDX-License-Identifier: BSL-1.0

#pragma once

#include <string>

// Forward declare
class GStreamer;

class GStreamerSubWorker
{
private:
    GStreamer *m_gstreamerBlock;
    const std::string m_name;

protected:
    GStreamerSubWorker(GStreamer *gstreamerBlock, const std::string &name);

public:
    GStreamerSubWorker() = delete;
    GStreamerSubWorker(const GStreamerSubWorker&) = delete; // Non construction-copyable
    GStreamerSubWorker& operator=(const GStreamerSubWorker&) = delete; // Non copyable
    GStreamerSubWorker(GStreamerSubWorker&&) = delete; // Non construction-moveable
    GStreamerSubWorker& operator=(GStreamerSubWorker&&) = delete; // Non moveable

    virtual ~GStreamerSubWorker();

    GStreamer* gstreamerBlock() const;
    const std::string& name() const;
    std::string funcName(const std::string &funcName) const;
    virtual void activate();
    virtual void deactivate();
    virtual bool blocking();
    virtual void work(long long maxTimeoutNs) = 0;

};  // class GStreamerSubWorker
