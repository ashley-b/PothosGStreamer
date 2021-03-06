/// Copyright (c) 2017-2019 Ashley Brighthope
/// SPDX-License-Identifier: BSL-1.0

#include "GStreamer.hpp"
#include "GStreamerTypes.hpp"
#include <Poco/TemporaryFile.h>
#include <Pothos/Framework.hpp>
#include <Pothos/Proxy.hpp>
#include <Pothos/Testing.hpp>
#include <complex>
#include <fstream>
#include <functional>
#include <iostream>
#include <json.hpp>
#include <tuple>

using json = nlohmann::json;

#define POTHOS_TEST_EQUAL_GCHAR(s1, s2)  POTHOS_TEST_EQUALA((s1), (s2), (std::min( strlen(s1), strlen(s2) )+1))

static const char testPath[]{ "/media/tests" };

template< typename T >
std::vector< T > createSequentialValues(typename std::vector< T >::size_type size, T init = {})
{
    std::vector< T > v( size );
    std::iota(v.begin(), v.end(), init);
    return v;
}

POTHOS_TEST_BLOCK(testPath, test_gstreamer_types_gvalue_to_object)
{
    {
        const std::string testString( "test 1, 2" );
        {
            GstTypes::GVal value(G_TYPE_STRING);
            g_value_set_static_string(value(), testString.c_str() );

            auto obj = GstTypes::gvalueToObject(value());
            POTHOS_TEST_EQUAL(obj.type().hash_code(), typeid(testString).hash_code());
            POTHOS_TEST_EQUAL(obj.extract< std::string >(), testString);
        }

        {
            GstTypes::GVal value(G_TYPE_GSTRING);
            g_value_take_boxed(value(), g_string_new(testString.c_str()) );

            auto obj = GstTypes::gvalueToObject(value());
            POTHOS_TEST_EQUAL(obj.type().hash_code(), typeid(testString).hash_code());
            POTHOS_TEST_EQUAL(obj.extract< std::string >(), testString);
        }

        {
            auto obj = GstTypes::gcharToObject( testString.c_str() );
            POTHOS_TEST_EQUAL(obj.type().hash_code(), typeid(testString).hash_code());
            POTHOS_TEST_EQUAL(obj.extract< std::string >(), testString);
        }
    }

    // GValue enum test
    {
        GstTypes::GVal gvalue(GST_TYPE_STATE);

        constexpr gint testValue = GST_STATE_PLAYING;
        g_value_set_enum(gvalue(), testValue);

        auto obj = gvalue.toPothosObject();
        std::cerr <<  obj.toString() << std::endl;

        const auto args = obj.convert< Pothos::ObjectKwargs >();

        {
            const auto typeIt = args.find( "type" );

            POTHOS_TEST_TRUE( typeIt != args.cend() );

            const auto type = typeIt->second.convert< std::string >();
            POTHOS_TEST_EQUAL( type, "enum" );
        }

        {
            const auto valueIt = args.find( "value" );

            POTHOS_TEST_TRUE( valueIt != args.cend() );

            const auto value = valueIt->second.convert< int >();
            POTHOS_TEST_EQUAL( value, testValue );
        }

        {
            const auto stringIt = args.find( "string" );

            POTHOS_TEST_TRUE( stringIt != args.cend() );

            const auto string = stringIt->second.convert< std::string >();
            POTHOS_TEST_EQUAL( string, "GST_STATE_PLAYING" );
        }
    }

    // GValue flag test
    {
        GstTypes::GVal gvalue( GST_TYPE_ELEMENT_FLAGS );
        constexpr guint testValue = 0xffff;
        g_value_set_flags(gvalue(), testValue);

        auto obj = gvalue.toPothosObject();
        std::cerr <<  obj.toString() << std::endl;

        const auto args = obj.convert< Pothos::ObjectKwargs >();

        {
            const auto typeIt = args.find( "type" );

            POTHOS_TEST_TRUE( typeIt != args.cend() );

            const auto type = typeIt->second.convert< std::string >();
            POTHOS_TEST_EQUAL( type, "flags" );
        }

        {
            const auto valueIt = args.find( "value" );

            POTHOS_TEST_TRUE( valueIt != args.cend() );

            const auto value = valueIt->second.convert< int >();
            POTHOS_TEST_EQUAL( value, testValue );
        }

        {
            const auto stringIt = args.find( "string" );

            POTHOS_TEST_TRUE( stringIt != args.cend() );

            // Should test the string values of the flag, but too tricky to be worth it.
            //const auto value = stringIt->second.convert< std::string >();
            //POTHOS_TEST_EQUAL( value, "GST_STATE_PLAYING" );
        }

        {
            const auto testData = createSequentialValues< int8_t >( 1024 );
            GstTypes::GVal v(G_TYPE_BYTES);
            g_value_take_boxed( &v.value, g_bytes_new_static( testData.data(), testData.size() ) );

            POTHOS_TEST_EQUAL( v.type(), G_TYPE_BYTES );
            poco_information( GstTypes::logger(), "v.toDebugString() = " + Pothos::Object( v.toDebugObjectKwargs() ).toString() );

            auto box = reinterpret_cast< GBytes* >( g_value_get_boxed( &v.value ) );
            gsize boxSize;
            const auto boxData = reinterpret_cast< const char* >( g_bytes_get_data( box, &boxSize ) );

            POTHOS_TEST_EQUAL( boxSize, testData.size());

            POTHOS_TEST_EQUALA(testData, boxData, boxSize );
        }
    }
}

