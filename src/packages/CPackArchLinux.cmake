# Custom CPack script for Arch Linux .pkg.tar.zst package generation
# This script is called by CPack's External generator

# Verify prerequisites
if(NOT DEFINED ENV{MAKEPKG})
    message(FATAL_ERROR "makepkg not found - required for Arch Linux packaging")
endif()

if(NOT DEFINED ENV{PACMAN})
    message(FATAL_ERROR "pacman not found - required for Arch Linux packaging")
endif()

# Set up variables for package creation
set(PKG_NAME "${CPACK_PACKAGE_NAME}")
set(PKG_VERSION "${CPACK_PACKAGE_VERSION}")

# Create the package directory structure in CPACK_TEMPORARY_DIRECTORY
set(PKG_ROOT "${CPACK_TEMPORARY_DIRECTORY}/pkg")
file(MAKE_DIRECTORY ${PKG_ROOT})

# Copy the installed files into the package structure
# This uses what was already installed by CPack's component system

# Determine where CPACK_INSTALL_PREFIX is set, fallback to default if not set
if(NOT DEFINED CPACK_INSTALL_PREFIX)
    set(CPACK_INSTALL_PREFIX "/usr")
endif()

# We're assuming CPack has already installed the files in temporary directory
# Create PKGBUILD file for makepkg to use
set(PKGBUILD_FILE "${CPACK_TEMPORARY_DIRECTORY}/PKGBUILD")

file(WRITE ${PKGBUILD_FILE} "pkgname=${PKG_NAME}\n")
file(APPEND ${PKGBUILD_FILE} "pkgver=${PKG_VERSION}\n")
file(APPEND ${PKGBUILD_FILE} "pkgrel=1\n")
file(APPEND ${PKGBUILD_FILE} "pkgdesc=\"${CPACK_PACKAGE_DESCRIPTION}\"\n")
file(APPEND ${PKGBUILD_FILE} "url=\"${CPACK_PACKAGE_HOMEPAGE_URL}\"\n")
file(APPEND ${PKGBUILD_FILE} "license=(\"${CPACK_PACKAGE_LICENSE}\")\n")
file(APPEND ${PKGBUILD_FILE} "arch=('any')\n")

# Set dependencies from the installed components (this is minimal since we have no runtime deps specified)
if(DEFINED CPACK_EXTERNAL_PACKAGE_SCRIPT_DEPENDS)
    file(APPEND ${PKGBUILD_FILE} "depends=(${CPACK_EXTERNAL_PACKAGE_SCRIPT_DEPENDS})\n")
else()
    file(APPEND ${PKGBUILD_FILE} "depends=()\n")
endif()

file(APPEND ${PKGBUILD_FILE} "source=()\n")
file(APPEND ${PKGBUILD_FILE} "noextract=()\n")
file(APPEND ${PKGBUILD_FILE} "md5sums=('0')\n\n")

file(APPEND ${PKGBUILD_FILE} "package() {\n")
file(APPEND ${PKGBUILD_FILE} "  cd \"${CPACK_TEMPORARY_DIRECTORY}\"\n")
file(APPEND ${PKGBUILD_FILE} "  cp -a pkg/* \"\$pkgdir/\"\n")
file(APPEND ${PKGBUILD_FILE} "}\n")

# Run makepkg to create the package
execute_process(COMMAND makepkg -f --noconfirm
                WORKING_DIRECTORY ${CPACK_TEMPORARY_DIRECTORY}
                RESULT_VARIABLE makepkg_result)

if(NOT makepkg_result EQUAL 0)
    message(FATAL_ERROR "makepkg failed with exit code ${makepkg_result}")
endif()

# Rename the package to match expected CPack naming convention
set(ARCH_PKG_NAME "${PKG_NAME}-${PKG_VERSION}-1-x86_64.pkg.tar.zst")

# Ensure the output package file is located where CPack expects it in CPACK_EXTERNAL_BUILT_PACKAGES 
file(GLOB ARCH_PKG_FILE "${CPACK_TEMPORARY_DIRECTORY}/*.pkg.tar.zst")
if(ARCH_PKG_FILE)
    # Rename to standard format
    execute_process(COMMAND mv ${ARCH_PKG_FILE} ${CPACK_TEMPORARY_DIRECTORY}/${ARCH_PKG_NAME}
                    RESULT_VARIABLE rename_result)
    
    if(NOT rename_result EQUAL 0)
        message(FATAL_ERROR "Failed to rename package file")
    endif()
    
    # Set the built package location for CPack
    set(CPACK_EXTERNAL_BUILT_PACKAGES ${CPACK_TEMPORARY_DIRECTORY}/${ARCH_PKG_NAME})
else()
    message(FATAL_ERROR "Arch package was not created by makepkg")
endif()