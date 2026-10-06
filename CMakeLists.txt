cmake_minimum_required(VERSION 3.16)

option(VACUUM_OS2_CROSS_COMPILE
	"Cross-compile Vacuum and its plugins for OS/2 using the Qt6 OS/2 toolchain"
	OFF)
set(VACUUM_OS2_TOOLCHAIN_FILE ""
	CACHE FILEPATH "CMake toolchain file used by VACUUM_OS2_CROSS_COMPILE")
if (VACUUM_OS2_CROSS_COMPILE AND NOT CMAKE_TOOLCHAIN_FILE)
	if ("${VACUUM_OS2_TOOLCHAIN_FILE}" STREQUAL "")
		set(_vacuum_default_os2_toolchain
			"${CMAKE_SOURCE_DIR}/../qt6-base-os2/qt6-base-os2/cmake/qt6-os2.cmake")
		if (EXISTS "${_vacuum_default_os2_toolchain}")
			set(VACUUM_OS2_TOOLCHAIN_FILE "${_vacuum_default_os2_toolchain}"
				CACHE FILEPATH "CMake toolchain file used by VACUUM_OS2_CROSS_COMPILE" FORCE)
		endif()
	endif()
	if ("${VACUUM_OS2_TOOLCHAIN_FILE}" STREQUAL "")
		message(FATAL_ERROR
			"VACUUM_OS2_CROSS_COMPILE requires CMAKE_TOOLCHAIN_FILE or "
			"VACUUM_OS2_TOOLCHAIN_FILE")
	endif()
	set(CMAKE_TOOLCHAIN_FILE "${VACUUM_OS2_TOOLCHAIN_FILE}"
		CACHE FILEPATH "CMake toolchain file" FORCE)
endif()

project(vacuum-im VERSION 2.0.0 LANGUAGES CXX)

set(VACUUM_PYTHON_EXECUTABLE "python3" CACHE STRING "Python interpreter used by the check target")
set(VACUUM_GIT_EXECUTABLE "git" CACHE STRING "Git executable used by the check target")

if (OS2)
	list(PREPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
	list(APPEND CMAKE_MODULE_PATH "${CMAKE_PREFIX_PATH}/lib/cmake/Qt6")
	# The OS/2 Qt build currently does not install every static plugin target
	# referenced by its generated Qt6*Plugins.cmake files.  Do not make the
	# application configuration depend on those incomplete auto-import lists.
	set(QT_SKIP_AUTO_PLUGIN_INCLUSION ON)
	set(QT_SKIP_AUTO_QML_PLUGIN_INCLUSION ON)
	# The OS/2 CMake port aborts inside its built-in include_directories()
	# command. Preserve the project-wide include roots through compiler flags;
	# source-file-local quoted includes remain handled by the compiler.
	function(include_directories)
	endfunction()
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -idirafter D:/usr/include -I${CMAKE_SOURCE_DIR}/src -I${CMAKE_SOURCE_DIR}/src/thirdparty/zlib -I${CMAKE_SOURCE_DIR}/src/thirdparty/idn")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -idirafter D:/usr/include -I${CMAKE_SOURCE_DIR}/src -I${CMAKE_SOURCE_DIR}/src/thirdparty/zlib -I${CMAKE_SOURCE_DIR}/src/thirdparty/idn")
	list(APPEND CMAKE_AUTOMOC_MOC_OPTIONS "-I${CMAKE_SOURCE_DIR}/src")
	set(CMAKE_CXX_CREATE_SHARED_LIBRARY
		"<CMAKE_CXX_COMPILER> <LANGUAGE_COMPILE_FLAGS> <CMAKE_SHARED_LIBRARY_CXX_FLAGS> <LINK_FLAGS> <CMAKE_SHARED_LIBRARY_CREATE_CXX_FLAGS> -o <TARGET> <OBJECTS> <LINK_LIBRARIES> <TARGET_BASE>.def")
endif (OS2)

find_package(Threads REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Xml Sql)

