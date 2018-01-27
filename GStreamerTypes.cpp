/// Copyright (c) 2017-2018 Ashley Brighthope
/// SPDX-License-Identifier: BSL-1.0

#include <gst/gst.h>
#include <Pothos/Framework.hpp>
#include <Pothos/Exception.hpp>
#include <Poco/Logger.h>
#include <string>
#include "GStreamerTypes.hpp"

namespace GstTypes
{
    const std::array< std::pair< std::string, GstBufferFlags >, gst_buffer_flag_list_count > gst_buffer_flag_list
    { {
        { "LIVE"       , GST_BUFFER_FLAG_LIVE        },
        { "DECODE_ONLY", GST_BUFFER_FLAG_DECODE_ONLY },
        { "DISCONT"    , GST_BUFFER_FLAG_DISCONT     },
        { "RESYNC"     , GST_BUFFER_FLAG_RESYNC      },
        { "CORRUPTED"  , GST_BUFFER_FLAG_CORRUPTED   },
        { "MARKER"     , GST_BUFFER_FLAG_MARKER      },
        { "HEADER"     , GST_BUFFER_FLAG_HEADER      },
        { "GAP"        , GST_BUFFER_FLAG_GAP         },
        { "DROPPABLE"  , GST_BUFFER_FLAG_DROPPABLE   },
        { "DELTA_UNIT" , GST_BUFFER_FLAG_DELTA_UNIT  },
        { "TAG_MEMORY" , GST_BUFFER_FLAG_TAG_MEMORY  },
        { "SYNC_AFTER" , GST_BUFFER_FLAG_SYNC_AFTER  }
    } };

    // Packet meta data for GstBuffer
    const std::string PACKET_META_EOS        ( "eos" );
    const std::string PACKET_META_SEGMENT    ( "segment" );
    const std::string PACKET_META_CAPS       ( "caps" );

    const std::string PACKET_META_TIMESTAMP  ( "timestamp" );
    const std::string PACKET_META_PTS        ( "pts" );
    const std::string PACKET_META_DTS        ( "dts" );
    const std::string PACKET_META_FLAGS      ( "flags" );
    const std::string PACKET_META_DURATION   ( "duration" );
    const std::string PACKET_META_OFFSET     ( "offset" );
    const std::string PACKET_META_OFFSET_END ( "offset_end" );

    Poco::Logger & logger(void)
    {
        static auto &_logger = Poco::Logger::get("GStreamer");
        return _logger;
    }

    std::string boolToString(bool x)
    {
        return (x) ? "true" : "false";
    }

    std::string gquarkToString(GQuark quark)
    {
        auto quarkStr = g_quark_to_string( quark );
        assert( quarkStr != nullptr );
        return quarkStr;
    }

    std::string gerrorToString(const GError *gError)
    {
        if ( gError == nullptr )
        {
            return std::string();
        }
        if ( gError->message == nullptr )
        {
            return std::string();
        }

        return std::string( gError->message );
    }

    std::string gcharToString(const gchar *gstr)
    {
        return (gstr != nullptr) ? std::string( gstr ) : std::string();
    }

    Pothos::Object gcharToObject(const gchar *gstr)
    {
        if ( gstr == nullptr )
        {
            return Pothos::Object();
        }
        return Pothos::Object( gstr );
    }

//-----------------------------------------------------------------------------

    static gboolean gstStructureForeachFunc(GQuark field_id, const GValue *value, gpointer user_data)
    {
        auto args = static_cast< Pothos::ObjectKwargs* >( user_data );
        (*args)[ GstTypes::gquarkToString( field_id ) ] = objectFrom( value );
        return true;
    }

    Pothos::Object objectFrom(const GstStructure *gstStructure)
    {
        Pothos::ObjectKwargs fields;
        if ( gst_structure_foreach(gstStructure, gstStructureForeachFunc, &fields) )
        {
            Pothos::ObjectKwargs object;
            object[ gst_structure_get_name( gstStructure ) ] = Pothos::Object( fields );
            return Pothos::Object( object );
        }

        return Pothos::Object();
    }

    static void gDestroyNotifySharedVoid(gpointer data)
    {
        auto container = static_cast< std::shared_ptr< void >* >(data);
        delete container;
    }

    GstBuffer* makeSharedGStreamerBuffer(const void *data, size_t size, std::shared_ptr< void > container)
    {
        // Make copy of container
        auto userData = static_cast< gpointer >( new std::shared_ptr< void >( container ) );
        // Its ok to cast away the const as we create this buffer readonly
        return gst_buffer_new_wrapped_full(
            GST_MEMORY_FLAG_READONLY,        // GstMemoryFlags flags
            const_cast< gpointer >( data ),  // gpointer data
            size,                            // gsize maxsize
            0,                               // gsize offset
            size,                            // gsize size
            userData,                        // gpointer user_data
            &gDestroyNotifySharedVoid        // GDestroyNotify notify
        );
    }

