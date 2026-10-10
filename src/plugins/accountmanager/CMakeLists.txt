project(accountmanager)

set(PLUGIN_NAME "accountmanager")
set(PLUGIN_DISPLAY_NAME "Account manager")
set(PLUGIN_DEPENDENCIES xmppstreams) # used only in CPack
set(${PLUGIN_NAME}_IS_ESSENTIAL YES)

if (NOT OS2)
	find_package(Qt6 REQUIRED COMPONENTS Bluetooth)
	list(APPEND ADD_LIBS Qt6::Bluetooth)
endif()

include("accountmanager.cmake")
include("${CMAKE_SOURCE_DIR}/src/plugins/plugins.cmake")

if (BUILD_TESTING)
	add_executable(protocol_account_type_tests EXCLUDE_FROM_ALL
		protocolaccounttype_test.cpp)
	target_include_directories(protocol_account_type_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
	target_link_libraries(protocol_account_type_tests PRIVATE Qt6::Core Qt6::Xml)

	add_executable(ax25_account_config_tests EXCLUDE_FROM_ALL
		ax25accountconfig_test.cpp ax25accountconfig.cpp)
	target_include_directories(ax25_account_config_tests PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}")
	target_compile_features(ax25_account_config_tests PRIVATE cxx_std_11)
	target_link_libraries(ax25_account_config_tests PRIVATE Qt6::Core)

	add_executable(accountmanager_protocol_account_root_tests EXCLUDE_FROM_ALL
		accountmanager_test.cpp)
	target_include_directories(accountmanager_protocol_account_root_tests PRIVATE
		"${CMAKE_SOURCE_DIR}/src"
		"${CMAKE_CURRENT_SOURCE_DIR}")
	target_compile_features(accountmanager_protocol_account_root_tests PRIVATE cxx_std_11)
	target_link_libraries(accountmanager_protocol_account_root_tests PRIVATE Qt6::Core)

	add_executable(meshcore_ble_device_catalog_tests EXCLUDE_FROM_ALL
		meshcorebledevicecatalog_test.cpp)
	target_include_directories(meshcore_ble_device_catalog_tests PRIVATE
		"${CMAKE_SOURCE_DIR}/src"
		"${CMAKE_CURRENT_SOURCE_DIR}")
	target_compile_features(meshcore_ble_device_catalog_tests PRIVATE cxx_std_11)
	target_link_libraries(meshcore_ble_device_catalog_tests PRIVATE Qt6::Core)
endif()
