project(rosterchanger)

set(PLUGIN_NAME "rosterchanger")
set(PLUGIN_DISPLAY_NAME "Roster changer")
set(PLUGIN_DEPENDENCIES roster) # used only in CPack
set(${PLUGIN_NAME}_IS_ESSENTIAL YES)

include("rosterchanger.cmake")
include("${CMAKE_SOURCE_DIR}/src/plugins/plugins.cmake")

if (BUILD_TESTING)
	add_executable(rosterchanger_add_contact_tests EXCLUDE_FROM_ALL
		protocolcontactactionpolicy.cpp protocolcontactactionpolicy_test.cpp)
	target_include_directories(rosterchanger_add_contact_tests PRIVATE
		"${CMAKE_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}")
	target_compile_features(rosterchanger_add_contact_tests PRIVATE cxx_std_11)
	target_link_libraries(rosterchanger_add_contact_tests PRIVATE Qt6::Core)
endif (BUILD_TESTING)
