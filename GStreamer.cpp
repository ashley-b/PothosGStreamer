/// Copyright (c) 2017-2018 Ashley Brighthope
/// SPDX-License-Identifier: BSL-1.0

/***********************************************************************
 * |PothosDoc GStreamer Wrapper
 *
 * Simple Wrapper to allow access to GStreamer processing.<br>
 * For more details about GStreamer visit <a href="https://gstreamer.freedesktop.org">https://gstreamer.freedesktop.org</a>
 *
 * |category /Media
 * |keywords audio sound video picture codec
 *
 * |param pipelineString[Pipeline description] <p>String representation of a GStreamer pipeline</p>
 * <p>Ports from Pothos to GStreamer are added by specifying a <strong>appsrc name=appsrc_name</strong> at the start of the pipeline or<br>
 * from GStreamer to Pothos by specifying a <strong>appsink name=appsink_name</strong> at the end of the pipeline.</p>
 * <p>The name argument in the pipeline is used to identify the GStreamer block and will be used to name the Pothos port that is linked to that block.</p>
 *
 * Examples
 * <ul>
 *   <li>Passthrough with named input and output:<br>
 *     <code>appsrc name=in ! appsink name=out</code></li>
 *   <li>Example audio source with 1kHz sinewave, signed 8 bit, single channel at 44100 samples per second:<br>
 *     <code>audiotestsrc wave=sine freq=1000 ! audio/x-raw,format=S8,channels=1,rate=44100 ! appsink name=out</code></li>
 * </ul>
 * More example pipelines can be found below
 * <ul>
 *   <li><a href="https://gstreamer.freedesktop.org/documentation/tools/gst-launch.html#pipeline-examples">gstreamer.freedesktop.org/documentation/tools/gst-launch.html#pipeline-examples</a></li>
 *   <li><a href="http://wiki.oz9aec.net/index.php/Gstreamer_cheat_sheet">GStreamer cheat sheet</a></li>
 * </ul>
 * |default "appsrc name=in ! appsink name=out"
 *
 * |param state[State] Changes the state of the pipeline
 * <ul>
 *   <li>"PLAY" - Start the pipeline playing</li>
 *   <li>"PAUSE" - Pause the pipeline playing</li>
 * </ul>
 * |default "PLAY"
 * |option [Play] "PLAY"
 * |option [Pause] "PAUSE"
 * |preview disable
 *
 * |factory /media/gstreamer(pipelineString)
 * |setter setState(state)
 **********************************************************************/

#include "GStreamer.hpp"
#include "GStreamerStatic.hpp"
#include "GStreamerToPothos.hpp"
#include "GStreamerTypes.hpp"
#include "PothosToGStreamer.hpp"
#include <Poco/Logger.h>
#include <Pothos/Exception.hpp>
#include <Pothos/Framework.hpp>
#include <gst/gst.h>
#include <sstream>
#include <vector>

const std::string signalBusName( "bus" );
const std::string signalTag( "busTag" );
const std::string signalEosName( "eos" );

