project(messagearchiver)

set(PLUGIN_NAME "messagearchiver")
set(PLUGIN_DISPLAY_NAME "Message archiver")
set(PLUGIN_DEPENDENCIES xmppstreams stanzaprocessor) # used only in CPack

include("messagearchiver.cmake")
include("${CMAKE_SOURCE_DIR}/src/plugins/plugins.cmake")

set(MESSAGEARCHIVER_TEST_SOURCE_DIR "${CMAKE_SOURCE_DIR}/tests/messagearchiver")

if (BUILD_TESTING)
	add_executable(messagearchiver_history_viewmodel_tests EXCLUDE_FROM_ALL
		protocolhistoryviewmodel.cpp
		${MESSAGEARCHIVER_TEST_SOURCE_DIR}/protocolhistoryviewmodel_test.cpp)
	target_include_directories(messagearchiver_history_viewmodel_tests PRIVATE
		"${CMAKE_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}")
	target_compile_features(messagearchiver_history_viewmodel_tests PRIVATE cxx_std_11)
	target_link_libraries(messagearchiver_history_viewmodel_tests PRIVATE Qt6::Core)
	add_executable(messagearchiver_history_viewer_plugin_tests EXCLUDE_FROM_ALL
		${MESSAGEARCHIVER_TEST_SOURCE_DIR}/protocolhistoryviewer_plugin_test.cpp)
	target_include_directories(messagearchiver_history_viewer_plugin_tests PRIVATE
		"${CMAKE_SOURCE_DIR}/src")
	target_compile_features(messagearchiver_history_viewer_plugin_tests PRIVATE cxx_std_11)
	target_link_libraries(messagearchiver_history_viewer_plugin_tests PRIVATE Qt6::Core)
	add_dependencies(messagearchiver_history_viewer_plugin_tests messagearchiver)
	add_test(NAME messagearchiver_history_viewer_plugin_tests
		COMMAND messagearchiver_history_viewer_plugin_tests $<TARGET_FILE:messagearchiver>)
	add_executable(messagearchiver_registry_plugin_tests EXCLUDE_FROM_ALL
		${MESSAGEARCHIVER_TEST_SOURCE_DIR}/messagearchiver_registry_plugin_test.cpp)
	target_include_directories(messagearchiver_registry_plugin_tests PRIVATE
		"${CMAKE_SOURCE_DIR}/src")
	target_compile_features(messagearchiver_registry_plugin_tests PRIVATE cxx_std_11)
	target_link_libraries(messagearchiver_registry_plugin_tests PRIVATE Qt6::Core)
	add_dependencies(messagearchiver_registry_plugin_tests messagearchiver)
	add_test(NAME messagearchiver_registry_plugin_tests
		COMMAND messagearchiver_registry_plugin_tests $<TARGET_FILE:messagearchiver>)
endif()
