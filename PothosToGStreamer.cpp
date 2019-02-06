/// Copyright (c) 2017-2019 Ashley Brighthope
/// SPDX-License-Identifier: BSL-1.0

#include "PothosToGStreamer.hpp"
#include "GStreamer.hpp"
#include "GStreamerTypes.hpp"
#include <gst/app/gstappsrc.h>
#include <string>

static std::string gstFlowToString(GstFlowReturn gstFlowReturn)
{
    return GstTypes::gquarkToString( gst_flow_to_quark( gstFlowReturn ) ).value("unknown");
}

namespace
{

    class PothosToGStreamerRunState {
    private:
        std::unique_ptr< GstAppSrc, GstTypes::GstObjectUnrefFunc > m_gstAppSource;
        GstTypes::GstCapsPtr m_baseCaps;
        bool m_tagSendAppDataOnce;
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
            return FALSE;
        }

        static GstAppSrc* getAppSrcByName(GStreamerSubWorker *gstreamerSubWorker)
        {
            auto element = gstreamerSubWorker->gstreamerBlock()->getPipelineElementByName( gstreamerSubWorker->name() );
            if ( !GST_IS_APP_SRC( element.get() ) )
            {
                throw Pothos::NullPointerException("PothosToGStreamerRunState::getAppSrcByName", "Could not find a GstAppSrc named \"" + gstreamerSubWorker->name() + "\"");
            }
            return reinterpret_cast< GstAppSrc* >( element.release() );
        }

    public:
        PothosToGStreamerRunState() = delete;
        PothosToGStreamerRunState(const PothosToGStreamerRunState&) = delete;
        PothosToGStreamerRunState& operator=(const PothosToGStreamerRunState&) = delete;
        PothosToGStreamerRunState(PothosToGStreamerRunState&&) = delete;
        PothosToGStreamerRunState& operator=(PothosToGStreamerRunState&&) = delete;

        explicit PothosToGStreamerRunState(GStreamerSubWorker *gstreamerSubWorker) :
            m_gstAppSource( getAppSrcByName( gstreamerSubWorker ) ),
            m_baseCaps( nullptr ),
            m_tagSendAppDataOnce( true ),
            m_needData( false )
        {
            // Save the caps if they were set from pipeline
            m_baseCaps.reset( gst_app_src_get_caps( m_gstAppSource.get() ) );

            GstAppSrcCallbacks gstAppSrcCallbacks{
                &need_data,
                &enough_data,
                &seek_data,
                { }
            };
            gst_app_src_set_callbacks( m_gstAppSource.get(), &gstAppSrcCallbacks, this, nullptr);

            /* Our GstAppSrc can only stream from Pothos, can't seek */
            gst_app_src_set_stream_type( m_gstAppSource.get(), GST_APP_STREAM_TYPE_STREAM );

            g_object_set( m_gstAppSource.get(),
    //            "is-live",      FALSE,
    //            "format",       GST_FORMAT_TIME,
                "block",        FALSE,              /* We can't block in Pothos work() method */
                "do-timestamp", TRUE,               /* Get GstAppSrc to time stamp our buffers */
                nullptr                             /* List termination */
            );
        }

        ~PothosToGStreamerRunState()
        {
            // Disconnect callbacks
            GstAppSrcCallbacks gstAppSrcCallbacks{
                nullptr,
                nullptr,
                nullptr,
                { }
            };
            gst_app_src_set_callbacks( m_gstAppSource.get(), &gstAppSrcCallbacks, this, nullptr);

            sendEos();
        }

        bool sendEos()
        {
            const auto flowReturn = gst_app_src_end_of_stream( gstAppSource() );
            if ( flowReturn != GST_FLOW_OK )
            {
                const auto flowStr = gstFlowToString( flowReturn );
                poco_warning( GstTypes::logger(), "PothosToGStreamer::sendEos() flow_return = " + std::to_string( flowReturn ) + " (" + flowStr + ")" );
                return false;
            }
            return true;
        }

        GstAppSrc *gstAppSource()
        {
            return m_gstAppSource.get();
        }

        GstCaps* getBaseCaps()
        {
            return m_baseCaps.get();
        }

        bool needData() const
        {
            return m_needData.load();
        }

        bool tagSendAppDataOnce()
        {
            if ( m_tagSendAppDataOnce )
            {
                m_tagSendAppDataOnce = false;
                return true;
            }
            return false;
        }