    GstBuffer* makeGstBufferFromPacket(const Pothos::Packet& packet)
    {
        const std::string funcName( "GstTypes::createGstBufferFromPacket()" );
        //poco_information( GstTypes::logger(), funcName + " packet.payload.length = " + std::to_string( packet.payload.length ) );

        // Make a copy of the Pothos buffer for GStreamer buffer wrapping.
        // When GStreamer buffer is freed, it will free the copy of Pothos::BufferChunk.
        auto bufferChunk = std::make_shared< Pothos::BufferChunk >(packet.payload);

        auto gstBuffer = makeSharedGStreamerBuffer(bufferChunk->as< void* >(), bufferChunk->length, bufferChunk);

        if ( gstBuffer == nullptr )
        {
            // Could not allocate buffer.... bail
            poco_error( GstTypes::logger(), funcName + " Could not alloc buffer of size = " + std::to_string( packet.payload.length ) );
            return nullptr;
        }

        ifKeyExtract( packet.metadata, PACKET_META_TIMESTAMP , GST_BUFFER_TIMESTAMP ( gstBuffer ) );
        ifKeyExtract( packet.metadata, PACKET_META_PTS       , GST_BUFFER_PTS       ( gstBuffer ) );
        ifKeyExtract( packet.metadata, PACKET_META_DTS       , GST_BUFFER_DTS       ( gstBuffer ) );
        ifKeyExtract( packet.metadata, PACKET_META_DURATION  , GST_BUFFER_DURATION  ( gstBuffer ) );
        ifKeyExtract( packet.metadata, PACKET_META_OFFSET    , GST_BUFFER_OFFSET    ( gstBuffer ) );
        ifKeyExtract( packet.metadata, PACKET_META_OFFSET_END, GST_BUFFER_OFFSET_END( gstBuffer ) );

        // Try to convert buffer flags back into GStreamer buffer flags

        Pothos::ObjectKwargs flags_args;
        if ( GstTypes::ifKeyExtract( packet.metadata, PACKET_META_FLAGS, flags_args )  )
        {
            if ( GstTypes::debug_extra )
            {
                poco_information( GstTypes::logger(), funcName + " We got GST_BUFFER_META_FLAGS in the metadata: " + Pothos::Object( flags_args ).toString() );
            }

            for (const auto &flag : gst_buffer_flag_list)
            {
                const auto flag_bit_it = flags_args.find( flag.first );
                if ( flag_bit_it == flags_args.cend() )
                {
                    continue;
                }
                bool value = false;
                if ( GstTypes::ifKeyExtract( flags_args, flag.first , value ) )
                {
                    ( value ) ? GST_BUFFER_FLAG_SET(gstBuffer, flag.second) : GST_BUFFER_FLAG_UNSET(gstBuffer, flag.second);
                }
            }
        }

        return gstBuffer;
    }

    namespace
    {
        class GStreamerBufferWrapper
        {
            GstBuffer *m_buffer;
            GstMapInfo m_mapInfo;

        public:
            GStreamerBufferWrapper(const GStreamerBufferWrapper&) = delete;             // No copy constructor
            GStreamerBufferWrapper& operator= (const GStreamerBufferWrapper&) = delete; // No assignment operator

            GStreamerBufferWrapper(GstBuffer *gstBuffer) :
                m_buffer(gst_buffer_ref(gstBuffer)),
                m_mapInfo(GST_MAP_INFO_INIT)
            {
                if ( !gst_buffer_map(m_buffer, &m_mapInfo, GST_MAP_READ) )
                {
                    gst_buffer_unref(m_buffer);
                    // Blow up
                    throw Pothos::OutOfMemoryException("GStreamerBufferWrapper::GStreamerBufferWrapper()", "gst_buffer_map failed");
                }
            }

            ~GStreamerBufferWrapper(void)
            {
                gst_buffer_unmap(m_buffer, &m_mapInfo);
                gst_buffer_unref(m_buffer);
            }

            static Pothos::SharedBuffer makeSharedBuffer(GstBuffer *gstBuffer)
            {
                auto gstreamerBufferWrapper = std::make_shared< GStreamerBufferWrapper >(gstBuffer);

                return
                    Pothos::SharedBuffer(
                        reinterpret_cast< size_t >( gstreamerBufferWrapper->m_mapInfo.data ),
                        gstreamerBufferWrapper->m_mapInfo.size,
                        gstreamerBufferWrapper
                    );
            }

        };  // class GStreamerBufferWrapper
    }

