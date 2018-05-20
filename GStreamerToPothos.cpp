/// Copyright (c) 2017-2018 Ashley Brighthope
/// SPDX-License-Identifier: BSL-1.0

#include "GStreamerToPothos.hpp"
#include <gst/app/gstappsink.h>
#include <gst/audio/audio-info.h>
#include <string>
#include "GStreamer.hpp"
#include "GStreamerTypes.hpp"

namespace
{

    class GStreamerToPothosRunState {
    private:
        std::unique_ptr< GstAppSink, GstTypes::GstObjectUnrefFunc > m_gstAppSink;
        std::atomic_uint32_t m_bufferCount;
        Pothos::DType m_dtype;
        GstTypes::GstCapsPtr m_lastCaps;
        Pothos::Object m_capsObj;
        Pothos::Label m_rxRateLabel;
        bool m_eosChanged;
        bool m_eos;

        static void eos(GstAppSink */* appsink */, gpointer /* user_data */)
        {
        }

        static GstFlowReturn new_preroll(GstAppSink */* appsink */, gpointer user_data)
        {
            auto self = static_cast< GStreamerToPothosRunState* >(user_data);
            self->m_bufferCount++;
            return GST_FLOW_OK;
        }

        static GstFlowReturn new_sample(GstAppSink */* appsink */, gpointer user_data)
        {
            auto self = static_cast< GStreamerToPothosRunState* >(user_data);
            self->m_bufferCount++;
            return GST_FLOW_OK;
        }

        static GstAppSink* getAppSinkByName(GStreamerSubWorker *gstreamerSubWorker)
        {
            auto appSink = GST_APP_SINK( gstreamerSubWorker->gstreamerBlock()->getPipelineElementByName( gstreamerSubWorker->name() ) );
            if ( appSink == nullptr )
            {
                throw Pothos::NullPointerException("GStreamerToPothosRunState::getAppSinkByName", "Could not get find GstAppSink from \"" + gstreamerSubWorker->name() + "\"");
            }
            return appSink;
        }

        static Pothos::DType gstreamerTypeToDtype(const GstAudioInfo *gstAudioInfo)
        {
            if ( GST_AUDIO_INFO_WIDTH( gstAudioInfo ) != GST_AUDIO_INFO_DEPTH( gstAudioInfo ) )
            {
                poco_warning(GstTypes::logger(),
                             "GST_AUDIO_INFO_WIDTH( gstAudioInfo ) = " + std::to_string( GST_AUDIO_INFO_WIDTH( gstAudioInfo ) ) +
                             " and GST_AUDIO_INFO_DEPTH( gstAudioInfo ) = " + std::to_string( GST_AUDIO_INFO_DEPTH( gstAudioInfo ) ) + " do not match, this may produce garbel data.");
            }

            if ( GST_AUDIO_INFO_ENDIANNESS( gstAudioInfo ) != G_BYTE_ORDER )
            {
                poco_warning(GstTypes::logger(), "GST_AUDIO_INFO_ENDIANNESS( gstAudioInfo ) does not match machine endianness");
            }

            std::string format;

            if ( ( gstAudioInfo->finfo->flags & GST_AUDIO_FORMAT_FLAG_COMPLEX ) != 0 )
            {
                format = "complex_";
            }

            if ( GST_AUDIO_INFO_IS_INTEGER( gstAudioInfo ) )
            {
                format += ( GST_AUDIO_INFO_IS_SIGNED( gstAudioInfo ) ) ? "int" : "uint";
            }
            else
            if ( GST_AUDIO_INFO_IS_FLOAT( gstAudioInfo ) )
            {
                format += "float";
            }

            format += std::to_string( GST_AUDIO_INFO_WIDTH( gstAudioInfo ) );

            return Pothos::DType( format, GST_AUDIO_INFO_CHANNELS( gstAudioInfo ) );
        }

