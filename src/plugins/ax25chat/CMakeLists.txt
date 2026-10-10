project(ax25chat)

set(PLUGIN_NAME "ax25chat")
set(PLUGIN_DISPLAY_NAME "AX.25 Chat")
set(PLUGIN_DESCRIPTION "AX.25 packet-radio chat support")
set(PLUGIN_DEPENDENCIES accountmanager)

find_package(Qt6 REQUIRED COMPONENTS SerialPort Widgets)
set(ADD_LIBS Qt6::SerialPort)

set(SOURCES
    ax25chatplugin.cpp
    ax25kisscodec.cpp
    ax25kissserialtransport.cpp
    axcpcodec.cpp
    axcp_reliable_engine.cpp)
set(HEADERS
    ax25chatplugin.h
    ax25chat.json
    ax25kisscodec.h
    ax25kissserialtransport.h
    axcpcodec.h
    axcp_reliable_engine.h)

set(USED_LANGS "")
include("${CMAKE_SOURCE_DIR}/src/plugins/plugins.cmake")

if (BUILD_TESTING)
    # Test executables
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
        ax25kisscodec.cpp)
    target_include_directories(ax25kiss_serial_transport_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_features(ax25kiss_serial_transport_tests PRIVATE cxx_std_11)
    target_link_libraries(ax25kiss_serial_transport_tests PRIVATE Qt6::Core Qt6::SerialPort)

    add_executable(ax25chat_plugin_tests EXCLUDE_FROM_ALL
        ax25chatplugin_test.cpp
        ax25chatplugin.cpp
        ax25chatplugin.h
        ax25kisscodec.cpp
        ax25kissserialtransport.cpp
        axcpcodec.cpp
        axcp_reliable_engine.cpp)
    target_include_directories(ax25chat_plugin_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_features(ax25chat_plugin_tests PRIVATE cxx_std_11)
    target_link_libraries(ax25chat_plugin_tests PRIVATE
        ${VACUUM_UTILS_NAME} Qt6::Core Qt6::Widgets Qt6::SerialPort Qt6::Xml)
endif()
