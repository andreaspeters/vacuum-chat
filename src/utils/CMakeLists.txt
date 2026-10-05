# needed for languages.cmake to correctly generate CPack components
set(PLUGIN_NAME ${VACUUM_UTILS_NAME})
set(PLUGIN_DISPLAY_NAME "utils library")

include("${CMAKE_SOURCE_DIR}/src/make/config.cmake")
include("${CMAKE_SOURCE_DIR}/src/translations/languages.cmake") 
include("utils.cmake")

project(${VACUUM_UTILS_NAME} CXX)

add_definitions("-DUTILS_DLL")
add_definitions("-DQXT_STATIC")
include_directories(AFTER  "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/src/thirdparty/zlib")

if (NOT OS2)
	include(FindX11)
	include_directories(AFTER ${X11_INCLUDE_DIR})
endif (NOT OS2)
if (NOT SYSTEM_IDN_FOUND)
	include_directories("${CMAKE_SOURCE_DIR}/src/thirdparty/idn")
endif (NOT SYSTEM_IDN_FOUND)

add_translations(TRANSLATIONS "${VACUUM_UTILS_NAME}" ${SOURCES})
if (OS2)
	add_library(${VACUUM_UTILS_NAME} STATIC ${SOURCES} ${HEADERS} ${TRANSLATIONS})
	else()
	add_library(${VACUUM_UTILS_NAME} SHARED ${SOURCES} ${HEADERS} ${TRANSLATIONS})
	endif()
if (OS2)
	set_target_properties(${VACUUM_UTILS_NAME} PROPERTIES OUTPUT_NAME "vacuum")
endif()

# Qt6 setup
find_package(Qt6 COMPONENTS Core Gui Widgets Network Xml REQUIRED)
if (NOT OS2)
	set_target_properties(${VACUUM_UTILS_NAME} PROPERTIES
		VERSION "${VACUUM_UTILS_ABI}"
		SOVERSION "${VACUUM_UTILS_ABI}")
endif (NOT OS2)

set(ADD_LIBS qxtglobalshortcut idle)
if (SYSTEM_ZLIB_FOUND)
	list(APPEND ADD_LIBS "${SYSTEM_ZLIB_FOUND}")
else (SYSTEM_ZLIB_FOUND)
	list(APPEND ADD_LIBS zlib)
endif (SYSTEM_ZLIB_FOUND)

if (SYSTEM_IDN_FOUND)
	list(APPEND ADD_LIBS "${SYSTEM_IDN_FOUND}")
else (SYSTEM_IDN_FOUND)
	list(APPEND ADD_LIBS idn)
endif (SYSTEM_IDN_FOUND)

if (SYSTEM_MINIZIP_FOUND)
	list(APPEND ADD_LIBS "${SYSTEM_MINIZIP_FOUND}")
else (SYSTEM_MINIZIP_FOUND)
	list(APPEND ADD_LIBS minizip)
endif (SYSTEM_MINIZIP_FOUND)


if (NOT OS2 AND X11_Xscreensaver_FOUND)
	message(STATUS "Found Xscreensaver: ${X11_Xscreensaver_LIB}")
	list(APPEND ADD_LIBS "${X11_Xscreensaver_LIB}")
else (NOT OS2 AND X11_Xscreensaver_FOUND)
	message(STATUS "Xscreensaver not found")
endif (X11_Xscreensaver_FOUND)

if (APPLE)
	list(APPEND ADD_LIBS "-framework Carbon -framework IOKit -framework Cocoa")
endif (APPLE)

target_link_libraries(${VACUUM_UTILS_NAME} PRIVATE
	Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Xml ${ADD_LIBS})

if (WIN32 OR OS2)
	set_target_properties(${VACUUM_UTILS_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
	install(TARGETS ${VACUUM_UTILS_NAME}
		RUNTIME DESTINATION "${INSTALL_LIBS}"
		COMPONENT ${VACUUM_UTILS_NAME})
endif (WIN32 OR OS2)

if (UNIX AND NOT APPLE AND NOT OS2)
	install(TARGETS ${VACUUM_UTILS_NAME}
		LIBRARY DESTINATION "${INSTALL_LIBS}"
		COMPONENT ${VACUUM_UTILS_NAME})
endif (UNIX AND NOT APPLE AND NOT OS2)

cpack_add_component(${VACUUM_UTILS_NAME}
	DISPLAY_NAME "Utils library"
	REQUIRED
	GROUP core)

if (INSTALL_SDK)
	file(GLOB ALL_HEADERS "*.h")
	install(FILES ${ALL_HEADERS}
		DESTINATION "${INSTALL_INCLUDES}/utils"
		COMPONENT sdk)
	if (WIN32 OR OS2)
		install(TARGETS ${VACUUM_UTILS_NAME}
			ARCHIVE DESTINATION "${INSTALL_LIBS}"
			COMPONENT sdk)
	endif (WIN32 OR OS2)
endif (INSTALL_SDK)

if (BUILD_TESTING)
	add_executable(messagenotificationmute_tests EXCLUDE_FROM_ALL
		messagenotificationmute_test.cpp)
	target_compile_features(messagenotificationmute_tests PRIVATE cxx_std_11)
	target_include_directories(messagenotificationmute_tests PRIVATE
		"${CMAKE_SOURCE_DIR}/src")
	target_link_libraries(messagenotificationmute_tests PRIVATE
		${VACUUM_UTILS_NAME} Qt6::Xml)

	add_executable(image_load_scheduler_tests EXCLUDE_FROM_ALL
		imageloadscheduler_test.cpp)
	target_compile_features(image_load_scheduler_tests PRIVATE cxx_std_11)
	target_include_directories(image_load_scheduler_tests PRIVATE
		"${CMAKE_SOURCE_DIR}/src")
	target_link_libraries(image_load_scheduler_tests PRIVATE
		${VACUUM_UTILS_NAME} Qt6::Core Qt6::Gui)

	add_executable(matrixhtml_test EXCLUDE_FROM_ALL
		matrixhtml_test.cpp)
	target_compile_features(matrixhtml_test PRIVATE cxx_std_11)
	target_include_directories(matrixhtml_test PRIVATE
		"${CMAKE_SOURCE_DIR}/src")
	target_link_libraries(matrixhtml_test PRIVATE
		${VACUUM_UTILS_NAME} Qt6::Core Qt6::Gui Qt6::Xml)
endif()