GStreamer::GStreamer(const std::string &pipelineString) :
    m_pipeline_string( pipelineString ),
    m_pipeline( ),
    m_bus( ),
    m_gstreamerSubWorkers( ),
    m_blockingNodes( 0 ),
    m_pipeline_active( false ),
    m_gstState( GST_STATE_PLAYING )
{
    if ( GstStatic::getInitError() != nullptr )
    {
        throw Pothos::RuntimeException( "GStreamer::GStreamer()", "Could not initialize GStreamer library: " + GstTypes::gerrorToString( GstStatic::getInitError() ) );
    }

    poco_information( GstTypes::logger(), "GStreamer version: " + GstStatic::getVersion() );

    poco_information( GstTypes::logger(), "About to create pipeline: " + m_pipeline_string );

    // Try to create GStreamer pipeline from given string
    createPipeline();

    // Iterate through pipeline and try and find AppSink(s) and AppSource(s)
    findSourcesAndSinks( GST_BIN( m_pipeline.get() ) );

    this->registerSignal( signalBusName );
    this->registerSignal( signalTag );
    this->registerSignal( signalEosName );

    this->registerCall(this, POTHOS_FCN_TUPLE(GStreamer, getPipelineString));
    this->registerCall(this, POTHOS_FCN_TUPLE(GStreamer, setState));
    this->registerCall(this, POTHOS_FCN_TUPLE(GStreamer, getPipeline));
    this->registerCall(this, POTHOS_FCN_TUPLE(GStreamer, getPipelineString));
    this->registerCall(this, POTHOS_FCN_TUPLE(GStreamer, getPipelineLatency));
    this->registerCall(this, POTHOS_FCN_TUPLE(GStreamer, getPipelinePosition));
    this->registerCall(this, POTHOS_FCN_TUPLE(GStreamer, getPipelineDuration));

    this->registerProbe("getPipelineLatency");
    this->registerProbe("getPipelinePosition");
    this->registerProbe("getPipelineDuration");
}

GStreamer::~GStreamer()
{
    POTHOS_EXCEPTION_TRY
    {
        destroyPipeline();
    }
    POTHOS_EXCEPTION_CATCH (const Pothos::Exception &e)
    {
        poco_error( GstTypes::logger(), e.displayText() + ". Pipeline:  " + getPipelineString() );
    }
}

Pothos::Block *GStreamer::make(const std::string &pipelineString)
{
    return new GStreamer( pipelineString );
}

GstElement* GStreamer::getPipelineElementByName(const std::string &name) const
{
    return gst_bin_get_by_name( GST_BIN( getPipeline() ), name.c_str() );
}

template< class Fn>
GstIteratorResult gstIteratorForeach(GstIterator* gstIterator, Fn f)
{
    GstTypes::GVal item;
    GstIteratorResult result;

    while ( (result = gst_iterator_next(gstIterator, &item.value)) == GST_ITERATOR_OK )
    {
        // RAII safe guard to call g_value_reset between iterations
        struct ValueUnset final
        {
            GstTypes::GVal *m_value;
            explicit ValueUnset(GstTypes::GVal *value) noexcept : m_value(value)
            {
            }
            ~ValueUnset()
            {
                m_value->reset();
            }
        } value_unset(&item);

        f(&item.value);
    }

    return result;
}

void GStreamer::findSourcesAndSinks(GstBin *bin)
{
    const std::string funcName("GStreamer::findSourcesAndSinks(GstBin *bin)");
    GstTypes::GstIteratorPtr gstIterator( gst_bin_iterate_elements( bin ) );
    if ( gstIterator.get() == nullptr )
    {
        throw Pothos::RuntimeException( funcName, "gst_bin_iterate_elements returned null" );
    }

    auto forEachElement = [ this ] (const GValue *value)
    {
        auto gstElement = GST_ELEMENT( g_value_get_object( value ) );
        if ( gstElement == nullptr )
        {
            poco_warning(GstTypes::logger(), "for_each_pipeline_element: object is not a GstElement");
            return;
        }

        {
            auto pothosToGStreamer = PothosToGStreamer::makeIfType( this, gstElement );
            if ( pothosToGStreamer )
            {
                this->m_gstreamerSubWorkers.push_back( std::move( pothosToGStreamer ) );
                return;
            }
        }

        {
            auto gstreamerToPothos = GStreamerToPothos::makeIfType( this, gstElement );
            if ( gstreamerToPothos )
            {
                this->m_gstreamerSubWorkers.push_back( std::move( gstreamerToPothos ) );
                return;
            }
        }
    };

    const auto gstIteratorRes = gstIteratorForeach(gstIterator.get(), forEachElement);
    switch ( gstIteratorRes )
    {
        case GST_ITERATOR_RESYNC:
        {
            throw Pothos::RuntimeException( funcName, "Error iterating elements to find sources and sinks: GST_ITERATOR_RESYNC" );
        }

        case GST_ITERATOR_ERROR:
        {
            throw Pothos::RuntimeException( funcName, "Error iterating elements to find sources and sinks: GST_ITERATOR_ERROR" );
        }

        case GST_ITERATOR_OK:
        case GST_ITERATOR_DONE:
        {
            // Count how many blocking sub workers we have for timing
            m_blockingNodes = 0;
            for (auto &subWorker : m_gstreamerSubWorkers )
            {
                if ( subWorker->blocking() )
                {
                    ++m_blockingNodes;
                }
            }
            return;
        }
        default:
            throw Pothos::RuntimeException( funcName, "Unkown return code from gst_iterator_foreach " + std::to_string( gstIteratorRes ) );
    }
}

