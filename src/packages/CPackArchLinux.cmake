# CPack External hook that wraps the staged install tree as an Arch package.
# Arch package creation needs bsdtar and zstd, but does not require makepkg/fakeroot.

if(NOT DEFINED CPACK_TEMPORARY_DIRECTORY OR NOT IS_DIRECTORY "${CPACK_TEMPORARY_DIRECTORY}")
	message(FATAL_ERROR "CPack External staging directory is unavailable")
endif()
if(NOT CPACK_ARCHLINUX_TAR_EXECUTABLE OR NOT EXISTS "${CPACK_ARCHLINUX_TAR_EXECUTABLE}")
	message(FATAL_ERROR "Arch packaging requires bsdtar (set CPACK_ARCHLINUX_TAR_EXECUTABLE)")
endif()
if(NOT CPACK_ARCHLINUX_ZSTD_EXECUTABLE OR NOT EXISTS "${CPACK_ARCHLINUX_ZSTD_EXECUTABLE}")
	message(FATAL_ERROR "Arch packaging requires zstd (set CPACK_ARCHLINUX_ZSTD_EXECUTABLE)")
endif()

set(_stage "${CPACK_TEMPORARY_DIRECTORY}")
set(_pkgname "${CPACK_ARCHLINUX_PACKAGE_NAME}")
set(_pkgrel "${CPACK_ARCHLINUX_PACKAGE_REL}")
set(_architecture "${CPACK_ARCHLINUX_ARCHITECTURE}")
set(_homepage "${CPACK_PACKAGE_HOMEPAGE_URL}")
set(_description "${CPACK_PACKAGE_DESCRIPTION_SUMMARY}")
set(_license "${CPACK_ARCHLINUX_PACKAGE_LICENSE}")

if(NOT _pkgname OR NOT _pkgrel OR NOT _architecture OR NOT CPACK_PACKAGE_VERSION)
	message(FATAL_ERROR "Arch package name, release, architecture, and version must be configured")
endif()
if(NOT _homepage)
	message(FATAL_ERROR "CPACK_PACKAGE_HOMEPAGE_URL is required for Arch packaging")
endif()

# Arch pkgver values cannot contain hyphens; use underscores for CPack revision suffixes.
set(_pkgver "${CPACK_PACKAGE_VERSION}")
string(REPLACE "-" "_" _pkgver "${_pkgver}")
set(_package_filename "${_pkgname}-${_pkgver}-${_pkgrel}-${_architecture}.pkg.tar.zst")

set(_work_dir "${CPACK_TOPLEVEL_DIRECTORY}/archlinux")
file(MAKE_DIRECTORY "${_work_dir}")
set(_package_root "${_work_dir}/root")
file(REMOVE_RECURSE "${_package_root}")
file(MAKE_DIRECTORY "${_package_root}/usr")
file(GLOB _stage_entries RELATIVE "${_stage}" "${_stage}/*")
if(NOT _stage_entries)
	message(FATAL_ERROR "CPack staging directory contains no package files")
endif()
foreach(_entry IN LISTS _stage_entries)
	file(COPY "${_stage}/${_entry}" DESTINATION "${_package_root}/usr")
endforeach()

file(GLOB_RECURSE _payload_files LIST_DIRECTORIES FALSE "${_package_root}/usr/*")
set(_installed_size 0)
foreach(_file IN LISTS _payload_files)
	file(SIZE "${_file}" _file_size)
	math(EXPR _installed_size "${_installed_size} + ${_file_size}")
endforeach()
string(TIMESTAMP _builddate "%s" UTC)

set(_pkginfo "pkgname = ${_pkgname}\n")
string(APPEND _pkginfo "pkgbase = ${_pkgname}\n")
string(APPEND _pkginfo "pkgver = ${_pkgver}-${_pkgrel}\n")
string(APPEND _pkginfo "pkgdesc = ${_description}\n")
string(APPEND _pkginfo "url = ${_homepage}\n")
string(APPEND _pkginfo "builddate = ${_builddate}\n")
string(APPEND _pkginfo "packager = Vacuum Chat maintainers\n")
string(APPEND _pkginfo "size = ${_installed_size}\n")
string(APPEND _pkginfo "arch = ${_architecture}\n")
string(APPEND _pkginfo "license = ${_license}\n")
set(_dependencies ${CPACK_ARCHLINUX_PACKAGE_DEPENDS})
list(REMOVE_DUPLICATES _dependencies)
foreach(_dependency IN LISTS _dependencies)
	string(STRIP "${_dependency}" _dependency)
	if(_dependency)
		string(APPEND _pkginfo "depend = ${_dependency}\n")
	endif()
endforeach()
file(WRITE "${_package_root}/.PKGINFO" "${_pkginfo}")

set(_tar_file "${_work_dir}/${_pkgname}-${_pkgver}-${_pkgrel}-${_architecture}.pkg.tar")
set(_compressed_file "${_work_dir}/${_package_filename}")
set(_staged_package "${_stage}/${_package_filename}")
set(_archive_entries .PKGINFO usr)

execute_process(
	COMMAND "${CPACK_ARCHLINUX_TAR_EXECUTABLE}"
		--format=gnutar --numeric-owner --uid 0 --gid 0 --uname root --gname root
		-cf "${_tar_file}" -C "${_package_root}" ${_archive_entries}
	RESULT_VARIABLE _tar_result
	ERROR_VARIABLE _tar_error)
if(NOT "${_tar_result}" STREQUAL "0")
	message(FATAL_ERROR "bsdtar failed (${_tar_result}): ${_tar_error}")
endif()

execute_process(
	COMMAND "${CPACK_ARCHLINUX_ZSTD_EXECUTABLE}" -q -f -19 -o "${_compressed_file}" "${_tar_file}"
	RESULT_VARIABLE _zstd_result
	ERROR_VARIABLE _zstd_error)
if(NOT "${_zstd_result}" STREQUAL "0")
	message(FATAL_ERROR "zstd failed (${_zstd_result}): ${_zstd_error}")
endif()
file(REMOVE "${_tar_file}")

execute_process(
	COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_compressed_file}" "${_staged_package}"
	RESULT_VARIABLE _copy_result
	ERROR_VARIABLE _copy_error)
if(NOT "${_copy_result}" STREQUAL "0")
	message(FATAL_ERROR "Unable to expose Arch package to CPack (${_copy_result}): ${_copy_error}")
endif()

set(CPACK_EXTERNAL_BUILT_PACKAGES "${_staged_package}")