        bool diffCaps(GstCaps *caps)
        {
            if ( caps == nullptr )
            {
                if ( !m_lastCaps.operator bool() )
                {
                    return false;
                }

                m_dtype = Pothos::DType();
                m_rxRateLabel = Pothos::Label();
                m_lastCaps.reset();
                return true;
            }

            if ( ( m_lastCaps.operator bool() ) && ( gst_caps_is_equal( caps, m_lastCaps.get() ) == TRUE ) )
            {
                return false;
            }

            m_lastCaps.reset( gst_caps_ref( caps ) );

            GstAudioInfo gstAudioInfo;
            if ( gst_audio_info_from_caps(&gstAudioInfo, caps) == TRUE )
            {
                if ( gstAudioInfo.layout != GST_AUDIO_LAYOUT_INTERLEAVED )
                {
                    poco_warning(GstTypes::logger(), "We do not support non INTERLEAVED data");
                }
                //poco_information(GstTypes::logger(), std::string("gstAudioInfo.finfo->name: ") + gstAudioInfo.finfo->name);
                m_dtype = gstreamerTypeToDtype( &gstAudioInfo );
                m_rxRateLabel = Pothos::Label("rxRate", Pothos::Object( GST_AUDIO_INFO_RATE( &gstAudioInfo ) ), 0);
            }
            else
            {
                m_dtype = Pothos::DType();
                m_rxRateLabel = Pothos::Label();
            }

            return true;
        }
    public:
        GStreamerToPothosRunState() = delete;
        GStreamerToPothosRunState(const GStreamerToPothosRunState&) = delete;
        GStreamerToPothosRunState& operator=(const GStreamerToPothosRunState&) = delete;

        explicit GStreamerToPothosRunState(GStreamerSubWorker *gstreamerSubWorker) :
            m_gstAppSink( getAppSinkByName( gstreamerSubWorker ) ),
            m_bufferCount( 0 ),
            m_dtype(),
            m_lastCaps( nullptr ),
            m_rxRateLabel(),
            m_eosChanged( false ),
            m_eos( false )
        {
            g_object_set( m_gstAppSink.get(),
                "max-buffers",      20,   /* Limit number of buffer to queue (Provent memory runaway). */
                 nullptr                  /* List termination */
            );
            GstAppSinkCallbacks gstAppSinkCallbacks{
                &eos,
                &new_preroll,
                &new_sample,
                { }
            };
            gst_app_sink_set_callbacks(
                m_gstAppSink.get(),
                &gstAppSinkCallbacks,
                this,
                nullptr
            );
        }

        ~GStreamerToPothosRunState()
        {
            GstAppSinkCallbacks gstAppSinkCallbacks{
                nullptr,
                nullptr,
                nullptr,
                { }
            };
            gst_app_sink_set_callbacks(
                m_gstAppSink.get(),
                &gstAppSinkCallbacks,
                this,
                nullptr
            );
        }

        bool eosChanged()
        {
            const auto eosChanged = m_eosChanged;
            m_eosChanged = false;
            return eosChanged;
        }

        bool eos() const
        {
            return m_eos;
        }

        uint32_t bufferCount() const
        {
            return m_bufferCount.load();
        }

        GstSample* try_pull_sample( GstClockTime timeout )
        {
            const auto eos = ( gst_app_sink_is_eos( m_gstAppSink.get() ) == TRUE );
            if (eos != m_eos)
            {
                m_eosChanged = true;
                m_eos = eos;
            }
            GstSample *gst_sample = gst_app_sink_try_pull_sample( m_gstAppSink.get(), timeout );
            if (gst_sample != nullptr)
            {
                m_bufferCount--;
            }
            return gst_sample;
        }

        void queryCaps(GstCaps *caps, Pothos::Packet &packet)
        {
            if ( diffCaps(caps) )
            {
                m_capsObj = GstTypes::gcharToObject( GstTypes::GCharPtr( gst_caps_to_string( caps ) ).get() );
            }

            packet.metadata[ GstTypes::PACKET_META_CAPS ] = m_capsObj;

            // If m_rxRateLabel valid add it to the packet
            if ( !m_rxRateLabel.id.empty() )
            {
                packet.labels.push_back( m_rxRateLabel );
            }
            packet.payload.dtype = m_dtype;
        }
    };  // class GStreamerToPothosRunState

