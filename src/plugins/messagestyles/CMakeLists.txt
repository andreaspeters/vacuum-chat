project(messagestyles)

set(PLUGIN_NAME "messagestyles")
set(PLUGIN_DISPLAY_NAME "Message styles")
set(${PLUGIN_NAME}_IS_ESSENTIAL YES)

include("messagestyles.cmake")
include("${CMAKE_SOURCE_DIR}/src/plugins/plugins.cmake")

if (BUILD_TESTING)
	add_executable(messagestyles_current_local_time_tests EXCLUDE_FROM_ALL
		currentlocaltimecache_test.cpp
		currentlocaltimecache.h)
	target_compile_features(messagestyles_current_local_time_tests PRIVATE cxx_std_11)
	target_link_libraries(messagestyles_current_local_time_tests PRIVATE Qt6::Core)
endif()
