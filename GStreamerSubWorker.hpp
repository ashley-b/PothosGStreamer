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

    GStreamerSubWorker(const GStreamerSubWorker&) = delete; // Non construction-copyable
    GStreamerSubWorker& operator=(const GStreamerSubWorker&) = delete; // Non copyable

protected:
    GStreamerSubWorker(GStreamer *gstreamerBlock, const std::string &name);

public:
    virtual ~GStreamerSubWorker(void) = default;

    GStreamer* gstreamerBlock(void) const;
    const std::string& name(void) const;
    virtual void activate(void);
    virtual void deactivate(void);
    virtual bool blocking(void);
    virtual void work(long long maxTimeoutNs) = 0;

};  // class GStreamerSubWorker
