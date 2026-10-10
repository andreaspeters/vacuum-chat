project(messagewidgets)

set(PLUGIN_NAME "messagewidgets")
set(PLUGIN_DISPLAY_NAME "Message widgets")
set(${PLUGIN_NAME}_IS_ESSENTIAL YES)

include("messagewidgets.cmake")
include("${CMAKE_SOURCE_DIR}/src/plugins/plugins.cmake")

if(BUILD_TESTING)
	add_executable(messagewidgets_image_paste_policy_tests EXCLUDE_FROM_ALL
		imagepastepolicy_test.cpp)
	target_include_directories(messagewidgets_image_paste_policy_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
	target_compile_features(messagewidgets_image_paste_policy_tests PRIVATE cxx_std_11)
	target_link_libraries(messagewidgets_image_paste_policy_tests PRIVATE Qt6::Core Qt6::Gui)

	add_executable(messagewidgets_editor_image_paste_tests EXCLUDE_FROM_ALL
		test_messageeditor.cpp messageeditor.cpp)
	target_include_directories(messagewidgets_editor_image_paste_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
	target_compile_features(messagewidgets_editor_image_paste_tests PRIVATE cxx_std_11)
	target_link_libraries(messagewidgets_editor_image_paste_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)
endif()