void GStreamer::createPipeline()
{
    const std::string funcName("GStreamer::createPipeline");

    GstTypes::GErrorPtr errorPtr;
    std::unique_ptr< GstElement, GstTypes::GstObjectUnrefFunc > element(
        gst_parse_launch_full( m_pipeline_string.c_str(), nullptr, GST_PARSE_FLAG_FATAL_ERRORS, GstTypes::uniquePtrRef(errorPtr) )
    );

    const std::string errorMessage( GstTypes::gerrorToString( errorPtr.get() ) );

    if ( !element )
    {
        const std::string message( "Failed to parse pipeline ( " + m_pipeline_string + " ). Error: " + errorMessage );
        throw Pothos::InvalidArgumentException( funcName, message );
    }

    if ( errorPtr )
    {
        poco_warning( GstTypes::logger(), "Created pipeline, but had warning: " + errorMessage );
    }

    // If the element is not a pipeline bail
    if ( GST_IS_PIPELINE( element.get() ) == FALSE )
    {
        const std::string message( "Error in pipeline description, element can not be converted a pipeline" );
        poco_error( GstTypes::logger(), message );

        throw Pothos::InvalidArgumentException( funcName, message );
    }

    m_pipeline.reset( reinterpret_cast< GstPipeline* >( element.release() ) );

    m_bus.reset( gst_pipeline_get_bus( m_pipeline.get() ) );
    if ( !m_bus )
    {
        poco_error( GstTypes::logger(), "Failed to create GStreamer bus: " );
        destroyPipeline();

        throw Pothos::RuntimeException( funcName, "Failed to get bus for GStreamer pipeline." );
    }
}

void GStreamer::destroyPipeline()
{
    if ( !m_pipeline )
    {
        return;
    }

    std::exception_ptr exceptionPtr;
    try
    {
        // Shutdown the pipeline and alow GStreamer to free any resources.
        // N.B. This must be done so gst_object_unref actually free the object
        gstChangeState( GST_STATE_NULL );
    }
    catch (...)
    {
        exceptionPtr = std::current_exception();
    }

    if ( m_bus )
    {
        // Process any GStreamer messages left on the bus so we can print any errors.
        processGStreamerMessagesTimeout( 100 * GST_MSECOND );

        // Free Bus
        m_bus.reset();
    }

    // Free GStreamer pipeline
    m_pipeline.reset();

    if ( exceptionPtr )
    {
        std::rethrow_exception( exceptionPtr );
    }
}

