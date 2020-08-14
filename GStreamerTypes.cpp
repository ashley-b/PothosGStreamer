/// Copyright (c) 2017-2020 Ashley Brighthope
/// SPDX-License-Identifier: BSL-1.0

#include "GStreamerTypes.hpp"
#include <Poco/Logger.h>
#include <Pothos/Exception.hpp>
#include <Pothos/Framework.hpp>
#include <gst/gst.h>
#include <string>
#include <utility>

namespace GstTypes
{
    const std::array< std::pair< const char * const, GstBufferFlags >, GST_BUFFER_FLAG_LIST_SIZE > GST_BUFFER_FLAG_LIST
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
    const char PACKET_META_EOS       []{ "eos"        };
    const char PACKET_META_SEGMENT   []{ "segment"    };
    const char PACKET_META_INFO      []{ "info"       };
    const char PACKET_META_CAPS      []{ "caps"       };

    const char PACKET_META_PTS       []{ "pts"        };
    const char PACKET_META_DTS       []{ "dts"        };
    const char PACKET_META_FLAGS     []{ "flags"      };
    const char PACKET_META_DURATION  []{ "duration"   };
    const char PACKET_META_OFFSET    []{ "offset"     };
    const char PACKET_META_OFFSET_END[]{ "offset_end" };

    Poco::Logger & logger()
    {
        static auto &_logger = Poco::Logger::get("GStreamer");
        return _logger;
    }

    namespace detail
    {
        // Wraper around gst_buffer_unref as its static inline and will
        // cause linker error if we try and link directly to it.
        void gstBufferUnref(GstBuffer* gstBuffer) noexcept
        {
            gst_buffer_unref( gstBuffer );
        }

    }  // namespace detail

    std::string boolToString(bool x)
    {
        return (x) ? "true" : "false";
    }

    Poco::Optional< std::string > gquarkToString(GQuark quark)
    {
        const auto quarkStr = g_quark_to_string( quark );
        if ( quarkStr == nullptr )
        {
            return { };
        }
        return std::string( quarkStr );
    }

    std::string gerrorToString(const GError *gError)
    {
        if ( gError == nullptr )
        {
            return std::string("GError is null");
        }
        if ( gError->message == nullptr )
        {
            return std::string("GError->message is null");
        }

        return std::string( gError->message );
    }

