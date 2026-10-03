project(recentcontacts)

set(PLUGIN_NAME "recentcontacts")
set(PLUGIN_DISPLAY_NAME "Recent Contacts")
set(PLUGIN_DEPENDENCIES privatestorage accountmanager) # used only in CPack

include("recentcontacts.cmake")
include("${CMAKE_SOURCE_DIR}/src/plugins/plugins.cmake")

if (BUILD_TESTING)
	add_executable(recentcontacts_protocol_icon_tests EXCLUDE_FROM_ALL
		recentcontacts_protocol_icon_test.cpp
		recentcontactsprotocolicon.cpp)
	target_compile_definitions(recentcontacts_protocol_icon_tests PRIVATE
		RECENTCONTACTS_RESOURCE_DIR="${CMAKE_SOURCE_DIR}/resources")
	target_compile_features(recentcontacts_protocol_icon_tests PRIVATE cxx_std_11)
	target_link_libraries(recentcontacts_protocol_icon_tests PRIVATE
		${VACUUM_UTILS_NAME} Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Xml)
endif()
