project(chatstates)

set(PLUGIN_NAME "chatstates")
set(PLUGIN_DISPLAY_NAME "Chat states")
set(PLUGIN_DEPENDENCIES presence messagewidgets stanzaprocessor) # used only in CPack

include("chatstates.cmake")
include("${CMAKE_SOURCE_DIR}/src/plugins/plugins.cmake")

if (BUILD_TESTING)
	add_executable(chatstates_protocol_typing_routing_tests EXCLUDE_FROM_ALL
		protocolchatstaterouting_test.cpp)
	target_include_directories(chatstates_protocol_typing_routing_tests PRIVATE
		"${CMAKE_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}")
	target_compile_features(chatstates_protocol_typing_routing_tests PRIVATE cxx_std_11)
	target_link_libraries(chatstates_protocol_typing_routing_tests PRIVATE Qt6::Core)
endif()