    Pothos::Packet makePacketFromGstBuffer(GstBuffer *gstBuffer)
    {
        Pothos::Packet packet;

        packet.payload = Pothos::BufferChunk( GStreamerBufferWrapper::makeSharedBuffer( gstBuffer ) );

        if ( GST_BUFFER_TIMESTAMP_IS_VALID( gstBuffer ) )
        {
            const auto timeStamp = GST_BUFFER_TIMESTAMP( gstBuffer );
            packet.metadata[ PACKET_META_TIMESTAMP ] = Pothos::Object( timeStamp );
            packet.labels.push_back( Pothos::Label( PACKET_META_TIMESTAMP, timeStamp, 0) );
        }

        // The duration in nanoseconds
        if ( GST_BUFFER_DURATION_IS_VALID( gstBuffer ) )
        {
            packet.metadata[ PACKET_META_DURATION ] = Pothos::Object( GST_BUFFER_DURATION( gstBuffer ) );
        }

        // The presentation time stamp (pts) in nanoseconds
        if ( GST_BUFFER_PTS_IS_VALID( gstBuffer ) )
        {
            packet.metadata[ PACKET_META_PTS ] = Pothos::Object( GST_BUFFER_PTS( gstBuffer ) );
        }

        // The decoding time stamp (dts) in nanoseconds
        if ( GST_BUFFER_DTS_IS_VALID( gstBuffer ) )
        {
            packet.metadata[ PACKET_META_DTS ] = Pothos::Object( GST_BUFFER_DTS( gstBuffer ) );
        }

        // The offset in the source file of the beginning of this buffer.
        if ( GST_BUFFER_OFFSET_IS_VALID( gstBuffer ) )
        {
            const auto bufferOffset = GST_BUFFER_OFFSET( gstBuffer );
            packet.metadata[ PACKET_META_OFFSET ] = Pothos::Object( bufferOffset );
            packet.labels.push_back( Pothos::Label( PACKET_META_OFFSET, bufferOffset, 0) );
        }

        // The offset in the source file of the end of this buffer.
        if ( GST_BUFFER_OFFSET_END_IS_VALID( gstBuffer ) )
        {
            const auto bufferOffsetEnd = GST_BUFFER_OFFSET_END( gstBuffer );
            packet.metadata[ PACKET_META_OFFSET_END ] = Pothos::Object( bufferOffsetEnd );

            auto endPacketOffset = packet.payload.elements();
            if (endPacketOffset > 0)
            {
                --endPacketOffset;
            }
            packet.labels.push_back( Pothos::Label( PACKET_META_OFFSET_END, bufferOffsetEnd, endPacketOffset ) );
        }

        // Handle GST_BUFFER_FLAGS
        {
            Pothos::ObjectKwargs dict_flags;

            for (const auto &flag : gst_buffer_flag_list)
            {
                dict_flags[ flag.first ] = Pothos::Object( static_cast< bool >( GST_BUFFER_FLAG_IS_SET( gstBuffer, flag.second ) ) );
            }

            packet.metadata[ PACKET_META_FLAGS ] = Pothos::Object( dict_flags );
        }

        return packet;
    }

//-----------------------------------------------------------------------------

    Pothos::ObjectKwargs segmentToObjectKwargs(const GstSegment *segment)
    {
        Pothos::ObjectKwargs obj;

        obj[ "flags"        ] = Pothos::Object( (int)segment->flags );
        obj[ "rate"         ] = Pothos::Object( segment->rate );
        obj[ "applied_rate" ] = Pothos::Object( segment->applied_rate );
        obj[ "format"       ] = Pothos::Object( (int)segment->format );
        obj[ "base"         ] = Pothos::Object( segment->base );
        obj[ "offset"       ] = Pothos::Object( segment->offset );
        obj[ "start"        ] = Pothos::Object( segment->start );
        obj[ "stop"         ] = Pothos::Object( segment->stop );
        obj[ "time"         ] = Pothos::Object( segment->time );
        obj[ "position"     ] = Pothos::Object( segment->position );
        obj[ "duration"     ] = Pothos::Object( segment->duration );

        return obj;
    }

//-----------------------------------------------------------------------------

    static void convert_tag( const GstTagList *list, const gchar *tag, gpointer user_data );

