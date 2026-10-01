include("${CMAKE_SOURCE_DIR}/src/make/config.cmake")

project(qxtglobalshortcut CXX)

set(SOURCES "qxtglobal.cpp" "qxtglobalshortcut.cpp")


if (APPLE)
	set(SOURCES ${SOURCES} "qxtglobalshortcut_mac.cpp")
elseif (HAIKU)
	set(SOURCES ${SOURCES} "qxtglobalshortcut_haiku.cpp")
elseif (OS2)
	set(SOURCES ${SOURCES} "qxtglobalshortcut_os2.cpp")
elseif (UNIX)
	set(SOURCES ${SOURCES} "qxtglobalshortcut_x11.cpp")
	include(FindX11)
	include_directories(AFTER ${X11_INCLUDE_DIR})
elseif (WIN32)
	set(SOURCES ${SOURCES} "qxtglobalshortcut_win.cpp")
endif (APPLE)


add_definitions(-DUNICODE)
add_definitions(-DQXT_STATIC)

add_library(qxtglobalshortcut STATIC ${SOURCES} "qxtglobalshortcut.h")
target_link_libraries(qxtglobalshortcut PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)
set_target_properties(qxtglobalshortcut PROPERTIES POSITION_INDEPENDENT_CODE ON)
set_target_properties(qxtglobalshortcut PROPERTIES ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/src/libs"
	COMPILE_FLAGS "${CMAKE_SHARED_LIBRARY_C_FLAGS}"
	LINK_FLAGS "${CMAKE_SHARED_LIBRARY_C_FLAGS}")
