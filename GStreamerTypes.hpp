/// Copyright (c) 2017-2018 Ashley Brighthope
/// SPDX-License-Identifier: BSL-1.0

#pragma once

#include <gst/gst.h>
#include <Pothos/Framework.hpp>
#include <Pothos/Util/TypeInfo.hpp>
#include <Poco/Logger.h>
#include <string>
#include <array>

namespace GstTypes
{
    // GStreamer buffer flags
    constexpr int gst_buffer_flag_list_count = 12;
    extern const std::array< std::pair< std::string, GstBufferFlags >, gst_buffer_flag_list_count > gst_buffer_flag_list;

    // Packet meta data for GstBuffer
    extern const std::string PACKET_META_EOS;

    extern const std::string PACKET_META_SEGMENT;
    extern const std::string PACKET_META_INFO;
    extern const std::string PACKET_META_CAPS;

    extern const std::string PACKET_META_TIMESTAMP;
    extern const std::string PACKET_META_PTS;
    extern const std::string PACKET_META_DTS;
    extern const std::string PACKET_META_FLAGS;
    extern const std::string PACKET_META_DURATION;
    extern const std::string PACKET_META_OFFSET;
    extern const std::string PACKET_META_OFFSET_END;

    constexpr bool debug_extra = false;

    Poco::Logger &logger();

    std::string boolToString(bool x);

    template< typename T, void(*Fn)(T*) >
    struct Deleter
    {
        inline void operator() (void *p) const noexcept
        {
            Fn(static_cast<T*>(p));
        }
    };

    using GPointerType = std::remove_pointer< gpointer >::type;
    using GstObjectUnrefFunc = GstTypes::Deleter< GPointerType, gst_object_unref >;

    using GstIteratorPtr = std::unique_ptr < GstIterator, Deleter< GstIterator, gst_iterator_free > >;
    using GCharPtr = std::unique_ptr < gchar, Deleter< GPointerType, g_free > >;
    using GErrorPtr = std::unique_ptr < GError, Deleter< GError, g_error_free > >;
    using GstCapsPtr = std::unique_ptr< GstCaps, GstTypes::Deleter< GstCaps, gst_caps_unref > >;

    template< typename T >
    class UniquePtrRef {
        T *_ptr;
        typename T::element_type *_ref;
    public:
        using element_type = typename T::element_type;

        UniquePtrRef& operator=(const UniquePtrRef&) = delete;
        UniquePtrRef(const UniquePtrRef&) = delete;
        UniquePtrRef(UniquePtrRef&&) = default;
        UniquePtrRef& operator=(UniquePtrRef&&) = default;

        explicit UniquePtrRef(T &ptr) noexcept :
            _ptr( std::addressof( ptr ) ),
            _ref( nullptr )
        {
        }

        ~UniquePtrRef()
        {
            _ptr->reset( _ref );
        }

        operator element_type**() noexcept
        {
            return this->ref();
        }

        element_type** ref() noexcept
        {
            return &_ref;
        }
    };  // class UniquePtrRef< T >

    template< typename T>
    inline UniquePtrRef< T > uniquePtrRef(T &ptr) noexcept
    {
        return UniquePtrRef< T >( ptr );
    }

    std::string gquarkToString(GQuark quark);

    std::string gerrorToString(const GError *gError);

    std::string gcharToString(const gchar *gstr);

    Pothos::Object gcharToObject(const gchar *gstr);

    /**
     * @brief Convert GStreamer structure into Pothos::Object
     * @param gstStructure GstStructure to be converted to Pothos::ObjectKwargs.
     * @return Pothos::ObjectKwargs {"structure_name":{{"field1_name":"field1_value"},{"field2_name":"field2_value"}}
     */
    Pothos::ObjectKwargs structureToObjectKwargs(const GstStructure *gstStructure);

    GstBuffer* makeSharedGStreamerBuffer(const void *data, size_t size, std::shared_ptr< void > container);

