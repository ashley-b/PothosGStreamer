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

#include <Pothos/Framework.hpp>
#include <Poco/Logger.h>
#include <Pothos/Exception.hpp>
#include <gst/gst.h>
#include <vector>
#include <sstream>
#include "GStreamer.hpp"
#include "GStreamerTypes.hpp"
#include "PothosToGStreamer.hpp"
#include "GStreamerToPothos.hpp"
#include "GStreamerStatic.hpp"

const std::string signalBusName( "bus" );
const std::string signalTag( "busTag" );
const std::string signalEosName( "eos" );

GStreamer::GStreamer(const std::string &pipelineString) :
    m_pipeline_string( pipelineString ),
    m_pipeline( nullptr ),
    m_bus( nullptr ),
    m_gstreamerSubWorkers( ),
    m_blockingNodes( 0 ),
    m_pipeline_active( false ),
    m_gstState( GST_STATE_PLAYING )
{
    if ( GstStatic::getInitError() != nullptr )
    {
        throw Pothos::RuntimeException( "GStreamer::GStreamer()", "Could not initialize GStreamer library: " + GstTypes::gerrorToString( GstStatic::getInitError() ) );
    }

    poco_information( GstTypes::logger(), "GStreamer version: " + GstTypes::gcharToString( GstTypes::GCharPtr( gst_version_string() ).get() ) );

    poco_information( GstTypes::logger(), "About to create pipeline: " + m_pipeline_string );

    // Try to create GStreamer pipeline from given string
    createPipeline();

    // Iterate through pipeline and try and find AppSink(s) and AppSource(s)
    findSourcesAndSinks( GST_BIN( m_pipeline ) );

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
    destroyPipeline();
}

Pothos::Block *GStreamer::make(const std::string &pipelineString)
{
    return new GStreamer( pipelineString );
}

GstElement* GStreamer::getPipelineElementByName(const std::string &name) const
{
    return gst_bin_get_by_name( GST_BIN( getPipeline() ), name.c_str() );
}

void GStreamer::for_each_pipeline_element(const GValue *value, gpointer data)
{
    auto gstElement = GST_ELEMENT( g_value_get_object( value ) );
    if ( gstElement == nullptr )
    {
        poco_warning(GstTypes::logger(), "for_each_pipeline_element: object is not a GstElement");
        return;
    }

    auto gstreamer = static_cast< GStreamer* >( data );

    {
        auto PothosToGStreamer = PothosToGStreamer::makeIfType( gstreamer, gstElement );
        if ( PothosToGStreamer )
        {
            gstreamer->m_gstreamerSubWorkers.push_back( std::move( PothosToGStreamer ) );
            return;
        }
    }

    {
        auto gstreamerToPothos = GStreamerToPothos::makeIfType( gstreamer, gstElement );
        if ( gstreamerToPothos )
        {
            gstreamer->m_gstreamerSubWorkers.push_back( std::move( gstreamerToPothos ) );
            return;
        }
    }
}

