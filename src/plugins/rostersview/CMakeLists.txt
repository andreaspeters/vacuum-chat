project(rostersview)

set(PLUGIN_NAME "rostersview")
set(PLUGIN_DISPLAY_NAME "Rosters view")
set(PLUGIN_DEPENDENCIES rostersmodel) # used only in CPack
set(${PLUGIN_NAME}_IS_ESSENTIAL YES)

include("rostersview.cmake")
include("${CMAKE_SOURCE_DIR}/src/plugins/plugins.cmake")

if(BUILD_TESTING)
	add_executable(protocol_account_identifier_tests EXCLUDE_FROM_ALL
		protocolaccountidentifier_test.cpp)
	target_include_directories(protocol_account_identifier_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
	target_link_libraries(protocol_account_identifier_tests PRIVATE Qt6::Core)
	add_test(NAME protocol_account_identifier_tests COMMAND protocol_account_identifier_tests)
endif()
