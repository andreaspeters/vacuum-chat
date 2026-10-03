project(multiuserchat)

set(PLUGIN_NAME "multiuserchat")
set(PLUGIN_DISPLAY_NAME "Multi-user chat")
set(PLUGIN_DEPENDENCIES stanzaprocessor) # used only in CPack

include("multiuserchat.cmake")
include("${CMAKE_SOURCE_DIR}/src/plugins/plugins.cmake")

if (BUILD_TESTING)
	add_executable(multiuserchat_context_tests EXCLUDE_FROM_ALL
		multiuserchatcontext_test.cpp
		multiuserchatcontext.cpp)
	target_compile_features(multiuserchat_context_tests PRIVATE cxx_std_11)
	target_link_libraries(multiuserchat_context_tests PRIVATE ${VACUUM_UTILS_NAME} Qt6::Core Qt6::Xml)
endif()
