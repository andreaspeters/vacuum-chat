project(ax25chat)

set(PLUGIN_NAME "ax25chat")
set(PLUGIN_DISPLAY_NAME "APRS Chat")
set(PLUGIN_DESCRIPTION "APRS messaging over AX.25 packet-radio transports")
set(PLUGIN_DEPENDENCIES accountmanager)

find_package(Qt6 REQUIRED COMPONENTS SerialPort Widgets)
set(ADD_LIBS Qt6::SerialPort)

set(SOURCES
    ax25chatplugin.cpp
    ax25flexpacketpipetransport.cpp
    ax25kisscodec.cpp
    ax25kissserialtransport.cpp
    ax25chatmessagerouting.cpp
    aprsmessagecodec.cpp
    aprsroomcodec.cpp
    aprsroomstore.cpp
    aprsmessageengine.cpp)
set(HEADERS
    ax25chatplugin.h
    ax25flexpacketpipetransport.h
    aprsmessagecodec.h
    aprsroomcodec.h
    aprsroomstore.h
    aprsmessageengine.h
    ax25transport.h
    ax25chat.json
    ax25kisscodec.h
    ax25kissserialtransport.h
    ax25chatmessagerouting.h)

set(USED_LANGS "")
include("${CMAKE_SOURCE_DIR}/src/plugins/plugins.cmake")

if (BUILD_TESTING)
    # Test executables
    add_executable(aprs_message_engine_tests EXCLUDE_FROM_ALL
        aprsmessageengine_test.cpp
        aprsmessageengine.cpp
        aprsroomcodec.cpp
        aprsmessagecodec.cpp)
    target_include_directories(aprs_message_engine_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_features(aprs_message_engine_tests PRIVATE cxx_std_11)
    target_link_libraries(aprs_message_engine_tests PRIVATE Qt6::Core)

    add_executable(aprs_message_codec_tests EXCLUDE_FROM_ALL
        aprsmessagecodec_test.cpp aprsmessagecodec.cpp)
    target_include_directories(aprs_message_codec_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_features(aprs_message_codec_tests PRIVATE cxx_std_11)
    target_link_libraries(aprs_message_codec_tests PRIVATE Qt6::Core)

    add_executable(axcp_codec_tests EXCLUDE_FROM_ALL
        axcpcodec_test.cpp axcpcodec.cpp)
    target_include_directories(axcp_codec_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_features(axcp_codec_tests PRIVATE cxx_std_11)
    target_link_libraries(axcp_codec_tests PRIVATE Qt6::Core)

    add_executable(axcp_reliable_engine_tests EXCLUDE_FROM_ALL
        axcp_reliable_engine_test.cpp
        axcp_reliable_engine.h
        axcp_reliable_engine.cpp
        axcpcodec.cpp)
    target_include_directories(axcp_reliable_engine_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_features(axcp_reliable_engine_tests PRIVATE cxx_std_11)
    target_link_libraries(axcp_reliable_engine_tests PRIVATE Qt6::Core)

    add_executable(ax25kiss_codec_tests EXCLUDE_FROM_ALL
        ax25kisscodec_test.cpp
        ax25kisscodec.h
        ax25kisscodec.cpp)
    target_include_directories(ax25kiss_codec_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_features(ax25kiss_codec_tests PRIVATE cxx_std_11)
    target_link_libraries(ax25kiss_codec_tests PRIVATE Qt6::Core)

    add_executable(ax25kiss_serial_transport_tests EXCLUDE_FROM_ALL
        ax25kissserialtransport_test.cpp
        ax25kissserialtransport.h
        ax25kissserialtransport.cpp
        aprsmessagecodec.cpp
        ax25kisscodec.cpp)
    target_include_directories(ax25kiss_serial_transport_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_features(ax25kiss_serial_transport_tests PRIVATE cxx_std_11)
    target_link_libraries(ax25kiss_serial_transport_tests PRIVATE Qt6::Core Qt6::SerialPort)

    add_executable(ax25_flexpacket_pipe_transport_tests EXCLUDE_FROM_ALL
        ax25flexpacketpipetransport_test.cpp
        ax25flexpacketpipetransport.h
        ax25flexpacketpipetransport.cpp
        aprsmessagecodec.h
        aprsmessagecodec.cpp
        ax25kisscodec.h
        ax25kisscodec.cpp)
    target_include_directories(ax25_flexpacket_pipe_transport_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_features(ax25_flexpacket_pipe_transport_tests PRIVATE cxx_std_11)
    target_link_libraries(ax25_flexpacket_pipe_transport_tests PRIVATE Qt6::Core)

    add_executable(ax25chat_plugin_tests EXCLUDE_FROM_ALL
        ax25chatplugin_test.cpp
        ax25chatplugin.cpp
        ax25chatplugin.h
        ax25chatmessagerouting.cpp
        ax25chatmessagerouting.h
        ax25flexpacketpipetransport.cpp
        ax25flexpacketpipetransport.h
        ax25kisscodec.cpp
        ax25kissserialtransport.cpp
        aprsmessageengine.cpp
        aprsmessagecodec.cpp
        aprsroomcodec.cpp
        aprsroomstore.cpp)
    target_include_directories(ax25chat_plugin_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_features(ax25chat_plugin_tests PRIVATE cxx_std_11)
    target_link_libraries(ax25chat_plugin_tests PRIVATE
        ${VACUUM_UTILS_NAME} Qt6::Core Qt6::Widgets Qt6::SerialPort Qt6::Xml)

    add_executable(ax25chat_message_routing_tests EXCLUDE_FROM_ALL
        ax25chatmessagerouting_test.cpp
        ax25chatmessagerouting.cpp)
    target_include_directories(ax25chat_message_routing_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/src")
    target_compile_features(ax25chat_message_routing_tests PRIVATE cxx_std_11)
    target_link_libraries(ax25chat_message_routing_tests PRIVATE Qt6::Core)

    add_executable(aprs_room_codec_tests EXCLUDE_FROM_ALL
        aprsroomcodec_test.cpp
        aprsroomcodec.cpp)

    add_executable(aprs_room_store_tests EXCLUDE_FROM_ALL
        aprsroomstore_test.cpp
        aprsroomstore.cpp
        aprsroomcodec.cpp)
    target_include_directories(aprs_room_store_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_features(aprs_room_store_tests PRIVATE cxx_std_11)
    target_link_libraries(aprs_room_store_tests PRIVATE Qt6::Core)
    target_include_directories(aprs_room_codec_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_features(aprs_room_codec_tests PRIVATE cxx_std_11)
    target_link_libraries(aprs_room_codec_tests PRIVATE Qt6::Core)
endif()
