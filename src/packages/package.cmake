if (WIN32)
	set(CPACK_GENERATOR NSIS)
	#set(CPACK_PACKAGE_EXECUTABLES ${VACUUM_LOADER_NAME} "Vacuum Chat")
	set(CPACK_NSIS_CREATE_ICONS_EXTRA "CreateShortCut '\$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\Vacuum Chat.lnk' '\$INSTDIR\\\\vacuum.exe'")
	set(CPACK_NSIS_CONTACT "https://github.com/andreaspeters/vacuum-chat")
	set(CPACK_PACKAGE_INSTALL_REGISTRY_KEY "VacuumIM")
endif (WIN32)

execute_process(COMMAND svnversion -n "${CMAKE_SOURCE_DIR}"
	OUTPUT_VARIABLE SVNREVISION)

set(CPACK_PACKAGE_NAME "Vacuum Chat")
set(VER_MAJOR 2)
set(VER_MINOR 0)
set(VER_PATCH 0)
set(VERSION "${VER_MAJOR}.${VER_MINOR}.${VER_PATCH}")

if (SVNREVISION MATCHES "^[0-9]+$")
	set(VERSION "${VERSION}-r${SVNREVISION}")
endif (SVNREVISION MATCHES "^[0-9]+$")

set(CPACK_PACKAGE_VERSION_MAJOR ${VER_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${VER_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${VER_PATCH})
set(CPACK_PACKAGE_VERSION "${VER_MAJOR}.${VER_MINOR}.${VER_PATCH}")
set(CPACK_PACKAGE_VENDOR "https://github.com/andreaspeters/vacuum-chat")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/andreaspeters/vacuum-chat")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Vacuum Chat")
set(CPACK_PACKAGE_DESCRIPTION "Vacuum Chat")
set(CPACK_PACKAGE_LICENSE "GPL-3.0-only")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Vacuum-IM")
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/COPYING")
set(CPACK_RESOURCE_FILE_WELCOME "${CMAKE_SOURCE_DIR}/CHANGELOG")
set(CPACK_PACKAGE_FILE_NAME "vacuum-im-${VERSION}-installer")

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	option(CPACK_BINARY_DEB "Enable to build Debian packages" OFF)
	option(CPACK_BINARY_ARCH "Enable to build Arch Linux packages" OFF)
	set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/packages")
	set(CPACK_MONOLITHIC_INSTALL ON)
	set(CPACK_DEB_COMPONENT_INSTALL OFF)
	set(CPACK_DEBIAN_PACKAGE_NAME "vacuum-im")
	set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Vacuum Chat maintainers")
	set(CPACK_DEBIAN_PACKAGE_SECTION "net")
	set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
	set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")

	string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _cpack_processor)
	if(_cpack_processor MATCHES "^(x86_64|amd64)$")
		set(_cpack_arch "x86_64")
		set(_cpack_deb_arch "amd64")
	elseif(_cpack_processor MATCHES "^(aarch64|arm64)$")
		set(_cpack_arch "aarch64")
		set(_cpack_deb_arch "arm64")
	elseif(_cpack_processor MATCHES "^(i[3-6]86|x86)$")
		set(_cpack_arch "i686")
		set(_cpack_deb_arch "i386")
	elseif(_cpack_processor MATCHES "^armv7")
		set(_cpack_arch "armv7h")
		set(_cpack_deb_arch "armhf")
	elseif(_cpack_processor MATCHES "^ppc64le$")
		set(_cpack_arch "powerpc64le")
		set(_cpack_deb_arch "ppc64el")
	else()
		set(_cpack_arch "${_cpack_processor}")
		set(_cpack_deb_arch "${_cpack_processor}")
	endif()
	set(CPACK_ARCHLINUX_ARCHITECTURE "${_cpack_arch}")
	set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "${_cpack_deb_arch}")
	set(CPACK_ARCHLINUX_PACKAGE_NAME "vacuum-im")
	set(CPACK_ARCHLINUX_PACKAGE_REL "1")
	set(CPACK_ARCHLINUX_PACKAGE_LICENSE "GPL3")
	set(CPACK_ARCHLINUX_PACKAGE_DEPENDS "glibc;gcc-libs;qt6-base;qt6-connectivity;qt6-serialport;openssl;libidn2;zlib" CACHE STRING "Arch Linux runtime package dependencies")

	find_program(CPACK_ARCHLINUX_TAR_EXECUTABLE NAMES bsdtar)
	find_program(CPACK_ARCHLINUX_ZSTD_EXECUTABLE NAMES zstd)
	set(CPACK_EXTERNAL_ENABLE_STAGING ON)
	set(CPACK_EXTERNAL_PACKAGE_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/CPackArchLinux.cmake")
	set(CPACK_EXTERNAL_BUILD_ARCH OFF)

	if(CPACK_BINARY_ARCH)
		if(NOT CMAKE_VERSION VERSION_GREATER_EQUAL "3.19")
			message(FATAL_ERROR "CPACK_BINARY_ARCH requires CMake 3.19 or newer")
		endif()
		if(NOT CPACK_ARCHLINUX_TAR_EXECUTABLE OR NOT CPACK_ARCHLINUX_ZSTD_EXECUTABLE)
			message(FATAL_ERROR "CPACK_BINARY_ARCH requires bsdtar and zstd")
		endif()
		set(CPACK_EXTERNAL_BUILD_ARCH ON)
	endif()

	if(NOT DEFINED CPACK_GENERATOR OR CPACK_GENERATOR STREQUAL "")
		set(_cpack_generators)
		if(CPACK_BINARY_DEB)
			list(APPEND _cpack_generators DEB)
		endif()
		if(CPACK_BINARY_ARCH)
			list(APPEND _cpack_generators External)
		endif()
		if(NOT _cpack_generators)
			list(APPEND _cpack_generators DEB)
			if(CPACK_ARCHLINUX_TAR_EXECUTABLE AND CPACK_ARCHLINUX_ZSTD_EXECUTABLE AND CMAKE_VERSION VERSION_GREATER_EQUAL "3.19")
				list(APPEND _cpack_generators External)
				set(CPACK_EXTERNAL_BUILD_ARCH ON)
			endif()
		endif()
		set(CPACK_GENERATOR "${_cpack_generators}")
	endif()

	if(CPACK_GENERATOR MATCHES "(^|;)External(;|$)" AND NOT CPACK_BINARY_ARCH)
		# Preserve the existing default: when no format selector is enabled, External means Arch.
		set(CPACK_EXTERNAL_BUILD_ARCH ON)
	endif()
	if(CPACK_BINARY_ARCH)
		if(NOT CPACK_GENERATOR MATCHES "(^|;)External(;|$)")
			message(FATAL_ERROR "CPACK_BINARY_ARCH is ON but CPACK_GENERATOR does not include External")
		endif()
	endif()

	# Use explicit runtime dependencies for reliable packages across host environments.
	# dpkg-shlibdeps may fail for binaries built with private/Nix RUNPATHs.
	set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
	set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6, libgcc-s1, libstdc++6, libqt6core6, libqt6dbus6, libqt6gui6, libqt6widgets6, libqt6network6, libqt6xml6, libqt6sql6, libqt6serialport6, libqt6bluetooth6, libssl3, libidn2-0, zlib1g")
endif()

include(CPack)

add_custom_target(packages
	COMMAND "${CMAKE_CPACK_COMMAND}" --config "${CMAKE_BINARY_DIR}/CPackConfig.cmake"
	WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
	COMMENT "Generate the configured package(s)"
	VERBATIM)

cpack_add_component_group(core
	DISPLAY_NAME "Core components"
	DESCRIPTION "Loader and utils library")
cpack_add_component_group(essential_plugins
	DISPLAY_NAME "Essential plugins"
	DESCRIPTION "Minimal set of plugins required for basic messaging and presence functionality")
cpack_add_component_group(optional_plugins
	DISPLAY_NAME "Optional plugins"
	DESCRIPTION "Plugins that not required, but still useful")
cpack_add_component_group(translations
	DISPLAY_NAME "Translations"
	DESCRIPTION "Translations to various languages")
foreach(LANG ${LOCALIZED_LANGS})
	lang_display_name(LANG_NAME ${LANG})
	cpack_add_component_group(${LANG}_translation
		DISPLAY_NAME "${LANG_NAME} translation"
		DESCRIPTION "${LANG_NAME} translation"
		PARENT_GROUP translations)
endforeach(LANG)
