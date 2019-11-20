/// Copyright (c) 2017-2019 Ashley Brighthope
/// SPDX-License-Identifier: BSL-1.0

#include "GStreamerToPothos.hpp"
#include "GStreamer.hpp"
#include "GStreamerTypes.hpp"
#include <gst/app/gstappsink.h>
#include <gst/audio/audio-info.h>
#include <string>

namespace
{

    class GStreamerToPothosRunState final {
    private:
        std::unique_ptr< GstAppSink, GstTypes::GstObjectUnrefFunc > m_gstAppSink;
        std::atomic_uint32_t m_bufferCount;
        Pothos::DType m_dtype;
        Pothos::Label m_rxRateLabel;
        GstTypes::GstCapsCache m_gstCapsCach;
        bool m_eosChanged;
        bool m_eos;

        static void callBack_eos(GstAppSink */* appsink */, gpointer /* user_data */)
        {
        }

        static GstFlowReturn callBack_new_preroll(GstAppSink */* appsink */, gpointer user_data)
        {
            auto self = static_cast< GStreamerToPothosRunState* >(user_data);
            self->m_bufferCount++;
            return GST_FLOW_OK;
        }

        static GstFlowReturn callBack_new_sample(GstAppSink */* appsink */, gpointer user_data)
        {
            auto self = static_cast< GStreamerToPothosRunState* >(user_data);
            self->m_bufferCount++;
            return GST_FLOW_OK;
        }

        static GstAppSink* getAppSinkByName(GStreamerSubWorker *gstreamerSubWorker)
        {
            auto element = gstreamerSubWorker->gstreamerBlock()->getPipelineElementByName( gstreamerSubWorker->name() );
            if ( !GST_IS_APP_SINK( element.get() ) )
            {
                throw Pothos::NullPointerException("GStreamerToPothosRunState::getAppSinkByName", "Could not find a GstAppSink named \"" + gstreamerSubWorker->name() + "\"");
            }
            return reinterpret_cast< GstAppSink* >( element.release() );
        }