    #define check_run_state_ptr() do { \
        if ( !m_runState ) throw Pothos::NullPointerException("Not in running state: " + this->gstreamerBlock()->getPipelineString(), std::string( __func__ ) ); \
    } while ( false )

    class GStreamerToPothosImpl : public GStreamerSubWorker {
    private:
        Pothos::OutputPort *m_pothosOutputPort;
        std::unique_ptr< GStreamerToPothosRunState > m_runState;

    public:
        GStreamerToPothosImpl(const GStreamerToPothosImpl&) = delete;             // No copy constructor
        GStreamerToPothosImpl& operator= (const GStreamerToPothosImpl&) = delete; // No assignment operator

        GStreamerToPothosImpl(GStreamer* gstreamerBlock, GstAppSink* gstAppSink) :
            GStreamerSubWorker( gstreamerBlock, GstTypes::gcharToString( GstTypes::GCharPtr( gst_element_get_name( gstAppSink ) ).get() ) ),
            m_pothosOutputPort( gstreamerBlock->setupOutput( name() ) ),  // Allocate Pothos output port for our GStreamer pad
            m_runState()
        {
            // Register Callable and Probe
            {
                const auto funcGetterName = this->funcName( "getCurrentBufferCount" );
                gstreamerBlock->registerCallable(
                    funcGetterName,
                    Pothos::Callable(&GStreamerToPothosImpl::getCurrentBufferCount).bind( std::ref( *this ), 0)
                );
                gstreamerBlock->registerProbe(funcGetterName);
            }
        }

        ~GStreamerToPothosImpl() override = default;

        uint32_t getCurrentBufferCount()
        {
            check_run_state_ptr();
            return m_runState->bufferCount();
        }

        bool blocking() override
        {
            return true;
        }

        void activate() override
        {
            m_runState.reset( new GStreamerToPothosRunState( this ) );
        }

        void deactivate() override
        {
            m_runState.reset();
        }

        void work(long long maxTimeoutNs) override
        {
            Pothos::Packet packet;

            GstSample *gst_sample = m_runState->try_pull_sample( maxTimeoutNs * GST_NSECOND );

            if ( gst_sample != nullptr )
            {
                // Get GStreamer buffer and create Pothos packet from it
                GstBuffer *gst_buffer = gst_sample_get_buffer( gst_sample );
                packet = GstTypes::makePacketFromGstBuffer( gst_buffer );

                auto segment = gst_sample_get_segment( gst_sample );
                packet.metadata[ GstTypes::PACKET_META_SEGMENT ] = Pothos::Object::make( GstTypes::segmentToObjectKwargs( segment ) );

                const auto info = gst_sample_get_info( gst_sample );
                packet.metadata[ GstTypes::PACKET_META_INFO    ] = Pothos::Object::make( GstTypes::structureToObjectKwargs( info ) );

                auto caps = gst_sample_get_caps( gst_sample );
                m_runState->queryCaps(caps, packet);

                gst_sample_unref( gst_sample );
            }
            else
            {
                if ( m_runState->eosChanged() == false )
                {
                    return;
                }
            }

            // If packet.payload is not valid, create empty one with no size.
            if ( static_cast< bool >( packet.payload ) == false )
            {
                packet.payload = Pothos::BufferChunk( 0 );
            }
            packet.metadata[ GstTypes::PACKET_META_EOS ] = Pothos::Object( m_runState->eos() );
            m_pothosOutputPort->postMessage( packet );
        }

    };  // class GStreamerToPothosImpl

}  // namespace

std::unique_ptr< GStreamerSubWorker > GStreamerToPothos::makeIfType(GStreamer* gstreamerBlock, GstElement* gstElement)
{
    if ( GST_IS_APP_SINK( gstElement ) )
    {
        return std::unique_ptr< GStreamerSubWorker >(
            new GStreamerToPothosImpl(
                gstreamerBlock,
                reinterpret_cast< GstAppSink* >( gstElement )
            )
        );
    }
    return nullptr;
}