Pothos::ObjectKwargs GStreamer::gstMessageInfoWarnError( GstMessage *message )
{
    GstTypes::GErrorPtr errorPtr;
    GstTypes::GCharPtr dbgInfoPtr;

    using PriorityInfo = std::pair< const char *, Poco::Message::Priority >;

    const auto logLevel = [ &errorPtr, &dbgInfoPtr, message ]() -> PriorityInfo
    {
        switch ( GST_MESSAGE_TYPE( message ) )
        {
            case GST_MESSAGE_ERROR:
            {
                gst_message_parse_error( message, GstTypes::uniquePtrRef( errorPtr ), GstTypes::uniquePtrRef( dbgInfoPtr ) );
                return { "ERROR", Poco::Message::PRIO_ERROR };
            }
            case GST_MESSAGE_WARNING:
            {
                gst_message_parse_warning( message, GstTypes::uniquePtrRef( errorPtr ), GstTypes::uniquePtrRef( dbgInfoPtr ) );
                return { "WARNING", Poco::Message::PRIO_WARNING };
            }
            case GST_MESSAGE_INFO:
            {
                gst_message_parse_info( message, GstTypes::uniquePtrRef( errorPtr ), GstTypes::uniquePtrRef( dbgInfoPtr ) );
                return { "INFO", Poco::Message::PRIO_INFORMATION };
            }
            default:
                throw Pothos::RuntimeException("GStreamer::gstMessageInfoWarnError", "GStreamer message does not contains a GError");
        }
    } ( );

    const std::string prefix( std::string( "GStreamer " ) + logLevel.first + ": " );

    GstTypes::GCharPtr objectName( gst_object_get_name( message->src ) );
    std::ostringstream error_message;
    error_message << prefix;
    error_message << "From element: " << objectName.get();
    error_message << ", Code: " << errorPtr->code;
    error_message << ", Message: " << errorPtr->message;

    Pothos::ObjectKwargs objectMap;
    objectMap[ "message" ] = GstTypes::gcharToObject( errorPtr->message );
    objectMap[ "code"    ] = Pothos::Object( errorPtr->code );

    std::ostringstream debug_message;
    debug_message << prefix << "Debugging info: " << (( dbgInfoPtr ) ? dbgInfoPtr.get() : "none");

    objectMap[ "debug_message" ] = GstTypes::gcharToObject( dbgInfoPtr.get() );

    GstTypes::logger().log( Poco::Message( GstTypes::logger().name(), error_message.str(), logLevel.second ) );
    GstTypes::logger().log( Poco::Message( GstTypes::logger().name(), debug_message.str(), logLevel.second ) );

    return objectMap;
}

static Pothos::ObjectKwargs clockInfo(GstClock *clock)
{
    Pothos::ObjectKwargs objectMsgMap;
    GstTypes::GCharPtr objectName( gst_object_get_name( reinterpret_cast< GstObject* >(clock) ) );
    objectMsgMap[ "name"       ] = GstTypes::gcharToObject( objectName.get() );
    objectMsgMap[ "resolution" ] = Pothos::Object( gst_clock_get_resolution( clock ) );
    return objectMsgMap;
}

void GStreamer::workerStop(const std::string & reason)
{
    // Stop pipeline if there was an error
    m_pipeline_active = false;

    poco_warning( GstTypes::logger(), "Pipeline has stopped, reason: " + reason + ". This block will do nothing for the remainder of this running topology" );
}

