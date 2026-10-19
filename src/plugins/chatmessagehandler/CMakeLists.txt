project(chatmessagehandler)

set(PLUGIN_NAME "chatmessagehandler")
set(PLUGIN_DISPLAY_NAME "Chat message handler")
set(PLUGIN_DEPENDENCIES messagewidgets messageprocessor messagestyles) # used only in CPack
set(${PLUGIN_NAME}_IS_ESSENTIAL YES)

include("chatmessagehandler.cmake")
include("${CMAKE_SOURCE_DIR}/src/plugins/plugins.cmake")

if (BUILD_TESTING)
	set(CHATMESSAGEHANDLER_TEST_SOURCE_DIR "${CMAKE_SOURCE_DIR}/tests/chatmessagehandler")
	add_executable(chatmessagehandler_message_status_tests EXCLUDE_FROM_ALL
		protocolmessagestatus.cpp
		${CHATMESSAGEHANDLER_TEST_SOURCE_DIR}/protocolmessagestatus_test.cpp)
	target_include_directories(chatmessagehandler_message_status_tests PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}")
	target_compile_features(chatmessagehandler_message_status_tests PRIVATE cxx_std_11)
	target_link_libraries(chatmessagehandler_message_status_tests PRIVATE Qt6::Core)
	add_executable(chatmessagehandler_matrix_reply_tests EXCLUDE_FROM_ALL
		${CHATMESSAGEHANDLER_TEST_SOURCE_DIR}/matrixreply_test.cpp)
	target_compile_features(chatmessagehandler_matrix_reply_tests PRIVATE cxx_std_11)
	target_link_libraries(chatmessagehandler_matrix_reply_tests PRIVATE Qt6::Core)
	add_executable(chatmessagehandler_matrix_image_size_tests EXCLUDE_FROM_ALL
		${CHATMESSAGEHANDLER_TEST_SOURCE_DIR}/matrix_image_size_test.cpp)
	target_include_directories(chatmessagehandler_matrix_image_size_tests PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}")
	target_compile_features(chatmessagehandler_matrix_image_size_tests PRIVATE cxx_std_11)
	target_link_libraries(chatmessagehandler_matrix_image_size_tests PRIVATE Qt6::Core)
	add_executable(chatmessagehandler_matrix_history_tests EXCLUDE_FROM_ALL
		${CHATMESSAGEHANDLER_TEST_SOURCE_DIR}/protocolmessagehistory_test.cpp)
	target_include_directories(chatmessagehandler_matrix_history_tests PRIVATE
		"${CMAKE_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}")
	target_compile_features(chatmessagehandler_matrix_history_tests PRIVATE cxx_std_11)
	target_link_libraries(chatmessagehandler_matrix_history_tests PRIVATE Qt6::Core)
	add_executable(chatmessagehandler_room_sidebar_tests EXCLUDE_FROM_ALL
		${CHATMESSAGEHANDLER_TEST_SOURCE_DIR}/roomsidebarstate_test.cpp)
	target_include_directories(chatmessagehandler_room_sidebar_tests PRIVATE
		"${CMAKE_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}")
	target_compile_features(chatmessagehandler_room_sidebar_tests PRIVATE cxx_std_11)
	target_link_libraries(chatmessagehandler_room_sidebar_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)
	add_executable(chatmessagehandler_routing_tests EXCLUDE_FROM_ALL
		${CHATMESSAGEHANDLER_TEST_SOURCE_DIR}/protocolmessagerouting_test.cpp)
	target_include_directories(chatmessagehandler_routing_tests PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}")
	target_compile_features(chatmessagehandler_routing_tests PRIVATE cxx_std_11)
	add_executable(chatmessagehandler_unicode_avatar_tests EXCLUDE_FROM_ALL
		unicodeavatar.cpp ${CHATMESSAGEHANDLER_TEST_SOURCE_DIR}/unicodeavatar_test.cpp)
	target_include_directories(chatmessagehandler_unicode_avatar_tests PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}")
	target_compile_features(chatmessagehandler_unicode_avatar_tests PRIVATE cxx_std_11)
	target_link_libraries(chatmessagehandler_unicode_avatar_tests PRIVATE ${VACUUM_UTILS_NAME} Qt6::Core Qt6::Gui)
	add_executable(chatmessagehandler_rounded_avatar_tests EXCLUDE_FROM_ALL
		${CHATMESSAGEHANDLER_TEST_SOURCE_DIR}/roundedavatar_test.cpp)
	target_include_directories(chatmessagehandler_rounded_avatar_tests PRIVATE
		"${CMAKE_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}")
	target_compile_features(chatmessagehandler_rounded_avatar_tests PRIVATE cxx_std_11)
	target_link_libraries(chatmessagehandler_rounded_avatar_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)

	add_executable(chatmessagehandler_protocol_capabilities_tests EXCLUDE_FROM_ALL
		${CHATMESSAGEHANDLER_TEST_SOURCE_DIR}/protocolmessaging_capabilities_test.cpp)
	target_include_directories(chatmessagehandler_protocol_capabilities_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
	target_compile_features(chatmessagehandler_protocol_capabilities_tests PRIVATE cxx_std_11)
	target_link_libraries(chatmessagehandler_protocol_capabilities_tests PRIVATE Qt6::Core)
	add_executable(chatmessagehandler_room_member_actions_tests EXCLUDE_FROM_ALL
		${CHATMESSAGEHANDLER_TEST_SOURCE_DIR}/protocolroommemberactions_test.cpp)
	target_include_directories(chatmessagehandler_room_member_actions_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
	target_compile_features(chatmessagehandler_room_member_actions_tests PRIVATE cxx_std_11)
	target_link_libraries(chatmessagehandler_room_member_actions_tests PRIVATE Qt6::Core)
	add_test(NAME chatmessagehandler_room_member_actions_tests COMMAND chatmessagehandler_room_member_actions_tests)
	add_executable(chatmessagehandler_history_policy_tests EXCLUDE_FROM_ALL
		${CHATMESSAGEHANDLER_TEST_SOURCE_DIR}/protocolhistoryactionpolicy_test.cpp)
	target_include_directories(chatmessagehandler_history_policy_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
	target_compile_features(chatmessagehandler_history_policy_tests PRIVATE cxx_std_11)
	target_link_libraries(chatmessagehandler_history_policy_tests PRIVATE Qt6::Core)
	add_test(NAME chatmessagehandler_history_policy_tests COMMAND chatmessagehandler_history_policy_tests)
	add_executable(chatmessagehandler_history_sync_throttle_tests EXCLUDE_FROM_ALL
		${CHATMESSAGEHANDLER_TEST_SOURCE_DIR}/protocolhistorysyncthrottle_test.cpp)
	target_include_directories(chatmessagehandler_history_sync_throttle_tests PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}")
	target_compile_features(chatmessagehandler_history_sync_throttle_tests PRIVATE cxx_std_11)
	target_link_libraries(chatmessagehandler_history_sync_throttle_tests PRIVATE Qt6::Core)
	add_executable(chatmessagehandler_plugin_load_tests EXCLUDE_FROM_ALL
		${CHATMESSAGEHANDLER_TEST_SOURCE_DIR}/chatmessagehandler_plugin_load_test.cpp)
	target_link_libraries(chatmessagehandler_plugin_load_tests PRIVATE Qt6::Core Qt6::Widgets)
	target_compile_features(chatmessagehandler_plugin_load_tests PRIVATE cxx_std_11)
	add_dependencies(chatmessagehandler_plugin_load_tests chatmessagehandler)
endif()
