project(simplemessagestyle)

set(PLUGIN_NAME "simplemessagestyle")
set(PLUGIN_DISPLAY_NAME "Simple message style")
set(${PLUGIN_NAME}_IS_ESSENTIAL YES)

include("simplemessagestyle.cmake")
include("${CMAKE_SOURCE_DIR}/src/plugins/plugins.cmake")

if (BUILD_TESTING)
	add_executable(simplemessagestyle_autoscroll_tests EXCLUDE_FROM_ALL
		autoscroll_test.cpp
		roundedimage.h
		simplemessagestyle.cpp
		simplemessagestyle.h
		styleviewer.cpp
		styleviewer.h)
	target_compile_features(simplemessagestyle_autoscroll_tests PRIVATE cxx_std_11)
	target_compile_definitions(simplemessagestyle_autoscroll_tests PRIVATE
		AUTOSCROLL_STYLE_PATH=\"${CMAKE_SOURCE_DIR}/resources/simplemessagestyles/plain\"
		AVATAR_STYLE_PATH=\"${CMAKE_SOURCE_DIR}/resources/simplemessagestyles/modern-chat\")
	target_link_libraries(simplemessagestyle_autoscroll_tests PRIVATE
		${VACUUM_UTILS_NAME}
		Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Xml Qt6::Sql)
	add_executable(simplemessagestyle_avatar_cache_tests EXCLUDE_FROM_ALL
		avatar_cache_test.cpp avatarpathcache.h)
	target_compile_features(simplemessagestyle_avatar_cache_tests PRIVATE cxx_std_11)
	target_link_libraries(simplemessagestyle_avatar_cache_tests PRIVATE Qt6::Core)
endif()

if (IS_ENABLED)
	install(DIRECTORY "${CMAKE_SOURCE_DIR}/resources/simplemessagestyles"
		DESTINATION "${INSTALL_RESOURCES}"
		COMPONENT ${PLUGIN_NAME}
		PATTERN ".svn" EXCLUDE)
endif (IS_ENABLED)