Pothos::ObjectKwargs GStreamer::gstMessageToFormattedObject(GstMessage *gstMessage)
{
    switch ( gstMessage->type )
    {
        case GST_MESSAGE_WARNING:
        {
            return gstMessageInfoWarnError( gstMessage );
        }

        case GST_MESSAGE_ERROR:
        {
            workerStop( "GStreamer Error" );
            return gstMessageInfoWarnError( gstMessage );
        }

        case GST_MESSAGE_INFO:
        {
            return gstMessageInfoWarnError( gstMessage );
        }

        case GST_MESSAGE_STATE_CHANGED:
        {
            GstState old_state, new_state, pending;

            gst_message_parse_state_changed( gstMessage, &old_state, &new_state, &pending );

            Pothos::ObjectKwargs objectMsgMap;
            objectMsgMap[ "old_state" ] = GstTypes::gcharToObject( gst_element_state_get_name( old_state ) );
            objectMsgMap[ "new_state" ] = GstTypes::gcharToObject( gst_element_state_get_name( new_state ) );
            objectMsgMap[ "pending"   ] = GstTypes::gcharToObject( gst_element_state_get_name( pending ) );

            return objectMsgMap;
        }

        // Apparently this is a good thing to do??
        // Not sure why though. Taken from example
        // https://gstreamer.freedesktop.org/documentation/tutorials/basic/streaming.html
        case GST_MESSAGE_CLOCK_LOST:
        {
            GstClock *clock;
            gst_message_parse_clock_lost( gstMessage, &clock );

            Pothos::ObjectKwargs objectMsgMap( clockInfo( clock ) );

            /* Stop and start pipeline to force new clock selection */
            gst_element_set_state ( reinterpret_cast< GstElement* >( m_pipeline.get() ), GST_STATE_PAUSED);
            gst_element_set_state ( reinterpret_cast< GstElement* >( m_pipeline.get() ), GST_STATE_PLAYING);

            return objectMsgMap;
        }

        case GST_MESSAGE_NEW_CLOCK:
        {
            GstClock *clock;
            gst_message_parse_new_clock( gstMessage, &clock );
            return clockInfo( clock );
        }

        case GST_MESSAGE_EOS:
        {
            workerStop( "End of stream" );
            return Pothos::ObjectKwargs();
        }

        case GST_MESSAGE_QOS:
        {
            Pothos::ObjectKwargs qos;
            {
                gboolean live;
                guint64 running_time;
                guint64 stream_time;
                guint64 timestamp;
                guint64 duration;
                gst_message_parse_qos(gstMessage, &live, &running_time, &stream_time, &timestamp, &duration);

                qos[ "live"         ] = Pothos::Object( static_cast< bool >( live ) );
                qos[ "running_time" ] = Pothos::Object( running_time );
                qos[ "stream_time"  ] = Pothos::Object( stream_time );
                qos[ "timestamp"    ] = Pothos::Object( timestamp );
                qos[ "duration"     ] = Pothos::Object( duration );
            }

            Pothos::ObjectKwargs values;
            {
                gint64 jitter;
                double proportion;
                int quality;
                gst_message_parse_qos_values( gstMessage, &jitter, &proportion, &quality );

                values[ "jitter"     ] = Pothos::Object( jitter );
                values[ "proportion" ] = Pothos::Object( proportion );
                values[ "quality"    ] = Pothos::Object( quality );
            }

            Pothos::ObjectKwargs stats;
            {
                GstFormat format;
                guint64 processed;
                guint64 dropped;

                gst_message_parse_qos_stats( gstMessage, &format, &processed, &dropped );

                if ( format != GST_FORMAT_UNDEFINED  )
                {
                    stats[ "format"    ] = Pothos::Object( GstTypes::gstFormatToObjectKwargs( format ) );
                    stats[ "processed" ] = Pothos::Object( processed );
                    stats[ "dropped"   ] = Pothos::Object( dropped );

                    poco_information( GstTypes::logger(), "GStreamer QOS stats: processed "+std::to_string( processed ) +
                                      ", dropped "+std::to_string( dropped ) );
                }
            }
            Pothos::ObjectKwargs objectMsgMap;
            objectMsgMap[ "qos"    ] = Pothos::Object( qos    );
            objectMsgMap[ "values" ] = Pothos::Object( values );
            objectMsgMap[ "stats"  ] = Pothos::Object( stats  );
            return objectMsgMap;
        }

        case GST_MESSAGE_TAG:
        {
            GstTagList *tags = nullptr;
            gst_message_parse_tag( gstMessage, &tags );
            auto objectMsgMap = GstTypes::tagListToObjectKwargs( tags );
            gst_tag_list_unref( tags );
            return objectMsgMap;
        }

        case GST_MESSAGE_PROPERTY_NOTIFY:
        {
            GstObject *object;
            const gchar *property_name;
            const GValue *property_value;
            gst_message_parse_property_notify(
                gstMessage,
                &object,
                &property_name,
                &property_value
            );

            Pothos::ObjectKwargs objectMsgMap;
            objectMsgMap[ "name"  ] = GstTypes::gcharToObject( property_name );
            objectMsgMap[ "value" ] = GstTypes::gvalueToObject( property_value );

            return objectMsgMap;
        }

        case GST_MESSAGE_SEGMENT_DONE:
        {
            GstFormat format;
            gint64 position;
            gst_message_parse_segment_done( gstMessage, &format, &position );

            Pothos::ObjectKwargs objectMsgMap;
            objectMsgMap[ "format"   ] = Pothos::Object( GstTypes::gstFormatToObjectKwargs( format ) );
            objectMsgMap[ "position" ] = Pothos::Object( position );

            return objectMsgMap;
        }

        case GST_MESSAGE_LATENCY:
        {
            gst_bin_recalculate_latency( GST_BIN( m_pipeline.get() ) );
            return Pothos::ObjectKwargs();
        }

        case GST_MESSAGE_BUFFERING:
        {
            Pothos::ObjectKwargs objectMsgMap;
            gint percent;
            gst_message_parse_buffering( gstMessage, &percent );

            objectMsgMap[ "percent" ] = Pothos::Object( percent );

            // FIXME: This does not work very well, some times stalls the pipeline.
            if ( false )
            {
                /* Wait until buffering is complete before start/resume playing */
                if (percent < 60)
                {
                    poco_information( GstTypes::logger(), ">Switching to paused while buffering" );
                    gst_element_set_state( GST_ELEMENT( m_pipeline.get() ), GST_STATE_PAUSED);
                    poco_information( GstTypes::logger(), ">Switching to paused while buffering" );
                }
                else
                {
                    poco_information( GstTypes::logger(), ">Restarting pipe line" );
                    gst_element_set_state( GST_ELEMENT( m_pipeline.get() ), GST_STATE_PLAYING);
                    poco_information( GstTypes::logger(), "<Restarting pipe line" );
                }
            }

            return objectMsgMap;
        }

        // To silence the compilers
        default:
        {
            break;
        }
    }
    return Pothos::ObjectKwargs();
}

