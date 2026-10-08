project(notifications)

set(PLUGIN_NAME "notifications")
set(PLUGIN_DISPLAY_NAME "Notifications")
set(${PLUGIN_NAME}_IS_ESSENTIAL YES)

include("notifications.cmake")
include("${CMAKE_SOURCE_DIR}/src/plugins/plugins.cmake")

if (BUILD_TESTING)
	add_executable(protocol_notification_roster_stream_id_tests EXCLUDE_FROM_ALL
		protocolnotificationrosterstreamid_test.cpp)
	target_include_directories(protocol_notification_roster_stream_id_tests PRIVATE
		"${CMAKE_SOURCE_DIR}/src")
	target_link_libraries(protocol_notification_roster_stream_id_tests PRIVATE Qt6::Core)
endif()

if (IS_ENABLED)
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/resources/sounds"
		DESTINATION "${INSTALL_RESOURCES}"
		COMPONENT ${PLUGIN_NAME}
		PATTERN ".svn" EXCLUDE)
endif (IS_ENABLED)