if (OS2)
	get_filename_component(_vacuum_qt_cmake_dir "${Qt6Core_DIR}" DIRECTORY)
	get_filename_component(_vacuum_qt_lib_dir "${_vacuum_qt_cmake_dir}" DIRECTORY)
	get_filename_component(_vacuum_qt_prefix "${_vacuum_qt_lib_dir}" DIRECTORY)
	foreach(_vacuum_qt_component Core Gui Widgets Network Xml Sql)
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -I${_vacuum_qt_prefix}/include/Qt${_vacuum_qt_component}")
		list(APPEND CMAKE_AUTOMOC_MOC_OPTIONS
			"-I${_vacuum_qt_prefix}/include/Qt${_vacuum_qt_component}")
	endforeach()
endif()

set(VACUUM_OS2_QT_STATIC_LIB_DIR ""
	CACHE PATH "Directory containing the static OS/2 Qt6 archives")
if (OS2 AND NOT "${VACUUM_OS2_QT_STATIC_LIB_DIR}" STREQUAL "")
	foreach(_vacuum_qt_component Core Gui Widgets Network Xml Sql)
		set(_vacuum_qt_archive
			"${VACUUM_OS2_QT_STATIC_LIB_DIR}/Qt6${_vacuum_qt_component}.a")
		if (NOT EXISTS "${_vacuum_qt_archive}")
			message(FATAL_ERROR "Missing OS/2 static Qt archive: ${_vacuum_qt_archive}")
		endif()
		# The existing Qt OS/2 package describes these static archives as
		# imported shared-library implementations. Override only the imported
		# archive path so every Vacuum target uses the working .a files.
		set_target_properties(Qt6::${_vacuum_qt_component} PROPERTIES
			IMPORTED_IMPLIB "${_vacuum_qt_archive}"
			IMPORTED_IMPLIB_DEBUG "${_vacuum_qt_archive}"
			IMPORTED_IMPLIB_RELEASE "${_vacuum_qt_archive}")
	endforeach()
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

add_custom_target(updatets)

include("src/make/config.cmake")
include("src/translations/languages.cmake")
include_directories("${CMAKE_SOURCE_DIR}/src")

install(DIRECTORY "resources/menuicons"
	DESTINATION "${INSTALL_RESOURCES}"
	PATTERN ".svn" EXCLUDE)

include("src/packages/package.cmake")

set(INSTALL_SDK YES CACHE BOOL "Install files needed to build custom plugins")
set(INSTALL_DOCS YES CACHE BOOL "Install documentation (currently it's only AUTHORS, CHANGELOG, README.md and COPYING)")
set(LANGS "" CACHE STRING "List of languages to build localization for (this variable overrides any of LANG_*)")
set(USED_LANGS "" CACHE INTERNAL "List of languages actually used for generating targets")
add_lang_options()

set(FORCE_BUNDLED_MINIZIP NO CACHE BOOL "Use bundled minizip even if local system have one")

find_library(SYSTEM_ZLIB_FOUND z)
find_library(SYSTEM_IDN_FOUND idn)
find_library(SYSTEM_MINIZIP_FOUND minizip)
find_library(SYSTEM_QTLOCKEDFILE_FOUND QtSolutions_LockedFile-2.4)

if (FORCE_BUNDLED_MINIZIP)
	set(SYSTEM_MINIZIP_FOUND NO)
endif (FORCE_BUNDLED_MINIZIP)

if (SYSTEM_ZLIB_FOUND)
	add_definitions(-DUSE_SYSTEM_ZLIB)
endif (SYSTEM_ZLIB_FOUND)

if (SYSTEM_IDN_FOUND)
	add_definitions(-DUSE_SYSTEM_IDN)
endif (SYSTEM_IDN_FOUND)

if (SYSTEM_MINIZIP_FOUND)
	add_definitions(-DUSE_SYSTEM_MINIZIP)
endif (SYSTEM_MINIZIP_FOUND)

if (SYSTEM_QTLOCKEDFILE_FOUND)
	add_definitions(-DUSE_SYSTEM_QTLOCKEDFILE)
endif (SYSTEM_QTLOCKEDFILE_FOUND)

if ("${LANGS}" STREQUAL "")
	process_lang_options()
else ("${LANGS}" STREQUAL "")
	set_enabled_langs("${LANGS}")
endif ("${LANGS}" STREQUAL "")