POTHOS_TEST_BLOCK(testPath, test_gstreamer_types_if_key_extract_or_default)
{
    Pothos::ObjectKwargs args;

    const std::string missingKey( "missing_key" );

    constexpr int value1Default = 12;
    constexpr int value1 = 42;
    const std::string key1( "key1");
    args[ key1 ] = Pothos::Object( value1 );

    const std::string value2Default( "DefaultValue" );
    const std::string value2( "Value");
    const std::string key2 ("key2");
    args[ key2 ] = Pothos::Object( value2 );

    // Found key1 test
    {
        const auto value = GstTypes::ifKeyExtract< int >(args, key1).value( value1Default );
        POTHOS_TEST_EQUAL(value, value1);
    }
    // Missing key1 test
    {
        const auto value = GstTypes::ifKeyExtract< int >(args, missingKey).value( value1Default );
        POTHOS_TEST_EQUAL(value, value1Default);
    }
    // Convert int to string test
    {
        const auto value = GstTypes::ifKeyExtract< std::string >(args, key1).value( value2Default );
        POTHOS_TEST_EQUAL(value, value2Default);
    }

    // Found key2 test
    {
        const auto value = GstTypes::ifKeyExtract< std::string >(args, key2).value( value2Default );
        POTHOS_TEST_EQUAL(value, value2);
    }
    // Missing key2 test
    {
        const auto value = GstTypes::ifKeyExtract< std::string >(args, missingKey).value( value2Default );
        POTHOS_TEST_EQUAL(value, value2Default);
    }
}

POTHOS_TEST_BLOCK(testPath, test_gstreamer_types_gchar_ptr)
{
    //(void)g_strdup( "leak" );
    constexpr size_t stringLength = 255;
    std::array< gchar, stringLength > srcString;

    // Create sequentially increasing test string
    std::iota(srcString.begin(), srcString.end(), 1);
    // Zero terminate
    srcString.back() = 0;

    GstTypes::GCharPtr gcharPtr( g_strdup( srcString.data() ) );

    POTHOS_TEST_EQUAL_GCHAR( gcharPtr.get(), srcString.data() );
}

static void outputArgumentGChar(const gchar *str, gchar **outStr)
{
    *outStr = g_strdup( str );
}

POTHOS_TEST_BLOCK(testPath, test_gstreamer_types_unique_out_arg)
{
    GstTypes::GCharPtr gcharPtr;
    static constexpr char testString[]{ "Caution!! test string" };

    outputArgumentGChar( testString, GstTypes::uniqueOutArg( gcharPtr ) );

    POTHOS_TEST_EQUAL_GCHAR( testString, gcharPtr.get() );
}

static GError* newTestGError()
{
    return g_error_new_literal(gst_core_error_quark(), GST_CORE_ERROR_TOO_LAZY, "Test Error");
}