        static Pothos::DType gstAudioInfoToDtype(const GstAudioInfo *gstAudioInfo)
        {
            if ( GST_AUDIO_INFO_WIDTH( gstAudioInfo ) != GST_AUDIO_INFO_DEPTH( gstAudioInfo ) )
            {
                poco_warning(
                    GstTypes::logger(),
                    "GST_AUDIO_INFO_WIDTH( gstAudioInfo ) = " + std::to_string( GST_AUDIO_INFO_WIDTH( gstAudioInfo ) ) + " and " +
                    "GST_AUDIO_INFO_DEPTH( gstAudioInfo ) = " + std::to_string( GST_AUDIO_INFO_DEPTH( gstAudioInfo ) ) + " do not match, this may produce garbel data."
                );
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
            else if ( GST_AUDIO_INFO_IS_FLOAT( gstAudioInfo ) )
            {
                format += "float";
            }

            format += std::to_string( GST_AUDIO_INFO_WIDTH( gstAudioInfo ) );

            return Pothos::DType( format, GST_AUDIO_INFO_CHANNELS( gstAudioInfo ) );
        }

        void capsToMetaInfo(GstCaps* caps)
        {
            if ( caps == nullptr )
            {
                m_dtype = Pothos::DType();
                m_rxRateLabel = Pothos::Label();
                return;
            }

            GstAudioInfo gstAudioInfo;
            if ( gst_audio_info_from_caps(&gstAudioInfo, caps) == TRUE )
            {
                if ( gstAudioInfo.layout != GST_AUDIO_LAYOUT_INTERLEAVED )
                {
                    poco_warning(GstTypes::logger(), "We do not support non INTERLEAVED data");
                }
                //poco_information(GstTypes::logger(), std::string("gstAudioInfo.finfo->name: ") + gstAudioInfo.finfo->name);
                m_dtype = gstAudioInfoToDtype( &gstAudioInfo );
                m_rxRateLabel = Pothos::Label("rxRate", Pothos::Object( GST_AUDIO_INFO_RATE( &gstAudioInfo ) ), 0);
            }
            else
            {
                m_dtype = Pothos::DType();
                m_rxRateLabel = Pothos::Label();
            }
        }
    public:
        GStreamerToPothosRunState() = delete;
        GStreamerToPothosRunState(const GStreamerToPothosRunState&) = delete;
        GStreamerToPothosRunState& operator=(const GStreamerToPothosRunState&) = delete;
        GStreamerToPothosRunState(GStreamerToPothosRunState&&) = delete;
        GStreamerToPothosRunState& operator=(GStreamerToPothosRunState&&) = delete;

        explicit GStreamerToPothosRunState(GStreamerSubWorker *gstreamerSubWorker) :
            m_gstAppSink( getAppSinkByName( gstreamerSubWorker ) ),
            m_bufferCount( 0 ),
            m_dtype(),
            m_rxRateLabel(),
            m_eosChanged( false ),
            m_eos( false )
        {
            /* Limit number of buffer to queue (Prevent memory runaway). */
            gst_app_sink_set_max_buffers(m_gstAppSink.get(), 20);

            GstAppSinkCallbacks gstAppSinkCallbacks{
                &callBack_eos,
                &callBack_new_preroll,
                &callBack_new_sample,
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

        GstSample* tryPullSample( GstClockTime timeout )
        {
            const auto currentEos = ( gst_app_sink_is_eos( m_gstAppSink.get() ) == TRUE );
            if (currentEos != m_eos)
            {
                m_eosChanged = true;
                m_eos = currentEos;
            }
            GstSample *gstSample = gst_app_sink_try_pull_sample( m_gstAppSink.get(), timeout );
            if (gstSample != nullptr)
            {
                m_bufferCount--;
            }
            return gstSample;
        }

        Pothos::Packet createPacketFromGstSample(GstSample* gstSample)
        {
            // Get GStreamer buffer and create Pothos packet from it
            auto packet = GstTypes::makePacketFromGstSample( gstSample, &m_gstCapsCach );

            if ( m_gstCapsCach.change() )
            {
                auto caps = gst_sample_get_caps( gstSample );
                capsToMetaInfo(caps);
            }

            // If m_rxRateLabel valid add it to the packet
            if ( !m_rxRateLabel.id.empty() )
            {
                packet.labels.push_back( m_rxRateLabel );
            }
            packet.payload.dtype = m_dtype;

            return packet;
        }
    };  // class GStreamerToPothosRunState

    #define check_run_state_ptr() do { \
        if ( !m_runState ) throw Pothos::NullPointerException("Not in running state: " + this->gstreamerBlock()->getPipelineString(), std::string( (std::decay< decltype( __func__ )>::type)& __func__ ) ); \
    } while ( false )

    class GStreamerToPothosImpl : public GStreamerSubWorker
    {
    private:
        Pothos::OutputPort *m_pothosOutputPort;
        std::unique_ptr< GStreamerToPothosRunState > m_runState;

    public:
        GStreamerToPothosImpl(const GStreamerToPothosImpl&) = delete;             // No copy constructor
        GStreamerToPothosImpl& operator= (const GStreamerToPothosImpl&) = delete; // No assignment operator

        GStreamerToPothosImpl(GStreamer* gstreamerBlock, GstAppSink* gstAppSink) :
            GStreamerSubWorker( gstreamerBlock, GstTypes::gcharToString( GstTypes::GCharPtr( gst_element_get_name( gstAppSink ) ).get() ).value() ),
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

            std::unique_ptr< GstSample, GstTypes::detail::Deleter< GstSample, gst_sample_unref > > gstSample(
                m_runState->tryPullSample( maxTimeoutNs * GST_NSECOND )
            );

            if ( gstSample )
            {
                packet = m_runState->createPacketFromGstSample( gstSample.get() );
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
