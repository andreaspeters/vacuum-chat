project(autostatus)

set(PLUGIN_NAME "autostatus")
set(PLUGIN_DISPLAY_NAME "Autostatus")
set(PLUGIN_DEPENDENCIES statuschanger accountmanager) # used only in CPack

include("autostatus.cmake")
include("${CMAKE_SOURCE_DIR}/src/plugins/plugins.cmake")

if (BUILD_TESTING)
	add_executable(autostatus_stream_guard_tests EXCLUDE_FROM_ALL
		autostatus_stream_guard_test.cpp)
	target_compile_features(autostatus_stream_guard_tests PRIVATE cxx_std_11)

	add_executable(autostatus_protocol_presence_tests EXCLUDE_FROM_ALL
		autostatus_protocol_presence_test.cpp)
	target_compile_features(autostatus_protocol_presence_tests PRIVATE cxx_std_11)
	target_link_libraries(autostatus_protocol_presence_tests PRIVATE
		${VACUUM_UTILS_NAME}
		Qt6::Core Qt6::Gui)
endif()