static void outputArgumentError(GError **outError)
{
    *outError = newTestGError();
}

POTHOS_TEST_BLOCK(testPath, test_gstreamer_types_gerror_ptr)
{
    GstTypes::GErrorPtr refError( newTestGError() );
    GstTypes::GErrorPtr gerrorPtr;
    outputArgumentError( GstTypes::uniqueOutArg( gerrorPtr ) );

    POTHOS_TEST_EQUAL(refError->code, gerrorPtr->code);
    POTHOS_TEST_EQUAL(refError->domain, gerrorPtr->domain);
    POTHOS_TEST_EQUAL_GCHAR( refError->message, gerrorPtr->message );
}

POTHOS_TEST_BLOCK(testPath, test_gstreamer_source)
{
    auto vector_source = Pothos::BlockRegistry::make( "/blocks/vector_source", "int8" );
    vector_source.call( "setMode", "ONCE" );

    // Allocate some fake data
    const auto testData = createSequentialValues< int8_t >( 2048 );

    // Convert our real values to complex values for setElements method
    {
        std::vector< std::complex< double > > dataComplex( testData.size() ) ;
        std::copy(testData.cbegin(), testData.cend(), dataComplex.begin() );

        vector_source.call( "setElements", dataComplex );
    }

    auto stream_to_packet = Pothos::BlockRegistry::make( "/blocks/stream_to_packet" );
    stream_to_packet.call( "setMTU", testData.size() );

    auto tempFile = Poco::TemporaryFile();

    auto gstreamer = Pothos::BlockRegistry::make( "/media/gstreamer", "appsrc name=sink1 ! filesink location=" + tempFile.path() );

    // Run the topology
    std::cout << "Run the topology\n";
    {
        Pothos::Topology topology;
        topology.connect( vector_source, 0 , stream_to_packet, 0 );
        topology.connect( stream_to_packet, 0 , gstreamer, "sink1" );
        topology.commit();
        topology.waitInactive( 1 );
        //POTHOS_TEST_TRUE(topology.waitInactive());
    }

    const auto fileSize = tempFile.getSize();

    // Check output file size is the same as the input test data
    POTHOS_TEST_EQUAL( fileSize , testData.size() );

    std::fstream fs;
    fs.open( tempFile.path(), std::fstream::in | std::fstream::binary );
    POTHOS_TEST_TRUE( (fs) );  // Make sure file can be opend. Should be written by GStreamer during above test

    // Reading output file, from GStreamer
    std::vector< int8_t > fileBuffer( fileSize );

    fs.read( reinterpret_cast< decltype( fs )::char_type* >( fileBuffer.data() ), fileBuffer.size() );
    POTHOS_TEST_TRUE( (fs) );
    fs.close();

    // Now test all bytes in match all bytes out
    POTHOS_TEST_EQUALV( fileBuffer, testData );
}

POTHOS_TEST_BLOCK(testPath, test_gstreamer_sink)
{
    constexpr int packetSize = 1024;
    constexpr int sentPacketCount = 2;
    const std::string testPipe = "fakesrc sizetype=fixed filltype=pattern sizemax=" + std::to_string( packetSize ) + " num-buffers=" + std::to_string( sentPacketCount ) + " ! appsink name=src1";

    auto gstreamer = Pothos::BlockRegistry::make( "/media/gstreamer", testPipe );
    auto collector_sink = Pothos::BlockRegistry::make( "/blocks/collector_sink", "int8" );

    // Run the topology
    std::cout << "Run the topology\n";
    {
        Pothos::Topology topology;
        topology.connect( gstreamer, "src1" , collector_sink, 0 );
        topology.commit();
        topology.waitInactive( 1 );
        //POTHOS_TEST_TRUE(topology.waitInactive(1, 10));
    }

    // Create fake data that matches what GStreamer fakesrc will create
    const auto testData = createSequentialValues< int8_t >( packetSize );

    const auto packets = collector_sink.call< std::vector< Pothos::Packet > >( "getPackets" );
    std::cout << "packets.size() = " << packets.size() << std::endl;

    // Plus one for packet eos
    POTHOS_TEST_EQUAL(packets.size(), sentPacketCount + 1 );

    int packetCount = 0;
    for ( const auto &packet : packets )
    {
        std::cout << "Packet No. " << packetCount << std::endl;
        std::cout << "\tpacket.payload.length = " << packet.payload.length << std::endl;
        std::cout << "\tpacket.metadata = " << Pothos::Object( packet.metadata ).toString() << std::endl;
        auto eos_it = packet.metadata.find("eos");
        POTHOS_TEST_TRUE( eos_it != packet.metadata.end() );

        POTHOS_TEST_TRUE( eos_it->second.type() == typeid ( bool ) );

        // Only check payload if not signaling eos
        if ( eos_it->second.extract< bool >() == false )
        {
            POTHOS_TEST_EQUAL(packet.payload.length, testData.size());
            POTHOS_TEST_EQUALA(testData , packet.payload.as< const int8_t* >(), packet.payload.length );
        }
        ++packetCount;
    }
}