    static Pothos::Object objectFromBoxed(GType type, gpointer boxedData)
    {
        if ( type == G_TYPE_GSTRING )
        {
            const auto gStr = static_cast< GString * >( boxedData );
            if ( gStr->str == nullptr )
            {
                return Pothos::Object();
            }
            return Pothos::Object( std::string( gStr->str, gStr->len ) );
        }

        if ( type == GST_TYPE_EVENT )
        {
            auto event = static_cast< GstEvent * >( boxedData );
            auto structure = gst_event_get_structure( event );
            return objectFrom( structure );
        }

        if ( type == GST_TYPE_TAG_LIST )
        {
            Pothos::ObjectKwargs objectMap;
            auto tagList = static_cast< GstTagList * >( boxedData );

            gst_tag_list_foreach( tagList, convert_tag, &objectMap );

            return Pothos::Object::make( objectMap );
        }

        if ( type == GST_TYPE_STRUCTURE )
        {
            auto structure = static_cast< const GstStructure* >( boxedData );
            return objectFrom( structure );
        }

        if ( type == G_TYPE_ERROR )
        {
            Pothos::ObjectKwargs args;
            auto gError = static_cast< const GError* >( boxedData );
            args[ "domain_id" ] = Pothos::Object( gError->domain );
            args[ "domain"    ] = Pothos::Object( GstTypes::gquarkToString( gError->domain ) );
            args[ "message"   ] = GstTypes::gcharToObject( gError->message );
            return Pothos::Object::make( /*std::move(*/ args /*)*/ );
        }

        if ( type == GST_TYPE_CAPS )
        {
            auto caps = static_cast< const GstCaps* >( boxedData );
            return GstTypes::gcharToObject( gst_caps_to_string( caps ) );
        }

        if ( type == GST_TYPE_DATE_TIME )
        {
            auto dateTime = static_cast< GstDateTime * >( boxedData );
            return gcharToObject( GCharPtr( gst_date_time_to_iso8601_string (dateTime ) ).get() );
        }

        if ( type == GST_TYPE_SAMPLE )
        {
            GstSample *gst_sample = static_cast< GstSample * >( boxedData );
            Pothos::ObjectKwargs sampleMap;
            sampleMap[ "caps"      ] = gcharToObject( gst_caps_to_string( gst_sample_get_caps( gst_sample ) ) );
            sampleMap[ "structure" ] = objectFrom( gst_sample_get_info( gst_sample ) );
            sampleMap[ "buffer"    ] = Pothos::Object::make( GstTypes::makePacketFromGstBuffer( gst_sample_get_buffer( gst_sample ) ) );

            // Handle sub struct of segment
            {
                auto segment = gst_sample_get_segment( gst_sample );
                sampleMap[ "segment" ] = Pothos::Object::make( segmentToObjectKwargs( segment ) );
            }

            return Pothos::Object::make( sampleMap );
        }

        if ( type == GST_TYPE_BUFFER )
        {
            auto gst_buffer = static_cast< GstBuffer* >( boxedData );

            auto packet = GstTypes::makePacketFromGstBuffer( gst_buffer );

            return Pothos::Object::make( packet );
        }

        if ( type == G_TYPE_DATE )
        {
            const auto date = static_cast< const GDate * >( boxedData );
            Pothos::ObjectKwargs dateMap;
            if ( date->dmy )
            {
                dateMap[ "year"  ] = Pothos::Object( date->year  );
                dateMap[ "month" ] = Pothos::Object( date->month );
                dateMap[ "day"   ] = Pothos::Object( date->day   );
            }

            if ( date->julian )
            {
                dateMap[ "julian_days" ] = Pothos::Object( date->julian_days );
            }
            return Pothos::Object::make( dateMap );
        }

        return Pothos::Object();
    }