    Poco::Optional< std::string > gcharToString(const gchar *gstr)
    {
        if ( gstr == nullptr )
        {
            return { };
        }
        return std::string( gstr );
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
    namespace
    {
        struct GstStructureForeach {
            Pothos::ObjectKwargs fields;
            std::exception_ptr exceptionPtr;

            static gboolean gstStructureForeachFunc(GQuark field_id, const GValue *value, gpointer user_data)
            {
                auto self = static_cast< GstStructureForeach* >( user_data );
                try
                {
                    (self->fields)[ gquarkToString( field_id ).value() ] = gvalueToObject( value );
                    return TRUE;
                }
                catch (...)  // Save any exception for when we leave gstreamer foreach
                {
                    self->exceptionPtr = std::current_exception();
                    return FALSE;
                }
            }
        };  // struct GstStructureForeach
    }  // namespace

    Pothos::ObjectKwargs gstStructureToObjectKwargs(const GstStructure *gstStructure)
    {
        if (gstStructure == nullptr)
        {
            return Pothos::ObjectKwargs();
        }
        GstStructureForeach gstStructureForeach;

        if ( gst_structure_foreach(gstStructure, GstStructureForeach::gstStructureForeachFunc, &gstStructureForeach) == FALSE )
        {
            if ( gstStructureForeach.exceptionPtr )
            {
                std::rethrow_exception( gstStructureForeach.exceptionPtr );
            }
            return Pothos::ObjectKwargs();
        }

        Pothos::ObjectKwargs objectArgs;
        objectArgs[ gst_structure_get_name( gstStructure ) ] = Pothos::Object( gstStructureForeach.fields );
        return objectArgs;
    }

    static void gDestroyNotifySharedVoid(gpointer data)
    {
        auto container = static_cast< std::shared_ptr< void >* >(data);
        delete container;
    }

    GstBufferPtr makeSharedGstBuffer(const void *data, size_t size, std::shared_ptr< void > container)
    {
        // Make copy of container
        auto userData = static_cast< gpointer >( new std::shared_ptr< void >( std::move( container ) ) );
        // Its ok to cast away the const as we create this buffer readonly
        return
            GstBufferPtr(
                gst_buffer_new_wrapped_full(
                    GST_MEMORY_FLAG_READONLY,        // GstMemoryFlags flags
                    const_cast< gpointer >( data ),  // gpointer data
                    size,                            // gsize maxsize
                    0,                               // gsize offset
                    size,                            // gsize size
                    userData,                        // gpointer user_data
                    &gDestroyNotifySharedVoid        // GDestroyNotify notify
                )
            );
    }

    GstBufferPtr makeGstBufferFromPacket(const Pothos::Packet& packet)
    {
        const std::string funcName( "GstTypes::makeGstBufferFromPacket()" );
        //poco_information( GstTypes::logger(), funcName + " packet.payload.length = " + std::to_string( packet.payload.length ) );

        // Make a copy of the Pothos buffer for GStreamer buffer wrapping.
        // When GStreamer buffer is freed, it will free the copy of Pothos::BufferChunk.
        auto bufferChunk = std::make_shared< Pothos::BufferChunk >(packet.payload);

        auto gstBuffer = makeSharedGstBuffer( bufferChunk->as< void* >(), bufferChunk->length, bufferChunk );

        if ( !gstBuffer )
        {
            // Could not allocate buffer.... bail
            poco_error( GstTypes::logger(), funcName + " Could not alloc buffer of size = " + std::to_string( packet.payload.length ) );
            return {};
        }

        auto * const gstBufferPtr = gstBuffer.get();
        ifKeyExtract( packet.metadata, PACKET_META_PTS       , GST_BUFFER_PTS       ( gstBufferPtr ) );
        ifKeyExtract( packet.metadata, PACKET_META_DTS       , GST_BUFFER_DTS       ( gstBufferPtr ) );
        ifKeyExtract( packet.metadata, PACKET_META_DURATION  , GST_BUFFER_DURATION  ( gstBufferPtr ) );
        ifKeyExtract( packet.metadata, PACKET_META_OFFSET    , GST_BUFFER_OFFSET    ( gstBufferPtr ) );
        ifKeyExtract( packet.metadata, PACKET_META_OFFSET_END, GST_BUFFER_OFFSET_END( gstBufferPtr ) );

        // Try to convert buffer flags back into GStreamer buffer flags
        const auto meta_flags_args_result = ifKeyExtract< Pothos::ObjectKwargs >( packet.metadata, PACKET_META_FLAGS );

        if ( meta_flags_args_result.isSpecified() )
        {
            const auto & flags_args = meta_flags_args_result.value();
            if ( GstTypes::debug_extra )
            {
                poco_information( GstTypes::logger(), funcName + " We got GST_BUFFER_META_FLAGS in the metadata: " + Pothos::Object( flags_args ).toString() );
            }

            for (const auto &flag : GST_BUFFER_FLAG_LIST)
            {
                const auto flag_bit_it = flags_args.find( flag.first );
                if ( flag_bit_it == flags_args.cend() )
                {
                    continue;
                }
                const auto result = ifKeyExtract< bool >( flags_args, flag.first );
                if ( result.isSpecified() )
                {
                    ( result.value() ) ? GST_BUFFER_FLAG_SET(gstBufferPtr, flag.second) : GST_BUFFER_FLAG_UNSET(gstBufferPtr, flag.second);
                }
            }
        }

        return gstBuffer;
    }

    namespace
    {
        class GstBufferMap final
        {
            GstBuffer *m_buffer;
            GstMapInfo m_mapInfo;

        public:
            GstBufferMap() = delete;
            GstBufferMap(const GstBufferMap&) = delete;             // No copy constructor
            GstBufferMap& operator= (const GstBufferMap&) = delete; // No assignment operator
            GstBufferMap(GstBufferMap&&) = delete;             // No move constructor
            GstBufferMap& operator= (GstBufferMap&&) = delete; // No move operator

            explicit GstBufferMap(GstBuffer *gstBuffer, GstMapFlags flags) :
                m_buffer(gst_buffer_ref(gstBuffer)),
                m_mapInfo(GST_MAP_INFO_INIT)
            {
                if ( gst_buffer_map(m_buffer, &m_mapInfo, flags) == FALSE )
                {
                    gst_buffer_unref(m_buffer);
                    // Blow up
                    throw Pothos::OutOfMemoryException("GstBufferMap::GstBufferMap()", "gst_buffer_map failed");
                }
            }

            ~GstBufferMap()
            {
                gst_buffer_unmap(m_buffer, &m_mapInfo);
                gst_buffer_unref(m_buffer);
            }

            static Pothos::SharedBuffer makeSharedReadBuffer(GstBuffer *gstBuffer)
            {
                auto gstBufferMap = std::make_shared< GstBufferMap >( gstBuffer, GST_MAP_READ );

                return
                    Pothos::SharedBuffer(
                        reinterpret_cast< size_t >( gstBufferMap->m_mapInfo.data ),
                        gstBufferMap->m_mapInfo.size,
                        gstBufferMap
                    );
            }

        };  // class GstBufferMap
    }  // namespace

//-----------------------------------------------------------------------------

    class GstCapsCache::Impl final
    {
        GstTypes::GstCapsPtr m_lastCaps;
        bool m_change{ false };
        std::string m_capsStr;

    public:
        Impl() = default;

        static bool equal(const GstCaps *caps1, const GstCaps *caps2) noexcept
        {
            if ( caps1 == caps2 )
            {
                return true;
            }
            if ( ( caps1 == nullptr ) || ( caps2 == nullptr ) )
            {
                return false;
            }
            return ( gst_caps_is_equal( caps1, caps2 ) == TRUE );
        }

        bool diff(GstCaps* caps)
        {
            if ( equal(caps, m_lastCaps.get()) )
            {
                m_change = false;
                return m_change;
            }

            if ( caps != nullptr )
            {
                m_lastCaps.reset( gst_caps_ref( caps ) );
                m_capsStr = GCharPtr( gst_caps_to_string( caps ) ).get();
            }
            else
            {
                m_capsStr.clear();
                m_lastCaps.reset();
            }
            m_change = true;
            return m_change;
        }

        const std::string& str() const noexcept
        {
            return m_capsStr;
        }

        bool change() const noexcept
        {
            return m_change;
        }

    };  // class GstCapsCache::Impl

    GstCapsCache::GstCapsCache() :
        m_impl( new GstCapsCache::Impl( ) )
    {
    }

    GstCapsCache::~GstCapsCache() = default;

    GstCapsCache::GstCapsCache(GstCapsCache &&) noexcept = default;
    GstCapsCache & GstCapsCache::operator= ( GstCapsCache && ) noexcept = default;

    bool GstCapsCache::diff(GstCaps* caps)
    {
        return m_impl->diff( caps );
    }

    const std::string& GstCapsCache::str() const noexcept
    {
        return m_impl->str();
    }

    bool GstCapsCache::change() noexcept
    {
        return m_impl->change();
    }

    std::string GstCapsCache::update(GstCaps* caps, GstCapsCache* gstCapsCache)
    {
        if ( gstCapsCache != nullptr )
        {
            auto impl = gstCapsCache->m_impl.get();
            impl->diff(caps);
            return impl->str();
        }

        return GCharPtr( gst_caps_to_string( caps ) ).get();
    }

//-----------------------------------------------------------------------------

    Pothos::Packet makePacketFromGstSample(GstSample *gstSample, GstCapsCache* gstCapsCach)
    {
        // Get GStreamer buffer and create Pothos packet from it
        auto packet = GstTypes::makePacketFromGstBuffer( gst_sample_get_buffer( gstSample ) );

        packet.metadata[ GstTypes::PACKET_META_INFO    ] =
            Pothos::Object( gstStructureToObjectKwargs( gst_sample_get_info( gstSample ) ) );

        packet.metadata[ GstTypes::PACKET_META_SEGMENT ] =
            Pothos::Object( gstSegmentToObjectKwargs( gst_sample_get_segment( gstSample ) ) );

        packet.metadata[ GstTypes::PACKET_META_CAPS    ] =
            Pothos::Object( GstCapsCache::update( gst_sample_get_caps( gstSample ), gstCapsCach) );

        return packet;
    }

    Pothos::Packet makePacketFromGstBuffer(GstBuffer *gstBuffer)
    {
        Pothos::Packet packet;

        packet.payload = Pothos::BufferChunk( GstBufferMap::makeSharedReadBuffer( gstBuffer ) );

        const auto payloadElements = packet.payload.elements();

        // The duration in nanoseconds
        if ( GST_BUFFER_DURATION_IS_VALID( gstBuffer ) )
        {
            packet.metadata[ PACKET_META_DURATION ] = Pothos::Object( GST_BUFFER_DURATION( gstBuffer ) );
        }

        // The presentation time stamp (pts) in nanoseconds
        if ( GST_BUFFER_PTS_IS_VALID( gstBuffer ) )
        {
            const auto pts = Pothos::Object( GST_BUFFER_PTS( gstBuffer ) );
            packet.metadata[ PACKET_META_PTS ] = pts;
            if (payloadElements > 0)
            {
              packet.labels.emplace_back(PACKET_META_PTS, pts, 0);
            }
        }

        // The decoding time stamp (dts) in nanoseconds
        if ( GST_BUFFER_DTS_IS_VALID( gstBuffer ) )
        {
            const auto dts = Pothos::Object( GST_BUFFER_DTS( gstBuffer ) );
            packet.metadata[ PACKET_META_DTS ] = dts;
            if (payloadElements > 0)
            {
              packet.labels.emplace_back(PACKET_META_PTS, dts, 0);
            }
        }

        // The offset in the source file of the beginning of this buffer.
        if ( GST_BUFFER_OFFSET_IS_VALID( gstBuffer ) )
        {
            const auto offset = Pothos::Object( GST_BUFFER_OFFSET( gstBuffer ) );
            packet.metadata[ PACKET_META_OFFSET ] = offset;
            if (payloadElements > 0)
            {
                packet.labels.emplace_back(PACKET_META_OFFSET, offset, 0);
            }
        }

        // The offset in the source file of the end of this buffer.
        if ( GST_BUFFER_OFFSET_END_IS_VALID( gstBuffer ) )
        {
            const auto offsetEnd = Pothos::Object( GST_BUFFER_OFFSET_END( gstBuffer ) );
            packet.metadata[ PACKET_META_OFFSET_END ] = offsetEnd;
            if (payloadElements > 0)
            {
                packet.labels.emplace_back(PACKET_META_OFFSET_END, offsetEnd, (payloadElements - 1) );
            }
        }

        // Handle GST_BUFFER_FLAGS
        {
            Pothos::ObjectKwargs dict_flags;

            for (const auto &flag : GST_BUFFER_FLAG_LIST)
            {
                dict_flags[ flag.first ] = Pothos::Object( static_cast< bool >( GST_BUFFER_FLAG_IS_SET( gstBuffer, flag.second ) ) );
            }

            packet.metadata[ PACKET_META_FLAGS ] = Pothos::Object( dict_flags );
        }

        return packet;
    }

//-----------------------------------------------------------------------------

    Pothos::ObjectKwargs gstSegmentToObjectKwargs(const GstSegment *segment)
    {
        Pothos::ObjectKwargs obj;

        obj[ "flags"        ] = Pothos::Object( enumToInteger( segment->flags ) );
        obj[ "rate"         ] = Pothos::Object( segment->rate );
        obj[ "applied_rate" ] = Pothos::Object( segment->applied_rate );
        obj[ "format"       ] = Pothos::Object( GstTypes::gstFormatToObjectKwargs( segment->format ) );
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

    Pothos::ObjectKwargs gvalueToObjectKwargs(const GValue* value)
    {
        Pothos::ObjectKwargs args;
        const auto type = G_VALUE_TYPE( value );
        args[ "type"                  ] = Pothos::Object( type );
        args[ "type_name"             ] = gcharToObject( g_type_name( type ) );

        const auto type_fundamental = G_TYPE_FUNDAMENTAL( type );
        args[ "type_fundamental"      ] = Pothos::Object( type_fundamental );
        args[ "type_fundamental_name" ] = gcharToObject( g_type_name( type_fundamental ) );

        args[ "abstract"              ] = Pothos::Object( boolToString( G_TYPE_IS_VALUE_ABSTRACT( type ) ) );
        args[ "contents"              ] = gcharToObject( GCharPtr( g_strdup_value_contents( value ) ).get() );
        return args;
    }

    Pothos::ObjectKwargs gstFormatToObjectKwargs(GstFormat format)
    {
        const auto gstFormatDetails = gst_format_get_details(format);

        Pothos::ObjectKwargs args;
        args[ "string"      ] = ( gstFormatDetails != nullptr ) ? gcharToObject( gstFormatDetails->nick ) : Pothos::Object();
        args[ "type"        ] = Pothos::Object( "enum" );
        args[ "value"       ] = Pothos::Object( enumToInteger( format ) );
        args[ "description" ] = ( gstFormatDetails != nullptr ) ? gcharToObject( gstFormatDetails->description ) : Pothos::Object();

        return Pothos::Object( args );
    }

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
            return Pothos::Object( gstStructureToObjectKwargs( structure ) );
        }

        if ( type == GST_TYPE_TAG_LIST )
        {
            auto tagList = static_cast< GstTagList * >( boxedData );
            return Pothos::Object( gstTagListToObjectKwargs( tagList ) );
        }

        if ( type == GST_TYPE_STRUCTURE )
        {
            auto structure = static_cast< const GstStructure* >( boxedData );
            return Pothos::Object( gstStructureToObjectKwargs( structure ) );
        }

        if ( type == G_TYPE_ERROR )
        {
            Pothos::ObjectKwargs args;
            auto gError = static_cast< const GError* >( boxedData );
            args[ "message"   ] = gcharToObject( gError->message );
            args[ "domain_id" ] = Pothos::Object( gError->domain );
            const auto domain = gquarkToString( gError->domain );
            args[ "domain"    ] = ( domain.isSpecified() ) ? Pothos::Object( domain.value() ) : Pothos::Object{};
            return Pothos::Object::make( args );
        }

        if ( type == GST_TYPE_CAPS )
        {
            auto caps = static_cast< const GstCaps* >( boxedData );
            return gcharToObject( GCharPtr( gst_caps_to_string( caps ) ).get() );
        }

        if ( type == GST_TYPE_DATE_TIME )
        {
            auto dateTime = static_cast< GstDateTime * >( boxedData );
            return gcharToObject( GCharPtr( gst_date_time_to_iso8601_string( dateTime ) ).get() );
        }

        if ( type == GST_TYPE_SAMPLE )
        {
            auto *gstSample = static_cast< GstSample * >( boxedData );
            return Pothos::Object( makePacketFromGstSample( gstSample, nullptr ) );
        }

        if ( type == GST_TYPE_BUFFER )
        {
            auto gstBuffer = static_cast< GstBuffer* >( boxedData );

            auto packet = makePacketFromGstBuffer( gstBuffer );

            return Pothos::Object::make( packet );
        }

        if ( type == G_TYPE_DATE )
        {
            const auto date = static_cast< const GDate * >( boxedData );
            Pothos::ObjectKwargs dateMap;

            {
                const auto year = g_date_get_year( date );
                if ( g_date_valid_year( year ) == TRUE )
                {
                    dateMap[ "year" ] = Pothos::Object( year );
                }
            }
            {
                const auto month = g_date_get_month( date );
                if ( g_date_valid_month( month ) == TRUE )
                {
                    dateMap[ "month" ] = Pothos::Object( month );
                }
            }
            {
                const auto day = g_date_get_day( date );
                if ( g_date_valid_day( day ) == TRUE )
                {
                    dateMap[ "day" ] = Pothos::Object( day );
                }
            }

            {
                const auto julian = g_date_get_julian( date );
                if ( g_date_valid_julian( julian ) == TRUE )
                {
                    dateMap[ "julian_days" ] = Pothos::Object( julian );
                }
            }
            return Pothos::Object::make( dateMap );
        }

        return Pothos::Object();
    }