if (INSTALL_DOCS)
	install(FILES "AUTHORS" "CHANGELOG" "README.md" "COPYING" "TRANSLATORS"
		DESTINATION "${INSTALL_DOCUMENTS}")
endif (INSTALL_DOCS)

add_subdirectory(src)

add_custom_target(check
	COMMAND "${VACUUM_PYTHON_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/matrix_protocol_smoke.py"
	COMMAND "${VACUUM_GIT_EXECUTABLE}" -c core.whitespace=cr-at-eol diff --check
	WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
	COMMENT "Running source and smoke checks"
	VERBATIM)
add_dependencies(check chatmessagehandler recentcontacts)

# runtime deps on Windows
if (WIN32)
	set(WEBKIT_WITHOUT_PHONON NO CACHE BOOL "Turn this flag ON if you use QtWebKit built without Phonon support")
	set(WEBKIT_WITHOUT_XMLPATTERNS NO CACHE BOOL "Turn this flag ON if you use QtWebKit built without QtXmlPatterns")
	cpack_add_component_group(libs
		DISPLAY_NAME "Shared libraries"
		DESCRIPTION "Required libraries that already may be present in your system")
	set(QT_LIBS "${QT_LIBRARY_DIR}/../bin/Qt6Core.dll"
		"${QT_LIBRARY_DIR}/../bin/Qt6Gui.dll"
		"${QT_LIBRARY_DIR}/../bin/Qt6Network.dll"
		"${QT_LIBRARY_DIR}/../bin/Qt6Xml.dll"
		"${QT_LIBRARY_DIR}/../bin/Qt6Widgets.dll")
	install(FILES ${QT_LIBS}
		DESTINATION "."
		COMPONENT qt_libs)
	install(DIRECTORY "${QT_PLUGINS_DIR}/imageformats"
		DESTINATION "."
		COMPONENT qt_libs
		FILES_MATCHING
		PATTERN "*.dll"
		PATTERN "*d4.dll" EXCLUDE)
	cpack_add_component(qt_libs
		DISPLAY_NAME "Qt6 libraries"
		DESCRIPTION "May be unchecked if you already have working Qt6 installation"
		GROUP libs)
	# Qt localization
	foreach(LANG ${LOCALIZED_LANGS})
		string(SUBSTRING ${LANG} 0 2 SHORTLANG)
		install(FILES "${QT_TRANSLATIONS_DIR}/qt_${SHORTLANG}.qm"
			DESTINATION "${INSTALL_TRANSLATIONS}/${LANG}"
			COMPONENT qt_${LANG})
		cpack_add_component(qt_${LANG}
			DISPLAY_NAME "Qt6 translation"
			GROUP ${LANG}_translation
			DEPENDS qt_libs)
	endforeach(LANG)

	install(FILES "libeay32.dll" "ssleay32.dll"
		DESTINATION "."
		COMPONENT openssl_libs)
	cpack_add_component(openssl_libs
		DISPLAY_NAME "OpenSSL libraries"
		DESCRIPTION "May be unchecked if you already have OpenSSL installed"
		GROUP libs)

	if (MINGW)
		install(FILES "mingwm10.dll" "libgcc_s_dw2-1.dll"
			DESTINATION "."
			COMPONENT mingw_runtime)
		cpack_add_component(mingw_runtime
			DISPLAY_NAME "MinGW runtime libraries"
			DESCRIPTION "May be unchecked if you already have MinGW installed"
			GROUP libs)
	elseif (MSVC)
		# TODO: check these patterns
		file(GLOB MSVC_DLLS "msvc*.dll")
		file(GLOB MANIFEST "Microsoft.VC*.CRT.manifest")
		install(FILES ${MSVC_DLLS} ${MANIFEST}
			DESTINATION "."
			COMPONENT msvc_runtime)
		cpack_add_component(msvc_runtime
			DISPLAY_NAME "MSVC runtime libraries"
			DESCRIPTION "May be unchecked if you already have MS Visual C++ or it's redistributable package installed"
			GROUP libs)
	endif (MINGW)
endif (WIN32)

if (INSTALL_SDK)
	cpack_add_component(sdk
		DISPLAY_NAME "SDK"
		DESCRIPTION "Files needed to build custom plugins"
		DISABLED)
endif (INSTALL_SDK)
