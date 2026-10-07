project(statusicons)

set(PLUGIN_NAME "statusicons")
set(PLUGIN_DISPLAY_NAME "Status icons")
set(${PLUGIN_NAME}_IS_ESSENTIAL YES)

include("statusicons.cmake")
include("${CMAKE_SOURCE_DIR}/src/plugins/plugins.cmake")

if (IS_ENABLED)
	install(DIRECTORY "${CMAKE_SOURCE_DIR}/resources/statusicons"
		DESTINATION "${INSTALL_RESOURCES}"
		COMPONENT ${PLUGIN_NAME}
		PATTERN ".svn" EXCLUDE)
endif (IS_ENABLED)

if (BUILD_TESTING)
	add_executable(statusicons_roster_presence_tests EXCLUDE_FROM_ALL
		roster_presence_icons_test.cpp
		${CMAKE_SOURCE_DIR}/src/plugins/rostersmodel/rosterindex.cpp
		${CMAKE_SOURCE_DIR}/src/plugins/rostersmodel/rosterindex.h)
	target_include_directories(statusicons_roster_presence_tests PRIVATE
		"${CMAKE_SOURCE_DIR}/src"
		"${CMAKE_SOURCE_DIR}/src/plugins/rostersmodel"
		"${CMAKE_CURRENT_SOURCE_DIR}")
	target_compile_features(statusicons_roster_presence_tests PRIVATE cxx_std_11)
	target_link_libraries(statusicons_roster_presence_tests PRIVATE
		statusicons Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Xml)
endif (BUILD_TESTING)
