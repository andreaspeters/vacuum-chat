project(clientinfo)

set(PLUGIN_NAME "clientinfo")
set(PLUGIN_DISPLAY_NAME "Client info")
set(PLUGIN_DEPENDENCIES stanzaprocessor) # used only in CPack

include("clientinfo.cmake")
include("${CMAKE_SOURCE_DIR}/src/plugins/plugins.cmake")

if (BUILD_TESTING)
	add_executable(clientinfo_software_version_policy_tests EXCLUDE_FROM_ALL
		protocolsoftwareversionpolicy_test.cpp)
	target_include_directories(clientinfo_software_version_policy_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
	target_compile_features(clientinfo_software_version_policy_tests PRIVATE cxx_std_11)
	target_link_libraries(clientinfo_software_version_policy_tests PRIVATE Qt6::Core)
endif (BUILD_TESTING)
