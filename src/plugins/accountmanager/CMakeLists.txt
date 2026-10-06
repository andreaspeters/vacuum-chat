project(accountmanager)

set(PLUGIN_NAME "accountmanager")
set(PLUGIN_DISPLAY_NAME "Account manager")
set(PLUGIN_DEPENDENCIES xmppstreams) # used only in CPack
set(${PLUGIN_NAME}_IS_ESSENTIAL YES)

include("accountmanager.cmake")
include("${CMAKE_SOURCE_DIR}/src/plugins/plugins.cmake")

if (BUILD_TESTING)
	add_executable(protocol_account_type_tests EXCLUDE_FROM_ALL
		protocolaccounttype_test.cpp)
	target_include_directories(protocol_account_type_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
	target_link_libraries(protocol_account_type_tests PRIVATE Qt6::Core Qt6::Xml)
endif()
