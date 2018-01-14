/// Copyright (c) 2017-2018 Ashley Brighthope
/// SPDX-License-Identifier: BSL-1.0

#include "PothosToGStreamer.hpp"
#include <gst/app/gstappsrc.h>
#include <string>
#include "GStreamer.hpp"
#include "GStreamerTypes.hpp"

namespace
{

    class PothosToGStreamerRunState {
    private:
        GstAppSrc *m_gstAppSource;
        GstCaps *m_baseCaps;
        bool m_tag_send_app_data_once;
        std::atomic_bool m_needData;

        static void need_data(GstAppSrc * /* src */, guint /* length */, gpointer user_data)
        {
            auto self = static_cast< PothosToGStreamerRunState* >( user_data );
            self->m_needData = true;
        }

        static void enough_data(GstAppSrc * /* src */, gpointer user_data)
        {
            auto self = static_cast< PothosToGStreamerRunState* >( user_data );
            self->m_needData = false;
        }

        static gboolean seek_data(GstAppSrc * /* src */, guint64 /* offset */, gpointer /* user_data */)
        {
            return false;
        }

        static GstAppSrc* getAppSrcByName(GStreamerSubWorker *gstreamerSubWorker)
        {
            auto appSrc = GST_APP_SRC( gstreamerSubWorker->gstreamerBlock()->getPipelineElementByName( gstreamerSubWorker->name() ) );
            if ( appSrc == nullptr )
            {
                throw Pothos::NullPointerException("PothosToGStreamerRunState::getAppSrcByName", "Could not get find GstAppSrc from \"" + gstreamerSubWorker->name() + "\"");
            }
            return appSrc;
        }

    public:
        PothosToGStreamerRunState() = delete;
        PothosToGStreamerRunState(const PothosToGStreamerRunState&) = delete;
        PothosToGStreamerRunState& operator=(const PothosToGStreamerRunState&) = delete;

        PothosToGStreamerRunState(GStreamerSubWorker *gstreamerSubWorker) :
            m_gstAppSource( getAppSrcByName( gstreamerSubWorker ) ),
            m_baseCaps( nullptr ),
            m_tag_send_app_data_once( true ),
            m_needData( false )
        {
            gst_object_ref( GST_OBJECT( m_gstAppSource ) );
            // Save the caps if they were set from pipeline
            m_baseCaps = gst_app_src_get_caps( m_gstAppSource );

            GstAppSrcCallbacks gstAppSrcCallbacks{
                &need_data,
                &enough_data,
                &seek_data,
                { }
            };
            gst_app_src_set_callbacks( m_gstAppSource, &gstAppSrcCallbacks, this, NULL);

            /* Our GstAppSrc can only stream from Pothos, can't seek */
            gst_app_src_set_stream_type( m_gstAppSource, GST_APP_STREAM_TYPE_STREAM );

            g_object_set( m_gstAppSource,
    //          "is-live",      FALSE,
    //          "format",       "GST_FORMAT_TIME",
                "block",        FALSE,              /* We can't block in Pothos work() method */
                "do-timestamp", TRUE,               /* Get GstAppSrc to time stamp our buffers */
                NULL                                /* List termination */
            );
        }

        ~PothosToGStreamerRunState(void)
        {
            // Disconnect callback
            GstAppSrcCallbacks gstAppSrcCallbacks{
                nullptr,
                nullptr,
                nullptr,
                { }
            };
            gst_app_src_set_callbacks( m_gstAppSource, &gstAppSrcCallbacks, this, NULL);

            sendEos();
            if ( m_baseCaps != nullptr )
            {
                gst_caps_unref( m_baseCaps );
                m_baseCaps = nullptr;
            }
            gst_object_unref( GST_OBJECT( m_gstAppSource ) );
        }

        bool sendEos(void)
        {
            auto flow_return = gst_app_src_end_of_stream( gstAppSource() );
            if ( flow_return != GST_FLOW_OK )
            {
                const auto flowQuark = GstTypes::gquarkToString( gst_flow_to_quark( flow_return ) );
                poco_warning( GstTypes::logger(), "PothosToGStreamer::sendEos() flow_return = " + std::to_string( flow_return ) + " (" + flowQuark + ")" );
                return false;
            }
            return true;
        }