Pothos::Object GStreamer::gstMessageToObject(GstMessage *gstMessage)
{
    const std::string message_type_name( GST_MESSAGE_TYPE_NAME( gstMessage ) );

    Pothos::ObjectKwargs objectMap;
    objectMap[ "type_name"  ] = Pothos::Object( message_type_name );
    objectMap[ "src_name"   ] = Pothos::Object( GST_MESSAGE_SRC_NAME( gstMessage ) );
    objectMap[ "time_stamp" ] = Pothos::Object( GST_MESSAGE_TIMESTAMP( gstMessage ) );
    objectMap[ "sqenum"     ] = Pothos::Object( gst_message_get_seqnum( gstMessage ) );

    // Debug message handling code
    {
        auto structure = gst_message_get_structure( gstMessage );
        Pothos::Object structureObject;
        if ( structure != nullptr )
        {
            structureObject = Pothos::Object::make( GstTypes::structureToObjectKwargs( structure ) );
        }

        objectMap[ "structureObject" ] = structureObject;
    }

    Pothos::ObjectKwargs objectMsgMap = gstMessageToFormattedObject(gstMessage);

    objectMap[ "body" ] = Pothos::Object::make( objectMsgMap );

    return Pothos::Object( objectMap );
}

void GStreamer::processGStreamerMessagesTimeout(GstClockTime timeout)
{
    while (true)
    {
        using GstMessagePtr = std::unique_ptr < GstMessage, GstTypes::Deleter< GstMessage, gst_message_unref > >;
        GstMessagePtr gstMessage(
            gst_bus_timed_pop(
                m_bus.get(),
                timeout
            )
        );
        // No more message bail
        if ( gstMessage.get() == nullptr )
        {
            return;
        }

        // Only block for time out period on the first loop iteration
        timeout = 0;
        POTHOS_EXCEPTION_TRY
        {
            auto object = gstMessageToObject( gstMessage.get() );

            if ( isActive() )
            {
                // Dedicated signals we send
                switch ( GST_MESSAGE_TYPE( gstMessage.get() ) )
                {
                    case GST_MESSAGE_EOS:
                        this->emitSignal( signalEosName, object );
                        break;
                    case GST_MESSAGE_TAG:
                        this->emitSignal( signalTag, object );
                        break;
                    // To silence the compilers
                    default:
                        break;
                }

                // Push the GStreamer message out as a Pothos signal
                this->emitSignal(signalBusName, object);
            }
        }
        POTHOS_EXCEPTION_CATCH (const Pothos::Exception & e)
        {
            poco_error(GstTypes::logger(), "GStreamer::processGStreamerMessagesTimeout error: " + e.displayText());
        }
    }
}

