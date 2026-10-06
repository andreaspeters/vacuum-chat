project(statuschanger)

set(PLUGIN_NAME "statuschanger")
set(PLUGIN_DISPLAY_NAME "Status changer")
set(PLUGIN_DEPENDENCIES presence) # used only in CPack
set(${PLUGIN_NAME}_IS_ESSENTIAL YES)

include("statuschanger.cmake")
include("${CMAKE_SOURCE_DIR}/src/plugins/plugins.cmake")

if (BUILD_TESTING)
	add_executable(statuschanger_protocol_presence_routing_tests EXCLUDE_FROM_ALL
		protocolpresencerouting_test.cpp)
	target_include_directories(statuschanger_protocol_presence_routing_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
	target_compile_features(statuschanger_protocol_presence_routing_tests PRIVATE cxx_std_11)
	target_link_libraries(statuschanger_protocol_presence_routing_tests PRIVATE Qt6::Core)
endif()
