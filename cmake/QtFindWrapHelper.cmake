# Compatibility helper for the incomplete Qt6-OS/2 CMake package.
# Qt's FindWrap*.cmake files call this helper, but the package omits it.
include(CMakeParseArguments)

function(qt_find_package_system_or_bundled prefix)
    set(options)
    set(oneValueArgs
        FRIENDLY_PACKAGE_NAME
        WRAP_PACKAGE_TARGET
        WRAP_PACKAGE_FOUND_VAR_NAME
        BUNDLED_PACKAGE_NAME
        BUNDLED_PACKAGE_TARGET
        SYSTEM_PACKAGE_NAME
        SYSTEM_PACKAGE_TARGET)
    cmake_parse_arguments(QT_WRAP "" "${oneValueArgs}" "" ${ARGN})

    if (QT_WRAP_BUNDLED_PACKAGE_TARGET AND TARGET ${QT_WRAP_BUNDLED_PACKAGE_TARGET})
        if (QT_WRAP_WRAP_PACKAGE_TARGET AND NOT TARGET ${QT_WRAP_WRAP_PACKAGE_TARGET})
            add_library(${QT_WRAP_WRAP_PACKAGE_TARGET} INTERFACE IMPORTED)
            target_link_libraries(${QT_WRAP_WRAP_PACKAGE_TARGET} INTERFACE
                ${QT_WRAP_BUNDLED_PACKAGE_TARGET})
        endif()
        if (QT_WRAP_WRAP_PACKAGE_FOUND_VAR_NAME)
            set(${QT_WRAP_WRAP_PACKAGE_FOUND_VAR_NAME} TRUE PARENT_SCOPE)
        endif()
    elseif (QT_WRAP_SYSTEM_PACKAGE_TARGET AND TARGET ${QT_WRAP_SYSTEM_PACKAGE_TARGET})
        if (QT_WRAP_WRAP_PACKAGE_TARGET AND NOT TARGET ${QT_WRAP_WRAP_PACKAGE_TARGET})
            add_library(${QT_WRAP_WRAP_PACKAGE_TARGET} INTERFACE IMPORTED)
            target_link_libraries(${QT_WRAP_WRAP_PACKAGE_TARGET} INTERFACE
                ${QT_WRAP_SYSTEM_PACKAGE_TARGET})
        endif()
        if (QT_WRAP_WRAP_PACKAGE_FOUND_VAR_NAME)
            set(${QT_WRAP_WRAP_PACKAGE_FOUND_VAR_NAME} TRUE PARENT_SCOPE)
        endif()
    else()
        if (QT_WRAP_WRAP_PACKAGE_FOUND_VAR_NAME)
            set(${QT_WRAP_WRAP_PACKAGE_FOUND_VAR_NAME} FALSE PARENT_SCOPE)
        endif()
    endif()
endfunction()
