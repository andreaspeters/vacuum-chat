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

	add_executable(accountmanager_protocol_account_root_tests EXCLUDE_FROM_ALL
		accountmanager_test.cpp)
	target_include_directories(accountmanager_protocol_account_root_tests PRIVATE
		"${CMAKE_SOURCE_DIR}/src"
		"${CMAKE_CURRENT_SOURCE_DIR}")
	target_compile_features(accountmanager_protocol_account_root_tests PRIVATE cxx_std_11)
	target_link_libraries(accountmanager_protocol_account_root_tests PRIVATE Qt6::Core)
endif()