void GStreamer::setState(const std::string &state)
{
    static const std::array< std::pair< std::string, GstState >, 2 > states =
    { {
        { "PLAY",  GST_STATE_PLAYING },
        { "PAUSE", GST_STATE_PAUSED  }
    } };

    try
    {
        const auto value = GstTypes::findValueByKey( states, state );
        // Can only change the state if the pipeline is running
        if ( m_pipeline_active )
        {
            this->gstChangeState( value );
        }
        m_gstState = value;
    }
    catch (const Pothos::NotFoundException &e)
    {
        throw Pothos::InvalidArgumentException("GStreamer::setState("+state+")", "Unknown state. " + e.message());
    }
}

std::string GStreamer::getPipelineString() const
{
    return m_pipeline_string;
}

GstPipeline* GStreamer::getPipeline() const
{
    return m_pipeline.get();
}

inline Pothos::Object GstClockTimeToObject(const GstClockTime gstClockTime)
{
    return (gstClockTime == GST_CLOCK_TIME_NONE) ? Pothos::Object() : Pothos::Object(gstClockTime);
}

Pothos::Object GStreamer::getPipelineLatency() const
{
    std::unique_ptr < GstQuery, GstTypes::Deleter< GstQuery, gst_query_unref > > query( gst_query_new_latency() );
    const auto res = gst_element_query( GST_ELEMENT( m_pipeline.get() ), query.get() );
    if (res == FALSE)
    {
        return Pothos::Object();
    }

    gboolean live;
    GstClockTime min_latency;
    GstClockTime max_latency;
    gst_query_parse_latency(query.get(), &live, &min_latency, &max_latency);

    Pothos::ObjectKwargs args;
    args["live"       ] = Pothos::Object( static_cast< bool >( live ) );
    args["min_latency"] = GstClockTimeToObject( min_latency );
    args["max_latency"] = GstClockTimeToObject( max_latency );
    return Pothos::Object::make( args );
}

static const std::array< std::pair< std::string, GstFormat >, 5 > formats
{ {
    { "DEFAULT"  , GST_FORMAT_DEFAULT },
    { "BYTES"    , GST_FORMAT_BYTES   },
    { "TIME"     , GST_FORMAT_TIME    },
    { "BUFFERS"  , GST_FORMAT_BUFFERS },
    { "PERCENT"  , GST_FORMAT_PERCENT }
} };

int64_t GStreamer::getPipelinePosition(const std::string &format) const
{
    try
    {
        const auto value = GstTypes::findValueByKey( formats, format );

        gint64 position;
        if ( gst_element_query_position(GST_ELEMENT( m_pipeline.get() ), value, &position) == TRUE )
        {
            return position;
        }
        throw Pothos::PropertyNotSupportedException("GStreamer::getPipelinePosition("+format+")", format + " not supported");
    }
    catch (const Pothos::NotFoundException &e)
    {
        throw Pothos::InvalidArgumentException("GStreamer::getPipelinePosition("+format+")", "Unknown format, can be " + e.message());
    }
}