// TODO: Write test_gstreamer_tag_source

POTHOS_TEST_BLOCK(testPath, test_gstreamer_tag_sink)
{
    constexpr int sentPacketCount = 2;
    const std::string testPipe =
        "fakesrc sizetype=fixed sizetype=empty num-buffers=" + std::to_string( sentPacketCount ) +
        " ! taginject tags=\"keywords={\\\"Fake\\\",\\\"Raw\\\"},title=\\\"Fake Data\\\"\" ! appsink name=src1";
        //" ! taginject tags='title=\"test\"' ! appsink name=src1";


    auto gstreamer = Pothos::BlockRegistry::make( "/media/gstreamer", testPipe );
    auto collector_sink = Pothos::BlockRegistry::make( "/blocks/collector_sink", "int8" );

    auto slot_to_message = Pothos::BlockRegistry::make( "/blocks/slot_to_message", SIGNAL_TAG );
    auto collector_tag_sink = Pothos::BlockRegistry::make( "/blocks/collector_sink", "int8" );

    // Run the topology
    std::cout << "Run the topology\n";
    {
        Pothos::Topology topology;
        topology.connect( gstreamer, "src1" , collector_sink, 0 );

        topology.connect( gstreamer, SIGNAL_TAG , slot_to_message, SIGNAL_TAG );
        topology.connect( slot_to_message, 0 , collector_tag_sink, 0 );

        topology.commit();
        topology.waitInactive( 1 );
        //POTHOS_TEST_TRUE(topology.waitInactive(1, 10));
    }

    const auto messages = collector_tag_sink.call< std::vector< Pothos::Object > >( "getMessages" );

    for (const auto &message : messages)
    {
        std::cout << "\t" << message.toString() << std::endl;
    }

    POTHOS_TEST_EQUAL( messages.size() , 1 );

    // Ugly code, but this checks tags make it from taginject all the way out to the appsink
    {
        auto args =  messages[0].extract< Pothos::ObjectKwargs >();

        auto bodyIt = args.find("body");
        POTHOS_TEST_TRUE( bodyIt != args.end() );

        auto body = bodyIt->second.extract< Pothos::ObjectKwargs >();
        {
            auto keywordsIt = body.find("keywords");
            POTHOS_TEST_TRUE( keywordsIt != body.end() );

            auto keywords = keywordsIt->second.extract< Pothos::ObjectVector >();
            POTHOS_TEST_EQUAL( keywords.size() , 2 );

            bool foundFake = false;
            bool foundRaw = false;
            for (const auto & keyword : keywords )
            {
                const auto &keywordStr = keyword.extract< std::string >();
                if ( keywordStr == "Fake")
                {
                    foundFake = true;
                }
                else
                if ( keywordStr == "Raw")
                {
                    foundRaw = true;
                }
            }
            POTHOS_TEST_TRUE( foundFake );
            POTHOS_TEST_TRUE( foundRaw );
        }
        {
            auto titlesIt = body.find("title");
            POTHOS_TEST_TRUE( titlesIt != body.end() );

            auto titles = titlesIt->second.extract< Pothos::ObjectVector >();
            POTHOS_TEST_EQUAL( titles.size() , 1 );
            POTHOS_TEST_EQUAL( titles[0].extract< std::string >() , "Fake Data" );
        }
    }

    const auto packets = collector_sink.call< std::vector< Pothos::Packet > >( "getPackets" );
    // Plus one for packet eos
    POTHOS_TEST_EQUAL(packets.size(), sentPacketCount + 1 );

    int packetCount = 0;
    for ( const auto &packet : packets )
    {
        std::cout << "Packet No. " << packetCount << std::endl;
        std::cout << "\tpacket.payload.length = " << packet.payload.length << std::endl;
        std::cout << "\tpacket.metadata = " << Pothos::Object( packet.metadata ).toString() << std::endl;

        POTHOS_TEST_EQUAL( packet.payload.length , 0 );

        auto eos_it = packet.metadata.find("eos");
        POTHOS_TEST_TRUE( eos_it != packet.metadata.end() );
        POTHOS_TEST_TRUE( eos_it->second.type() == typeid ( bool ) );

        ++packetCount;
    }
}