void GStreamer::findSourcesAndSinks(GstBin *bin)
{
    const std::string funcName("GStreamer::findSourcesAndSinks(GstBin *bin)");
    GstIterator *gstIterator = gst_bin_iterate_elements( bin );
    if ( gstIterator == nullptr )
    {
        throw Pothos::Exception( funcName, "gst_bin_iterate_elements returned null" );
    }

    constexpr int RESYNC_RETRY = 1000;
    for (int i = 0; i < RESYNC_RETRY; ++i)
    {
        auto gstIteratorRes = gst_iterator_foreach( gstIterator, for_each_pipeline_element, this );
        if ( gstIteratorRes == GST_ITERATOR_RESYNC )
        {
            gst_iterator_resync( gstIterator );
            continue;
        }

        gst_iterator_free( gstIterator );

        if ( gstIteratorRes == GST_ITERATOR_ERROR )
        {
            throw Pothos::Exception( funcName, "Error iterating elements to find sources and sinks: " + std::to_string( gstIteratorRes ) );
        }

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
    // If we made it here we had too many retries of GST_ITERATOR_RESYNC
    throw Pothos::RuntimeException( funcName, "Too many GST_ITERATOR_RESYNC iterating elements to find sources and sinks" );
}

void GStreamer::createPipeline()
{
    const std::string funcName("GStreamer::createPipeline");

    GstTypes::GErrorPtr errorPtr;
    auto element = gst_parse_launch_full( m_pipeline_string.c_str(), nullptr, GST_PARSE_FLAG_FATAL_ERRORS, GstTypes::uniquePtrRef(errorPtr) );

    const std::string errorMessage = GstTypes::gerrorToString( errorPtr.get() );

    if ( element == nullptr )
    {
        destroyPipeline();

        const std::string message( "Failed to parse pipeline ( " + m_pipeline_string + " ). Error: " + errorMessage );
        throw Pothos::InvalidArgumentException( funcName, message );
    }

    if ( errorPtr )
    {
        poco_warning( GstTypes::logger(), "Created pipeline, but had warning: " + errorMessage );
    }

    // If the element is not a pipeline bail
    if ( GST_IS_PIPELINE( element ) == FALSE )
    {
        gst_object_unref( GST_OBJECT( element ) );
        const std::string message( "Error in pipeline description, element can not be converted a pipeline" );
        poco_error( GstTypes::logger(), message );

        destroyPipeline();

        throw Pothos::InvalidArgumentException( funcName, message );
    }

    m_pipeline = reinterpret_cast< GstPipeline* >( element );

    m_bus = gst_pipeline_get_bus( m_pipeline );
    if ( m_bus == nullptr )
    {
        poco_error( GstTypes::logger(), "Failed to create GStreamer bus: " );
        destroyPipeline();

        throw Pothos::RuntimeException( funcName, "Failed to get bus for GStreamer pipeline." );
    }
}

void GStreamer::destroyPipeline()
{
    if ( m_pipeline == nullptr )
    {
        return;
    }

    //poco_information( GstTypes::logger(), "GStreamer::cleanup() - Pre state change" );
    // TODO/FIXME: Some times change state hangs(blocks). This function is not ment to block from the GStreamer docs.
    // Should we safe guard our selfs from this or  just let it crash Pothos ?????

    // Shutdown the pipeline and free any state info in it
    // N.B. This must be done so gst_object_unref actually free the object
    gstChangeState( GST_STATE_NULL, false );

    //poco_information( GstTypes::logger(), "GStreamer::cleanup() - Post state change" );

    if ( m_bus != nullptr )
    {
        // Process any GStreamer messages left on the bus so we can print any errors.
        processGStreamerMessagesTimeout( 100 * GST_MSECOND );

        gst_object_unref( m_bus );
        m_bus = nullptr;
    }

    // Free GStreamer pipeline
    gst_object_unref( GST_OBJECT( m_pipeline ) );
    m_pipeline = nullptr;
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
            gst_element_set_state ( reinterpret_cast< GstElement* >( m_pipeline ), GST_STATE_PAUSED);
            gst_element_set_state ( reinterpret_cast< GstElement* >( m_pipeline ), GST_STATE_PLAYING);

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
                    const GstFormatDefinition *gstFormatDefinition = gst_format_get_details( format );

                    stats[ "format"    ] = ( gstFormatDefinition != nullptr ) ? GstTypes::gcharToObject( gstFormatDefinition->nick ) : Pothos::Object( format );
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
            auto objectMsgMap = GstTypes::objectFrom( tags );
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
            objectMsgMap[ "value" ] = GstTypes::objectFrom( property_value );

            return objectMsgMap;
        }

        case GST_MESSAGE_SEGMENT_DONE:
        {
            GstFormat format;
            gint64 position;
            gst_message_parse_segment_done( gstMessage, &format, &position );

            Pothos::ObjectKwargs objectMsgMap;
            objectMsgMap[ "format"   ] = GstTypes::gcharToObject( gst_format_get_name( format ) );
            objectMsgMap[ "position" ] = Pothos::Object( position );

            return objectMsgMap;
        }

        case GST_MESSAGE_LATENCY:
        {
            gst_bin_recalculate_latency( GST_BIN( m_pipeline ) );
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
                    gst_element_set_state( GST_ELEMENT( m_pipeline ), GST_STATE_PAUSED);
                    poco_information( GstTypes::logger(), ">Switching to paused while buffering" );
                }
                else
                {
                    poco_information( GstTypes::logger(), ">Restarting pipe line" );
                    gst_element_set_state( GST_ELEMENT( m_pipeline ), GST_STATE_PLAYING);
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
        Pothos::Object structureString;
        Pothos::Object structureObject;
        if ( structure != nullptr )
        {
            structureString = GstTypes::gcharToObject( GstTypes::GCharPtr( gst_structure_to_string( structure ) ).get() );
            structureObject = GstTypes::objectFrom( structure );
        }

        objectMap[ "structureString" ] = structureString;
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
                m_bus,
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
}

void GStreamer::setState(const std::string &state)
{
    static const std::map< std::string, GstState > stateMap =
    {
        { "PLAY",  GST_STATE_PLAYING },
        { "PAUSE", GST_STATE_PAUSED  }
    };

    const auto it = stateMap.find( state );

    // Throw argument exception if state not found
    if ( it == stateMap.cend() )
    {
        const std::string options(
            GstTypes::joinStrings(stateMap.begin(), stateMap.end(), ", ", [](decltype(stateMap)::value_type value)->std::string{ return value.first; } )
        );
        throw Pothos::InvalidArgumentException("GStreamer::setState("+state+")", "Unknown state, can be "+options);
    }
    // Can only change the state if the pipeline is running
    if ( m_pipeline_active )
    {
        this->gstChangeState( it->second, true );
    }
    m_gstState = it->second;
}

std::string GStreamer::getPipelineString() const
{
    return m_pipeline_string;
}

GstPipeline* GStreamer::getPipeline() const
{
    return m_pipeline;
}

inline Pothos::Object GstClockTimeToObject(const GstClockTime gstClockTime)
{
    return (gstClockTime == GST_CLOCK_TIME_NONE) ? Pothos::Object() : Pothos::Object(gstClockTime);
}

Pothos::Object GStreamer::getPipelineLatency() const
{
    std::unique_ptr < GstQuery, GstTypes::Deleter< GstQuery, gst_query_unref > > query( gst_query_new_latency() );
    auto res = gst_element_query( GST_ELEMENT( m_pipeline ), query.get() );
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

static const std::map< std::string, GstFormat > formatMap
{
    { "DEFAULT"  , GST_FORMAT_DEFAULT },
    { "BYTES"    , GST_FORMAT_BYTES   },
    { "TIME"     , GST_FORMAT_TIME    },
    { "BUFFERS"  , GST_FORMAT_BUFFERS },
    { "PERCENT"  , GST_FORMAT_PERCENT }
};

int64_t GStreamer::getPipelinePosition(const std::string &format) const
{
    const auto it = formatMap.find( format );
    // Throw argument exception if format not found
    if ( it == formatMap.cend() )
    {
        const std::string options(
            GstTypes::joinStrings(
                formatMap.begin(),
                formatMap.end(),
                ", ",
                [](decltype(formatMap)::value_type value)->std::string{ return value.first; }
            )
        );

        throw Pothos::InvalidArgumentException("GStreamer::getPipelinePosition("+format+")", "Unknown format, can be "+options);
    }

    gint64 position;
    if ( gst_element_query_position(GST_ELEMENT( m_pipeline ), it->second, &position) == TRUE )
    {
        return position;
    }
    throw Pothos::PropertyNotSupportedException("GStreamer::getPipelinePosition("+format+")", format + " not supported");
    return -1;
}

int64_t GStreamer::getPipelineDuration(const std::string &format) const
{
    const auto it = formatMap.find( format );
    // Throw argument exception if format not found
    if ( it == formatMap.cend() )
    {
        const std::string options(
            GstTypes::joinStrings(
                formatMap.begin(),
                formatMap.end(),
                ", ",
                [](decltype(formatMap)::value_type value)->std::string{ return value.first; }
            )
        );

        throw Pothos::InvalidArgumentException("GStreamer::getPipelineDuration("+format+")", "Unknown format, can be "+options);
    }

    gint64 duration;
    if ( gst_element_query_duration(GST_ELEMENT( m_pipeline ), it->second, &duration) == TRUE )
    {
        return duration;
    }
    throw Pothos::PropertyNotSupportedException("GStreamer::getPipelineDuration("+format+")", format + " not supported");
    return -1;
}

void GStreamer::gstChangeState( GstState state, bool throwError )
{
    const std::string funcName( "GStreamer::gstChangeState()" );
    if ( m_pipeline == nullptr )
    {
        poco_warning( GstTypes::logger(), funcName + " Will do nothing as pipeline is NULL" );
        return;
    }

    const auto stateChangeReturn = gst_element_set_state( GST_ELEMENT( m_pipeline ), state );

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
            poco_information(
                GstTypes::logger(),
                funcName +
                    " The state change succeeded but the element cannot produce data in GST_STATE_PAUSED."
                    "This typically happens with live sources."
            );
            poco_information(
                GstTypes::logger(),
                funcName + " Current state: " + gst_element_state_get_name( state ) + " . Pipeline: " + getPipelineString() );
            return;
        default:
            errorStr = gst_element_state_change_return_get_name( stateChangeReturn );
            break;
    }
    poco_error( GstTypes::logger(), funcName + " error: "+errorStr+". Pipeline: "+getPipelineString() );

    if ( throwError )
    {
        throw Pothos::RuntimeException( funcName, "GStreamer state change error: " + errorStr );
    }
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
        gstChangeState( m_gstState, true );
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
