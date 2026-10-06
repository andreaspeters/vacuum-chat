project(privacylists)

set(PLUGIN_NAME "privacylists")
set(PLUGIN_DISPLAY_NAME "Privacy lists")
set(PLUGIN_DEPENDENCIES xmppstreams stanzaprocessor) # used only in CPack

include("privacylists.cmake")
include("${CMAKE_SOURCE_DIR}/src/plugins/plugins.cmake")

if (BUILD_TESTING)
	add_executable(privacylists_request_result_tests EXCLUDE_FROM_ALL
		privacylists_request_result_test.cpp)
	target_include_directories(privacylists_request_result_tests PRIVATE
		"${CMAKE_SOURCE_DIR}/src")
	target_compile_features(privacylists_request_result_tests PRIVATE cxx_std_11)
	target_link_libraries(privacylists_request_result_tests PRIVATE
		${VACUUM_UTILS_NAME} Qt6::Core Qt6::Xml)
	add_dependencies(privacylists_request_result_tests privacylists)
endif (BUILD_TESTING)
