/// Copyright (c) 2017-2018 Ashley Brighthope
/// SPDX-License-Identifier: BSL-1.0

#pragma once

#include <Pothos/Framework.hpp>
#include <gst/gst.h>
#include <string>
#include <memory>  /* std::unique_ptr */

extern const std::string signalBusName;
extern const std::string signalTag;
extern const std::string signalEosName;

// Forward declare
class GStreamerSubWorker;

class GStreamer : public Pothos::Block
{
private:
    const std::string m_pipeline_string;
    GstPipeline *m_pipeline;
    GstBus *m_bus;
    std::vector< std::unique_ptr< GStreamerSubWorker > > m_gstreamerSubWorkers;
    int m_blockingNodes;
    bool m_pipeline_active;
    GstState m_gstState;

    void gstChangeState( GstState state );
    void workerStop(const std::string &reason);
    Pothos::ObjectKwargs gstMessageToFormattedObject(GstMessage *gstMessage);
    Pothos::Object gstMessageToObject(GstMessage *gstMessage);
    void processGStreamerMessagesTimeout(GstClockTime timeout);
    void setState(const std::string &state);
    static void for_each_pipeline_element(const GValue *value, gpointer data);
    void findSourcesAndSinks(GstBin *bin);
    void createPipeline();
    void destroyPipeline();
    Pothos::ObjectKwargs gstMessageInfoWarnError( GstMessage *message );

public:
    GStreamer(const GStreamer&) = delete;
    GStreamer& operator=(const GStreamer&) = delete;

    GStreamer(const std::string &pipelineString);
    ~GStreamer() override;

    static Block *make(const std::string &pipelineString);

    GstElement* getPipelineElementByName(const std::string &name) const;

    std::string getPipelineString() const;
    GstPipeline* getPipeline() const;
    Pothos::Object getPipelineLatency() const;
    int64_t getPipelinePosition(const std::string &format) const;
    int64_t getPipelineDuration(const std::string &format) const;

    void activate() override;
    void deactivate() override;

    void propagateLabels(const Pothos::InputPort *input) override;
    void work() override;

};  // class GStreamer