        bool sendEvent(GstEvent *event)
        {
            return gst_element_send_event( GST_ELEMENT( gstAppSource() ), event) == TRUE;
        }
    };  // class PothosToGStreamerRunState

    #define check_run_state_ptr() do { \
        if ( !m_runState.get() ) throw Pothos::NullPointerException("Not in running state: " + this->gstreamerBlock()->getPipelineString(), std::string( __func__ ) ); \
    } while ( false )

    class PothosToGStreamerImpl : public GStreamerSubWorker
    {
    private:
        Pothos::InputPort *m_pothosInputPort;
        std::shared_ptr< std::string > m_tag_app_data;
        std::unique_ptr< PothosToGStreamerRunState > m_runState;

    public:
        PothosToGStreamerImpl(const PothosToGStreamerImpl&) = delete;              // No copy constructor
        PothosToGStreamerImpl& operator= (const PothosToGStreamerImpl&) = delete;  // No assignment operator

        PothosToGStreamerImpl(GStreamer* gstreamerBlock, GstAppSrc* gstAppSource) :
            GStreamerSubWorker( gstreamerBlock, GstTypes::gcharToString( GstTypes::GCharPtr( gst_element_get_name( gstAppSource ) ).get() ).value() ),
            m_pothosInputPort( gstreamerBlock->setupInput( name() ) ), // Allocate Pothos input port for GStreamer
            m_tag_app_data( std::make_shared< std::string >() ),
            m_runState()
        {
            // Register Callable and Probe
            {
                const auto funcGetterName = this->funcName( "getCurrentLevelBytes" );
                gstreamerBlock->registerCallable(
                    funcGetterName,
                    Pothos::Callable(&PothosToGStreamerImpl::getCurrentLevelBytes).bind( std::ref( *this ), 0)
                );
                gstreamerBlock->registerProbe( funcGetterName );
            }

            // Register sendEos Callable and Slot
            {
                const auto sendEosName = this->funcName( "sendEos" );
                gstreamerBlock->registerCallable(
                    sendEosName,
                    Pothos::Callable(&PothosToGStreamerImpl::sendEos).bind( std::ref( *this ), 0)
                );
                // registerCallable does not register our slot since this method takes no arguments
                gstreamerBlock->registerSlot( sendEosName );
            }
        }

        ~PothosToGStreamerImpl() override = default;

        const std::string& getTagAppData() const
        {
            return *m_tag_app_data;
        }

        void setTagAppData(const std::string& tag_app_data)
        {
            *m_tag_app_data = tag_app_data;
        }

        void activate() override
        {
            // Get current instance of GStreamer app source
            m_runState.reset( new PothosToGStreamerRunState( this ) );
        }

        void deactivate() override
        {
            m_runState.reset();
        }

        bool sendGstreamerAppTags()
        {
            GstTagList *gstTagList = gst_tag_list_new(
                GST_TAG_APPLICATION_NAME, "Pothos",
                nullptr
            );

            // If we have a string for GST_TAG_APPLICATION_DATA lets convert it and send it
            if ( !m_tag_app_data->empty() )
            {
                auto gstBuffer = GstTypes::makeSharedGstBuffer(m_tag_app_data->data(), m_tag_app_data->length(), m_tag_app_data);

                GstCaps *caps = gst_caps_from_string( "text/plain" );
                GstSegment *segment = gst_segment_new();
                GstStructure *info = gst_structure_new_empty( GST_TAG_APPLICATION_DATA );

                GstSample *sample = gst_sample_new(
                    gstBuffer.release(),
                    caps,
                    segment,
                    info
                );

                GstTypes::GVal value( GST_TYPE_SAMPLE );
                g_value_set_boxed( value(), sample );

                gst_tag_list_add_value(
                    gstTagList,
                    GST_TAG_MERGE_APPEND,
                    GST_TAG_APPLICATION_DATA, value()
                );
            }

            GstEvent *event = gst_event_new_tag( gstTagList );

            return m_runState->sendEvent( event );
        }

        bool sendToGStreamer(const Pothos::Packet &packet)
        {
            const std::string funcName( "PothosToGStreamer::sendToGStreamer" );

            // Try to push a GStreamer tag on first buffer push
            if ( m_runState->tagSendAppDataOnce() )
            {
                sendGstreamerAppTags();
            }

            // If GStreamer AppSrc is full bail
            if ( m_runState->needData() == false )
            {
                return false;
            }

            auto gstBuffer = GstTypes::makeGstBufferFromPacket( packet );

            // If GstBuffer could not be allocated, bail
            if ( !gstBuffer )
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
                    // If pothos packet has GStreamer caps add it to next GstBuffer push
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

            const auto flowReturn = gst_app_src_push_buffer( m_runState->gstAppSource(), gstBuffer.release() );
            if ( flowReturn != GST_FLOW_OK )
            {
                const auto flowStr = gstFlowToString( flowReturn );
                poco_warning( GstTypes::logger(), funcName + " flow_return = " + std::to_string( flowReturn ) + " (" + flowStr + ")" );
            }

            // Check if packet has EOS flags and if its set
            if ( GstTypes::ifKeyExtract< bool >( packet.metadata, GstTypes::PACKET_META_EOS ).value( false ) )
            {
                sendEos();
            }

            return ( flowReturn >= 0 );
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

        void sendEos()
        {
            check_run_state_ptr();

            m_runState->sendEos();
        }

        guint64 getCurrentLevelBytes() const
        {
            check_run_state_ptr();

            return gst_app_src_get_current_level_bytes( m_runState->gstAppSource() );
        }
    };  // class PothosToGStreamerImpl

}  // namespace

std::unique_ptr< GStreamerSubWorker > PothosToGStreamer::makeIfType(GStreamer* gstreamerBlock, GstElement* gstElement)
{
    if ( GST_IS_APP_SRC( gstElement ) )
    {
        return std::unique_ptr< GStreamerSubWorker >(
            new PothosToGStreamerImpl(
                gstreamerBlock,
                reinterpret_cast< GstAppSrc* >( gstElement )
            )
        );
    }
    return nullptr;
}
