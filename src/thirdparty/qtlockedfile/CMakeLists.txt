include("${CMAKE_SOURCE_DIR}/src/make/config.cmake")

project(qtlockedfile CXX)

set(SOURCES "qtlockedfile.cpp")

# Qt6 setup: minimal Qt (no GUI needed for lock file)
find_package(Qt6 COMPONENTS Core REQUIRED)
if (OS2)
	set(SOURCES ${SOURCES} "qtlockedfile_os2.cpp")
elseif (UNIX)
	set(SOURCES ${SOURCES} "qtlockedfile_unix.cpp")
elseif (WIN32)
	set(SOURCES ${SOURCES} "qtlockedfile_win.cpp")
endif (OS2)

add_definitions(-DUNICODE)

add_library(qtlockedfile STATIC ${SOURCES})

target_link_libraries(qtlockedfile PRIVATE Qt6::Core)
set_target_properties(qtlockedfile PROPERTIES POSITION_INDEPENDENT_CODE ON)
set_target_properties(qtlockedfile PROPERTIES ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/src/libs"
	COMPILE_FLAGS "${CMAKE_SHARED_LIBRARY_C_FLAGS}"
	LINK_FLAGS "${CMAKE_SHARED_LIBRARY_C_FLAGS}")