int64_t GStreamer::getPipelineDuration(const std::string &format) const
{
    try
    {
        const auto value = GstTypes::findValueByKey( formats, format );

        gint64 duration;
        if ( gst_element_query_duration(GST_ELEMENT( m_pipeline.get() ), value, &duration) == TRUE )
        {
            return duration;
        }

        throw Pothos::PropertyNotSupportedException("GStreamer::getPipelineDuration("+format+")", format + " not supported");
    }
    catch (const Pothos::NotFoundException &e)
    {
        throw Pothos::InvalidArgumentException("GStreamer::getPipelineDuration("+format+")", "Unknown format, can be " + e.message());
    }
}

void GStreamer::gstChangeState( GstState state )
{
    const std::string funcName( "GStreamer::gstChangeState()" );
    if ( !m_pipeline )
    {
        poco_warning( GstTypes::logger(), funcName + " Will do nothing as pipeline is NULL" );
        return;
    }

    // TODO: Some times change state hangs(blocks).
    //   This function is not ment to block from the GStreamer docs.
    //   Should we safe guard our selfs from this or just let it crash Pothos ?????
    const auto stateChangeReturn = gst_element_set_state( GST_ELEMENT( m_pipeline.get() ), state );

    std::string errorStr;
    switch ( stateChangeReturn )
    {
        case GST_STATE_CHANGE_FAILURE:
            errorStr = "Change failure";
            break;
        case GST_STATE_CHANGE_SUCCESS:
            //state_change_pipeline_to_dot_file( false );
            return;
        case GST_STATE_CHANGE_ASYNC:
            return;
        case GST_STATE_CHANGE_NO_PREROLL:
            poco_warning(
                GstTypes::logger(),
                funcName +
                    " The state change succeeded but the element cannot produce data in GST_STATE_PAUSED."
                    " This typically happens with live sources."
            );
            poco_information(
                GstTypes::logger(),
                funcName + " Current state: " + gst_element_state_get_name( state ) + " . Pipeline: " + getPipelineString() );
            return;
        default:
            errorStr = gst_element_state_change_return_get_name( stateChangeReturn );
            break;
    }

    throw Pothos::RuntimeException( funcName, "GStreamer state change error: " + errorStr );
}

void GStreamer::activate()
{
    // Recreate pipeline if it got destroyed last time round
    if ( m_pipeline == nullptr )
    {
        createPipeline();
    }

    for (auto &subWorker : m_gstreamerSubWorkers)
    {
        subWorker->activate();
    }

    try
    {
        gstChangeState( m_gstState );
    }
    catch (...)
    {
        // Process any GStreamer messages left on the bus so we can print errors.
        processGStreamerMessagesTimeout( 100 * GST_MSECOND );

        throw;
    }

    m_pipeline_active = true;
}

void GStreamer::deactivate()
{
    for (auto &subWorker : m_gstreamerSubWorkers)
    {
        subWorker->deactivate();
    }

    destroyPipeline();
}

// Don't propagate labels
void GStreamer::propagateLabels(const Pothos::InputPort * /*input*/)
{
}

void GStreamer::work()
{
    if ( m_pipeline_active == false )
    {
        return;
    }

    // Calculate time slices if we have more then one sub-worker
    auto nodeTimeoutNs = this->workInfo().maxTimeoutNs;
    nodeTimeoutNs = (m_blockingNodes != 0) ? (nodeTimeoutNs / m_blockingNodes) : nodeTimeoutNs;

    // Calculate message time out
    const auto gstMessageTimeout = (this->m_blockingNodes == 0) ? (nodeTimeoutNs * GST_NSECOND) : 0;

    // Handle GStreamer messages and forwarding into Pothos via signals
    processGStreamerMessagesTimeout( gstMessageTimeout );

    // Send data to and from GStreamer into Pothos
    for (auto &subWorker : m_gstreamerSubWorkers)
    {
        subWorker->work( nodeTimeoutNs );
    }

    this->yield();
}


// Register GStreamer into the block registry
static Pothos::BlockRegistry registerGStreamerBlock(
    "/media/gstreamer",
    &GStreamer::make
);