POTHOS_TEST_BLOCK(testPath, test_gstreamer_passthrough)
{
    const char passthrough_pipeline[]{ "appsrc name=in ! appsink name=out" };

    auto feederSource = Pothos::BlockRegistry::make( "/blocks/feeder_source", "int8" );

    auto gstreamer = Pothos::BlockRegistry::make( "/media/gstreamer", passthrough_pipeline );

    auto reinterpret = Pothos::BlockRegistry::make( "/blocks/reinterpret", "int8" );

    auto collectorSink = Pothos::BlockRegistry::make( "/blocks/collector_sink", "int8" );

    json testPlan;
    testPlan[ "enablePackets" ] = true;

    auto expected = feederSource.call("feedTestPlan", testPlan.dump());

    // Run the topology
    std::cout << "Run the topology\n";
    {
        Pothos::Topology topology;

        topology.connect( feederSource, 0 , gstreamer, "in" );
        topology.connect( gstreamer, "out" , reinterpret, 0 );
        topology.connect( reinterpret, 0 , collectorSink, 0 );

        topology.commit();
        topology.waitInactive( 1 );
        //POTHOS_TEST_TRUE(topology.waitInactive(1, 10));
    }

    collectorSink.call("verifyTestPlan", expected);
}

POTHOS_TEST_BLOCK(testPath, test_gstreamer_create_destroy)
{
    POTHOS_TEST_CHECKPOINT();

    (void)Pothos::BlockRegistry::make( "/media/gstreamer", "fakesrc ! fakesink" );

    POTHOS_TEST_CHECKPOINT();

    (void)Pothos::BlockRegistry::make( "/media/gstreamer", "fakesrc ! fakesink" );

    POTHOS_TEST_CHECKPOINT();
}

POTHOS_TEST_BLOCK(testPath, test_gstreamer_stuff)
{
    std::vector< std::tuple< std::string, Pothos::DType > > dtypeList;
    dtypeList.emplace_back( "Pothos::DType(, 10)", Pothos::DType("", 10) );
    dtypeList.emplace_back( "Pothos::DType(\"custom\", 10)", Pothos::DType("custom", 10) );
    dtypeList.emplace_back( "Pothos::DType(\"int\", 1)", Pothos::DType("int", 1) );
    dtypeList.emplace_back( "Pothos::DType(\"int\", 2)", Pothos::DType("int", 2) );

    for (const auto &dtype : dtypeList)
    {
        poco_information( GstTypes::logger(), std::get<0>(dtype) );
        poco_information( GstTypes::logger(), "    .isCustom(): "   + GstTypes::boolToString( std::get<1>(dtype).isCustom() ) );
        poco_information( GstTypes::logger(), "    .size(): "       + std::to_string( std::get<1>(dtype).size() ) );
        poco_information( GstTypes::logger(), "    .dimension(): "  + std::to_string( std::get<1>(dtype).dimension() ) );
    }

    GstCaps *caps = nullptr;
    const std::string s( GstTypes::GCharPtr( gst_caps_to_string( caps ) ).get() );

    poco_information( GstTypes::logger(), "caps = " + s );


    GstTypes::GstCapsCache gstCapsCache;

    auto g = std::move( gstCapsCache );
}
