project(chatmessagehandler)

set(PLUGIN_NAME "chatmessagehandler")
set(PLUGIN_DISPLAY_NAME "Chat message handler")
set(PLUGIN_DEPENDENCIES messagewidgets messageprocessor messagestyles) # used only in CPack
set(${PLUGIN_NAME}_IS_ESSENTIAL YES)

include("chatmessagehandler.cmake")
include("${CMAKE_SOURCE_DIR}/src/plugins/plugins.cmake")

if (BUILD_TESTING)
	add_executable(chatmessagehandler_matrix_reply_tests EXCLUDE_FROM_ALL
		matrixreply_test.cpp)
	target_compile_features(chatmessagehandler_matrix_reply_tests PRIVATE cxx_std_11)
	target_link_libraries(chatmessagehandler_matrix_reply_tests PRIVATE Qt6::Core)
	add_executable(chatmessagehandler_matrix_history_tests EXCLUDE_FROM_ALL
		protocolmessagehistory_test.cpp)
	target_include_directories(chatmessagehandler_matrix_history_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
	target_compile_features(chatmessagehandler_matrix_history_tests PRIVATE cxx_std_11)
	target_link_libraries(chatmessagehandler_matrix_history_tests PRIVATE Qt6::Core)
	add_executable(chatmessagehandler_routing_tests EXCLUDE_FROM_ALL
		protocolmessagerouting_test.cpp)
	target_compile_features(chatmessagehandler_routing_tests PRIVATE cxx_std_11)
	add_executable(chatmessagehandler_unicode_avatar_tests EXCLUDE_FROM_ALL
		unicodeavatar.cpp unicodeavatar_test.cpp)
	target_compile_features(chatmessagehandler_unicode_avatar_tests PRIVATE cxx_std_11)
	target_link_libraries(chatmessagehandler_unicode_avatar_tests PRIVATE ${VACUUM_UTILS_NAME} Qt6::Core Qt6::Gui)
endif()
