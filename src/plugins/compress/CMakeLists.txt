project(compress)

set(PLUGIN_NAME "compress")
set(PLUGIN_DISPLAY_NAME "Stream compression")
set(PLUGIN_DEPENDENCIES xmppstreams) # used only in CPack

if (SYSTEM_ZLIB_FOUND)
	# Nix compiler wrappers omit the host zlib development include path.
	if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
		find_package(ZLIB REQUIRED)
		set(ADD_LIBS ZLIB::ZLIB)
	else()
		set(ADD_LIBS ${SYSTEM_ZLIB_FOUND})
	endif()
else (SYSTEM_ZLIB_FOUND)
	set(ADD_LIBS zlib)
endif (SYSTEM_ZLIB_FOUND)

include("compress.cmake")
include("${CMAKE_SOURCE_DIR}/src/plugins/plugins.cmake")