    Pothos::Object objectFrom(const GValue *gvalue)
    {
        // If null, return null object
        if ( gvalue == nullptr )
        {
            return Pothos::Object();
        }

        const auto type = G_VALUE_TYPE( gvalue );

        switch ( G_TYPE_FUNDAMENTAL( type ) )
        {
            case G_TYPE_BOOLEAN:
                return Pothos::Object( static_cast< bool >( g_value_get_boolean( gvalue ) ) );
            case G_TYPE_INT64:
                return Pothos::Object( g_value_get_int64( gvalue ) );
            case G_TYPE_UINT64:
                return Pothos::Object( g_value_get_uint64( gvalue ) );
            case G_TYPE_INT:
                return Pothos::Object( g_value_get_int( gvalue ) );
            case G_TYPE_UINT:
                return Pothos::Object( g_value_get_uint( gvalue ) );
            case G_TYPE_FLOAT:
                return Pothos::Object( g_value_get_float( gvalue ) );
            case G_TYPE_DOUBLE:
                return Pothos::Object( g_value_get_double( gvalue ) );
            case G_TYPE_STRING:
                return gcharToObject( g_value_get_string( gvalue ) );
            case G_TYPE_CHAR:
                return Pothos::Object( g_value_get_schar( gvalue ) );
            case G_TYPE_UCHAR:
                return Pothos::Object( g_value_get_uchar( gvalue ) );

            case G_TYPE_ENUM:
            {
                Pothos::ObjectKwargs enumArgs;
                enumArgs[ "type" ] = Pothos::Object( "enum" );

                const auto enumValue = g_value_get_enum( gvalue );
                enumArgs[ "value"  ] = Pothos::Object( enumValue );
                enumArgs[ "string" ] = gcharToObject( GCharPtr( g_enum_to_string( type, enumValue) ).get() );

                return Pothos::Object::make( enumArgs );
            }

            case G_TYPE_FLAGS:
            {
                Pothos::ObjectKwargs flagsArgs;
                flagsArgs[ "type" ] = Pothos::Object( "flags" );

                const auto flags = g_value_get_flags( gvalue );
                flagsArgs[ "value"  ] = Pothos::Object( flags );
                flagsArgs[ "string" ] = gcharToObject( GCharPtr( g_flags_to_string(type, flags) ).get() );

                return Pothos::Object::make( flagsArgs );
            }

            case G_TYPE_BOXED:
            {
                const auto boxedData = g_value_get_boxed( gvalue );
                return objectFromBoxed(type, boxedData);
            }

            case G_TYPE_OBJECT:
            {
                auto object = g_value_get_object( gvalue );
                Pothos::ObjectKwargs objectArgs;
                objectArgs[ "type"      ] = Pothos::Object( "object" );
                objectArgs[ "className" ] = Pothos::Object( G_OBJECT_TYPE_NAME( object ) );
                objectArgs[ "object"    ] = Pothos::Object( reinterpret_cast< std::intptr_t >( object ) );
                return Pothos::Object::make( objectArgs );
            }
        }

        // If we made it here we failed to convert the GValue. Use glib to convert it to human readable string
        // log warning.
        const auto value_str( gcharToString( GCharPtr( g_strdup_value_contents( gvalue ) ).get() ) );
        const auto value_type_name = gcharToString( G_VALUE_TYPE_NAME( gvalue ) );
        poco_warning(
            GstTypes::logger(),
            "GstTypes::objectFrom() Could not convert GValue of type: " +
                value_type_name + " (abstract: " + boolToString( G_TYPE_IS_VALUE_ABSTRACT( type ) ) +
                    ", id: " + std::to_string( G_VALUE_TYPE( gvalue ) ) + "), value:" + value_str
        );

        Pothos::ObjectKwargs miscData;
        miscData[ "type"             ] = Pothos::Object( value_type_name );
        miscData[ "type_fundamental" ] = Pothos::Object( G_TYPE_FUNDAMENTAL( type ) );
        miscData[ "contents"         ] = Pothos::Object( value_str );

        return Pothos::Object::make( miscData );
    }

    static void convert_tag( const GstTagList *list, const gchar *tag, gpointer user_data )
    {
        auto tagMap = static_cast< Pothos::ObjectKwargs* >( user_data );

        const auto num = gst_tag_list_get_tag_size( list, tag );

        // If the tag is fixed add it as a unique object, otherwise add a vector of objects
        if ( gst_tag_is_fixed(tag) )
        {
            if (num > 1)
            {
                poco_warning(GstTypes::logger(), std::string("convert_tag: tag \"") + tag + "\" is fixed and size is > 1, this should never happen.");
            }

            const GValue *g_val = gst_tag_list_get_value_index( list, tag, 0 );

            (*tagMap)[ tag ] = objectFrom( g_val );
        }
        else
        {
            Pothos::ObjectVector objectVector;
            for ( std::remove_const< decltype(num) >::type i = 0; i < num; ++i )
            {
                const GValue *g_val = gst_tag_list_get_value_index( list, tag, i );

                // Added GStreamer value to Pothos vector
                objectVector.push_back( objectFrom( g_val ) );
            }
            (*tagMap)[ tag ] = Pothos::Object::make( objectVector );
        }
    }

    Pothos::ObjectKwargs objectFrom(const GstTagList *tags)
    {
        Pothos::ObjectKwargs objectMap;
        gst_tag_list_foreach( tags, convert_tag, &objectMap );
        return objectMap;
    }

}  // namespace GstTypes