    /**
     * @brief Create GstBuffer from Pothos::Packet, using shared memory
     * @param packet Pothos::Packet containing all data needed to make a GStreamer Buffer
     * @return GStreamer buffer filled out with data and meta data from packet
     */
    GstBuffer* makeGstBufferFromPacket(const Pothos::Packet& packet);

    /**
     * @brief Create Pothos::Packet from GstBuffer, using shared memory
     * @param gstBuffer GStreamer buffer to make packet from
     * @return Pothos::Packet with GStreamer buffer data and meta data
     */
    Pothos::Packet makePacketFromGstBuffer(GstBuffer *gstBuffer);

    Pothos::ObjectKwargs segmentToObjectKwargs(const GstSegment *segment);

    Pothos::ObjectKwargs gvalueToObjectKwargs(const GValue* value);

    Pothos::ObjectKwargs gstFormatToObjectKwargs(GstFormat format);

    Pothos::Object gvalueToObject(const GValue *gvalue);

    Pothos::ObjectKwargs tagListToObjectKwargs(const GstTagList *tags);

    struct GVal final
    {
        ::GValue value;

        GVal() noexcept : value( G_VALUE_INIT )
        {
        }

        explicit GVal(GType g_type) noexcept : value( G_VALUE_INIT )
        {
            g_value_init( &value, g_type );
        }

        GVal(const GVal&) = delete;
        GVal& operator=(const GVal&) = delete;
        GVal(GVal &&) = delete;
        GVal& operator=(GVal &&) = delete;

        ~GVal()
        {
            g_value_unset( &value );
        }

        inline void reset() noexcept
        {
            g_value_reset( &value );
        }

        Pothos::Object toPothosObject() const
        {
            return gvalueToObject( &value );
        }

        std::string toString() const
        {
            return gcharToString( GCharPtr( g_strdup_value_contents( &value ) ).get() );
        }

        Pothos::ObjectKwargs toDebugObjectKwargs() const
        {
            return gvalueToObjectKwargs( &value );
        }

        ::GValue* operator()() noexcept
        {
            return &value;
        }
    };  // struct GVal

    template< typename Iterator, class UnaryFunction >
    std::string joinStrings(const Iterator begin, const Iterator end, const std::string& separator, UnaryFunction func )
    {
        std::string str;

        auto it = begin;
        if (it != end)
        {
            str = func(*it);
            ++it;
        }
        for (; it != end; ++it)
        {
            str.append( separator );
            str.append( func(*it) );
        }
        return str;
    }

    template< typename T >
    bool ifKeyExtract(const Pothos::ObjectKwargs& map, const std::string& key, T& value)
    {
        const auto key_it = map.find( key );
        if ( key_it == map.cend() )
        {
            return false;
        }

        if ( key_it->second.type() != typeid(T) )
        {
            poco_error( GstTypes::logger(), "Can't convert the value of key \"" + key +
                        "\": (" + key_it->second.getTypeString() + ")\"" + key_it->second.toString() + "\" to " + Pothos::Util::typeInfoToString(typeid(T)) );
            return false;
        }

        if ( debug_extra )
        {
            poco_information( GstTypes::logger(), "Found key \"" + key + "\", value " + key_it->second.toString() + " (" + key_it->second.getTypeString() + ")");
        }
        value = key_it->second.extract< T >();
        return true;
    }

    template< typename T >
    T ifKeyExtractOrDefault(const Pothos::ObjectKwargs &map, const std::string &key, const T &fallBack)
    {
        const auto key_it = map.find( key );
        if ( key_it == map.cend() )
        {
            return fallBack;
        }

        if ( key_it->second.type() != typeid(T) )
        {
            return fallBack;
        }

        if ( debug_extra )
        {
            poco_information( GstTypes::logger(), "Found key \"" + key + "\", value " + key_it->second.toString() + " (" + key_it->second.getTypeString() + ")");
        }
        return key_it->second.extract< T >();
    }

}  // namespace GstTypes
