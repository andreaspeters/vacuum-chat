project(avatars)

set(PLUGIN_NAME "avatars")
set(PLUGIN_DISPLAY_NAME "Avatars")
set(PLUGIN_DEPENDENCIES vcard) # used only in CPack

include("avatars.cmake")
include("${CMAKE_SOURCE_DIR}/src/plugins/plugins.cmake")

if (BUILD_TESTING)
	add_executable(protocol_account_avatar_policy_tests EXCLUDE_FROM_ALL
		protocolaccountavatarpolicy_test.cpp)
	target_include_directories(protocol_account_avatar_policy_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
	target_compile_features(protocol_account_avatar_policy_tests PRIVATE cxx_std_11)
	target_link_libraries(protocol_account_avatar_policy_tests PRIVATE Qt6::Core)
endif()
