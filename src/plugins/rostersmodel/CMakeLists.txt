project(rostersmodel)

set(PLUGIN_NAME "rostersmodel")
set(PLUGIN_DISPLAY_NAME "Rosters model")
set(${PLUGIN_NAME}_IS_ESSENTIAL YES)

include("rostersmodel.cmake")
include("${CMAKE_SOURCE_DIR}/src/plugins/plugins.cmake")

if (BUILD_TESTING)
	add_executable(roster_protocol_lifecycle_tests EXCLUDE_FROM_ALL
		protocolrosterlifecycle_test.cpp)
	target_include_directories(roster_protocol_lifecycle_tests PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}")
	target_link_libraries(roster_protocol_lifecycle_tests PRIVATE Qt6::Core)

	add_executable(roster_protocol_presence_tests EXCLUDE_FROM_ALL
		roster_protocol_presence_test.cpp
		rosterindex.cpp
		rostersmodel.cpp)
	target_include_directories(roster_protocol_presence_tests PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/src")
	target_link_libraries(roster_protocol_presence_tests PRIVATE
		${VACUUM_UTILS_NAME}
		Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Xml Qt6::Sql)
endif()
