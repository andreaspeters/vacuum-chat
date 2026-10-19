project(messagewidgets)

set(PLUGIN_NAME "messagewidgets")
set(PLUGIN_DISPLAY_NAME "Message widgets")
set(${PLUGIN_NAME}_IS_ESSENTIAL YES)

include("messagewidgets.cmake")
include("${CMAKE_SOURCE_DIR}/src/plugins/plugins.cmake")

if(BUILD_TESTING)
	set(MESSAGEWIDGETS_TEST_SOURCE_DIR ${CMAKE_SOURCE_DIR}/tests/messagewidgets)

	add_executable(messagewidgets_view_url_fallback_policy_tests EXCLUDE_FROM_ALL
		${MESSAGEWIDGETS_TEST_SOURCE_DIR}/viewurlfallbackpolicy_test.cpp)
	target_include_directories(messagewidgets_view_url_fallback_policy_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
	target_compile_features(messagewidgets_view_url_fallback_policy_tests PRIVATE cxx_std_11)
	target_link_libraries(messagewidgets_view_url_fallback_policy_tests PRIVATE Qt6::Core)
	add_test(NAME messagewidgets_view_url_fallback_policy_tests
		COMMAND messagewidgets_view_url_fallback_policy_tests)

	add_executable(messagewidgets_image_paste_policy_tests EXCLUDE_FROM_ALL
		${MESSAGEWIDGETS_TEST_SOURCE_DIR}/imagepastepolicy_test.cpp)
	target_include_directories(messagewidgets_image_paste_policy_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
	target_compile_features(messagewidgets_image_paste_policy_tests PRIVATE cxx_std_11)
	target_link_libraries(messagewidgets_image_paste_policy_tests PRIVATE Qt6::Core Qt6::Gui)

	add_executable(messagewidgets_editor_image_paste_tests EXCLUDE_FROM_ALL
		${MESSAGEWIDGETS_TEST_SOURCE_DIR}/test_messageeditor.cpp ${CMAKE_CURRENT_SOURCE_DIR}/messageeditor.cpp)
	target_include_directories(messagewidgets_editor_image_paste_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
	target_compile_features(messagewidgets_editor_image_paste_tests PRIVATE cxx_std_11)
	target_link_libraries(messagewidgets_editor_image_paste_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)
endif()
