include("${CMAKE_SOURCE_DIR}/src/make/config.cmake")

project(idle CXX)

set(SOURCES "idle.cpp")

# Qt6 setup
find_package(Qt6 COMPONENTS Core Gui Widgets REQUIRED)

# Platform-specific sources
if (APPLE)
	set(SOURCES ${SOURCES} "idle_mac.cpp")
elseif (OS2)
	set(SOURCES ${SOURCES} "idle_os2.cpp")
elseif (UNIX)
	set(SOURCES ${SOURCES} "idle_x11.cpp")
	include(FindX11)
	find_path(X11_Xscreensaver_INCLUDE_PATH X11/extensions/scrnsaver.h)
	if (X11_Xscreensaver_FOUND AND X11_Xscreensaver_INCLUDE_PATH)
		add_definitions(-DHAVE_XSS)
		include_directories(AFTER "${X11_Xscreensaver_INCLUDE_PATH}")
	endif (X11_Xscreensaver_FOUND)
elseif (WIN32)
	set(SOURCES ${SOURCES} "idle_win.cpp")
endif (APPLE)

add_definitions(-DUNICODE)

add_library(idle STATIC ${SOURCES})

target_link_libraries(idle PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)
set_target_properties(idle PROPERTIES POSITION_INDEPENDENT_CODE ON)
set_target_properties(idle PROPERTIES ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/src/libs"
	COMPILE_FLAGS "${CMAKE_SHARED_LIBRARY_C_FLAGS}"
	LINK_FLAGS "${CMAKE_SHARED_LIBRARY_C_FLAGS}")
