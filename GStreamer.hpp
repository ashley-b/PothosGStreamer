/// Copyright (c) 2017-2019 Ashley Brighthope
/// SPDX-License-Identifier: BSL-1.0

#pragma once

#include "GStreamerTypes.hpp"
#include <Pothos/Framework.hpp>
#include <gst/gst.h>
#include <string>
#include <memory>  /* std::unique_ptr */
#include <type_traits>

extern const char SIGNAL_BUS_NAME[];
extern const char SIGNAL_TAG[];
extern const char SIGNAL_EOS_NAME[];

// Forward declare
class GStreamerSubWorker;

class GStreamer : public Pothos::Block
{
private:
    const std::string m_pipeline_string;
    std::unique_ptr< GstPipeline, GstTypes::GstObjectUnrefFunc > m_pipeline;
    std::unique_ptr< GstBus, GstTypes::GstObjectUnrefFunc > m_bus;
    std::vector< std::unique_ptr< GStreamerSubWorker > > m_gstreamerSubWorkers;
    int m_blockingNodes;
    bool m_pipelineActive;
    GstState m_gstState;

    void gstChangeState( GstState state );
    void workerStop(const std::string &reason);
    Pothos::ObjectKwargs gstMessageToFormattedObject(GstMessage *gstMessage);
    Pothos::Object gstMessageToObject(GstMessage *gstMessage);
    void processGstMessagesTimeout(GstClockTime timeout);
    void setState(const std::string &state);
    void findSourcesAndSinks(GstBin *bin);
    void createPipeline();
    void destroyPipeline();
    Pothos::ObjectKwargs gstMessageInfoWarnError( GstMessage *message );
    void debugPipelineToDot(const std::string &fileName);

public:
    GStreamer(const GStreamer&) = delete;
    GStreamer& operator=(const GStreamer&) = delete;

    explicit GStreamer(const std::string &pipelineString);
    ~GStreamer() override;

    static Block *make(const std::string &pipelineString);

    GstTypes::GstElementPtr getPipelineElementByName(const std::string &name) const;

    std::string getPipelineString() const;
    GstPipeline* getPipeline() const;
    Pothos::Object getPipelineLatency() const;
    int64_t getPipelinePosition(const std::string &format) const;
    int64_t getPipelineDuration(const std::string &format) const;
    std::string getPipelineGraph();
    void savePipelineGraph(const std::string &fileName);

    void activate() override;
    void deactivate() override;

    void propagateLabels(const Pothos::InputPort *input) override;
    void work() override;

};  // class GStreamer
