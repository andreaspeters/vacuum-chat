if (NOT DEFINED EMXEXP OR NOT EXISTS "${EMXEXP}")
    message(FATAL_ERROR "OS/2 export generator not found: ${EMXEXP}")
endif()
if (DEFINED NINJA_EXECUTABLE AND DEFINED NINJA_TARGET)
	if (NOT EXISTS "${NINJA_EXECUTABLE}")
		message(FATAL_ERROR "Ninja executable not found: ${NINJA_EXECUTABLE}")
	endif()
	if (NOT DEFINED NINJA_BUILD_DIRECTORY OR
		NOT IS_DIRECTORY "${NINJA_BUILD_DIRECTORY}")
		message(FATAL_ERROR "Ninja build directory not found: ${NINJA_BUILD_DIRECTORY}")
	endif()
	set(_os2_link_input_arguments
		--ninja "${NINJA_EXECUTABLE}"
		--ninja-target "${NINJA_TARGET}"
		--build-directory "${NINJA_BUILD_DIRECTORY}")
else()
	if (NOT DEFINED LINK_COMMAND_FILE OR NOT EXISTS "${LINK_COMMAND_FILE}")
		message(FATAL_ERROR "OS/2 target link command not found: ${LINK_COMMAND_FILE}")
	endif()
	if (NOT DEFINED LINK_WORKING_DIRECTORY OR
		NOT IS_DIRECTORY "${LINK_WORKING_DIRECTORY}")
		message(FATAL_ERROR "OS/2 target working directory not found: ${LINK_WORKING_DIRECTORY}")
	endif()
	set(_os2_link_input_arguments
		--link-command-file "${LINK_COMMAND_FILE}"
		--working-directory "${LINK_WORKING_DIRECTORY}")
endif()
if (NOT DEFINED OUTPUT_FILE)
    message(FATAL_ERROR "OUTPUT_FILE is required for the OS/2 export definition")
endif()

find_program(_os2_python3 python3)
if (NOT _os2_python3)
    message(FATAL_ERROR "OS/2 utility export generation requires host python3")
endif()

execute_process(
    COMMAND "${_os2_python3}"
        "${CMAKE_CURRENT_LIST_DIR}/generate_os2_exports.py"
        --emxexp "${EMXEXP}"
        ${_os2_link_input_arguments}
        --output "${OUTPUT_FILE}"
    RESULT_VARIABLE _generator_result)
if (NOT _generator_result EQUAL 0)
    message(FATAL_ERROR "OS/2 utility export generation failed (${_generator_result})")
endif()