    Pothos::Object gvalueToObject(const GValue *gvalue)
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

        // If we made it here we failed to convert the GValue.
        // Use glib to convert it to human readable string and log warning.
        auto miscData = Pothos::Object( gvalueToObjectKwargs( gvalue ) );
        poco_warning(
            GstTypes::logger(),
            "GstTypes::gvalueToObject() Could not convert GValue, falling back to Glib implementation: " + miscData.toString()
        );

        return miscData;
    }

    static void convert_tag( const GstTagList *list, const gchar *tag, gpointer user_data )
    {
        auto tagMap = static_cast< Pothos::ObjectKwargs* >( user_data );

        const auto num = gst_tag_list_get_tag_size( list, tag );

        // If the tag is fixed add it as a unique object, otherwise add a vector of objects
        if ( gst_tag_is_fixed(tag) == TRUE )
        {
            if (num > 1)
            {
                poco_warning(GstTypes::logger(), std::string("convert_tag: tag \"") + tag + "\" is fixed and size is > 1, this should never happen.");
            }

            const GValue *g_val = gst_tag_list_get_value_index( list, tag, 0 );

            (*tagMap)[ tag ] = gvalueToObject( g_val );
        }
        else
        {
            Pothos::ObjectVector objectVector;
            objectVector.reserve( num );
            for ( auto i = decltype(num){ 0 }; i < num; ++i )
            {
                const GValue *g_val = gst_tag_list_get_value_index( list, tag, i );

                // Added GStreamer value to Pothos vector
                objectVector.push_back( gvalueToObject( g_val ) );
            }
            (*tagMap)[ tag ] = Pothos::Object::make( objectVector );
        }
    }

    Pothos::ObjectKwargs gstTagListToObjectKwargs(const GstTagList *tags)
    {
        Pothos::ObjectKwargs objectMap;
        gst_tag_list_foreach( tags, convert_tag, &objectMap );
        return objectMap;
    }

}  // namespace GstTypes