        GstAppSrc *gstAppSource(void)
        {
            return m_gstAppSource;
        }

        GstCaps* getBaseCaps(void)
        {
            return m_baseCaps;
        }

        bool needData(void) const
        {
            return m_needData.operator bool();
        }

        bool tag_send_app_data_once(void)
        {
            if ( m_tag_send_app_data_once )
            {
                m_tag_send_app_data_once = false;
                return true;
            }
            else
            {
                return false;
            }
        }

        bool sendEvent(GstEvent *event)
        {
            return gst_element_send_event( GST_ELEMENT( gstAppSource() ), event);
        }
    };  // class PothosToGStreamerRunState

    #define check_run_state_ptr() do { \
        if ( m_runState.get() == nullptr ) throw Pothos::NullPointerException("Not in running state: " + this->gstreamerBlock()->getPipelineString(), std::string( __func__ ) ); \
    } while ( false )

    class PothosToGStreamerImpl : public GStreamerSubWorker
    {
    private:
        Pothos::InputPort *m_pothosInputPort;
        std::shared_ptr< std::string > m_tag_app_data;
        std::unique_ptr< PothosToGStreamerRunState > m_runState;

    public:
        PothosToGStreamerImpl(const PothosToGStreamerImpl&) = delete;              // No copy constructor
        PothosToGStreamerImpl& operator= (const PothosToGStreamerImpl&) = delete;  // No equals operator

        PothosToGStreamerImpl(GStreamer* gstreamerBlock, GstAppSrc* gstAppSource) :
            GStreamerSubWorker( gstreamerBlock, GstTypes::gcharToString( GstTypes::GCharPtr( gst_element_get_name( gstAppSource ) ).get() ) ),
            m_pothosInputPort( gstreamerBlock->setupInput( name() ) ), // Allocate Pothos input port for GStreamer
            m_tag_app_data( new std::string() ),
            m_runState()
        {
            // Register Callable and Probe
            {
                const auto funcGetterName = "getCurrentLevelBytes_" + name();
                gstreamerBlock->registerCallable(
                    funcGetterName,
                    Pothos::Callable(&PothosToGStreamerImpl::getCurrentLevelBytes).bind( std::ref( *this ), 0)
                );
                gstreamerBlock->registerProbe(funcGetterName);
            }

            // Register sendEos Callable and Slot
            {
                const auto sendEosName = "sendEos_" + name();
                gstreamerBlock->registerCallable(
                    sendEosName,
                    Pothos::Callable(&PothosToGStreamerImpl::sendEos).bind( std::ref( *this ), 0)
                );
                // registerCallable does not register our slot since this method takes no arguments
                gstreamerBlock->registerSlot( sendEosName );
            }
        }

        ~PothosToGStreamerImpl(void)
        {
        }

        const std::string& getTagAppData(void) const
        {
            return *m_tag_app_data;
        }

        void setTagAppData(const std::string& tag_app_data)
        {
            *m_tag_app_data = tag_app_data;
        }

        void activate(void) override
        {
            // Get current instance of GStreamer app source
            m_runState.reset( new PothosToGStreamerRunState( this ) );
        }

        void deactivate(void) override
        {
            m_runState.reset();
        }

        bool send_gstreamer_app_tags(void)
        {
            GstTagList *gstTagList = gst_tag_list_new(
                GST_TAG_APPLICATION_NAME, "Pothos",
                NULL
            );

            // If we have a string for GST_TAG_APPLICATION_DATA lets convert it and send it
            if ( !m_tag_app_data->empty() )
            {
                auto gstBuffer = GstTypes::makeSharedGStreamerBuffer(m_tag_app_data->data(), m_tag_app_data->length(), m_tag_app_data);

                GstCaps *caps = gst_caps_from_string( "text/plain" );
                GstSegment *segment = gst_segment_new();
                GstStructure *info = gst_structure_new_empty( GST_TAG_APPLICATION_DATA );

                GstSample *sample = gst_sample_new(
                    gstBuffer,
                    caps,
                    segment,
                    info
                );

                GValue value = G_VALUE_INIT;
                g_value_init( &value, GST_TYPE_SAMPLE );
                g_value_set_boxed( &value, sample );

                gst_tag_list_add_value(
                    gstTagList,
                    GST_TAG_MERGE_APPEND,
                    GST_TAG_APPLICATION_DATA, &value
                );
            }

            GstEvent *event = gst_event_new_tag( gstTagList );

            return m_runState->sendEvent( event );
        }

        bool sendToGStreamer(const Pothos::Packet &packet)
        {
            const std::string funcName( "PothosToGStreamer::sendToGStreamer" );

            // Try to push a GStreamer tag on first buffer push
            if ( m_runState->tag_send_app_data_once() )
            {
                send_gstreamer_app_tags();
            }

            // If GStreamer AppSrc is full bail
            if ( m_runState->needData() == false )
            {
                return false;
            }

            auto gstBuffer = GstTypes::makeGstBufferFromPacket( packet );

            // If GstBuffer could not be allocated, bail
            if ( gstBuffer == nullptr )
            {
                return false;
            }

            // Add caps if any to GStreamer buffer
            {
                auto caps_it = packet.metadata.find( GstTypes::PACKET_META_CAPS );
                if ( ( caps_it != packet.metadata.end() ) &&
                     ( caps_it->second.type() == typeid(std::string) ) )
                {
                    const auto & caps = caps_it->second.extract< std::string >();
                    if ( GstTypes::debug_extra )
                    {
                        poco_information( GstTypes::logger(), funcName + " We got caps in the metadata: " + caps );
                    }
                    auto gstCaps = gst_caps_from_string( caps.c_str() );
                    // If buffer packet had caps, push caps in with buffer in sample
                    if (gstCaps != nullptr)
                    {
                        gst_app_src_set_caps( m_runState->gstAppSource(), gstCaps );
                        gst_caps_unref( gstCaps );
                    }
                }
                else
                {
                    gst_app_src_set_caps( m_runState->gstAppSource(), m_runState->getBaseCaps() );
                }
            }

            auto flow_return = gst_app_src_push_buffer( m_runState->gstAppSource(), gstBuffer );
            if ( flow_return != GST_FLOW_OK )
            {
                const auto flowQuark = GstTypes::gquarkToString( gst_flow_to_quark( flow_return ) );
                poco_warning( GstTypes::logger(), funcName + " flow_return = " + std::to_string( flow_return ) + " (" + flowQuark + ")" );
            }

            // Check if packet has EOS flags and if its set
            {
                const bool eos = GstTypes::ifKeyExtractOrDefault( packet.metadata, GstTypes::PACKET_META_EOS, false );
                if (eos)
                {
                    sendEos();
                }
            }

            return ( flow_return >= 0 );
        }

        void work(long long /* maxTimeoutNs */) override
        {
            if ( !m_pothosInputPort->hasMessage() )
            {
                return;
            }

            auto message = m_pothosInputPort->peekMessage();

            if ( message.type() == typeid( Pothos::Packet ) )
            {
                const auto &packet = message.extract< Pothos::Packet >();
                if ( sendToGStreamer( packet ) )
                {
                    m_pothosInputPort->popMessage();
                }
                return;
            }

            poco_warning( GstTypes::logger(), "Received message on port "+m_pothosInputPort->name()+" of type we can't handle. Only accept Pothos::Packet" );
            m_pothosInputPort->popMessage();
        }

        void sendEos(void)
        {
            check_run_state_ptr();

            m_runState->sendEos();
        }

        guint64 getCurrentLevelBytes(void) const
        {
            check_run_state_ptr();

            return gst_app_src_get_current_level_bytes( m_runState->gstAppSource() );
        }
    };  // class PothosToGStreamerImpl

}

GStreamerSubWorker* PothosToGStreamer::makeIfType(GStreamer* gstreamerBlock, GstElement* gstElement)
{
    if ( GST_IS_APP_SRC( gstElement ) )
    {
        return new PothosToGStreamerImpl(
            gstreamerBlock,
            reinterpret_cast< GstAppSrc* >( gstElement )
        );
    }
    return nullptr;
}
