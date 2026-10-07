project(xmppstreams)

set(PLUGIN_NAME "xmppstreams")
set(PLUGIN_DISPLAY_NAME "XMPP streams")
set(${PLUGIN_NAME}_IS_ESSENTIAL YES)

include("xmppstreams.cmake")
include("${CMAKE_SOURCE_DIR}/src/plugins/plugins.cmake")

if (BUILD_TESTING)
	add_executable(xmpp_profile_action_policy_tests EXCLUDE_FROM_ALL
		xmppprofileactionpolicy_test.cpp xmppprofileactionpolicy.cpp)
	target_include_directories(xmpp_profile_action_policy_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
	target_compile_features(xmpp_profile_action_policy_tests PRIVATE cxx_std_11)
	target_link_libraries(xmpp_profile_action_policy_tests PRIVATE
		${VACUUM_UTILS_NAME} Qt6::Core Qt6::Gui Qt6::Xml)
endif()
if (BUILD_TESTING)
	add_executable(xmpp_profile_adapter_tests EXCLUDE_FROM_ALL
		xmpp_profile_adapter_test.cpp)
	target_include_directories(xmpp_profile_adapter_tests PRIVATE
		"${CMAKE_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}")
	target_compile_features(xmpp_profile_adapter_tests PRIVATE cxx_std_11)
	target_link_libraries(xmpp_profile_adapter_tests PRIVATE
		xmppstreams ${VACUUM_UTILS_NAME} Qt6::Core Qt6::Gui Qt6::Network Qt6::Widgets Qt6::Xml)
endif()
