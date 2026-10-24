if (NOT DEFINED EMXEXP OR NOT EXISTS "${EMXEXP}")
    message(FATAL_ERROR "OS/2 export generator not found: ${EMXEXP}")
endif()
if (NOT DEFINED LINK_COMMAND_FILE OR NOT EXISTS "${LINK_COMMAND_FILE}")
    message(FATAL_ERROR "OS/2 target link command not found: ${LINK_COMMAND_FILE}")
endif()
if (NOT DEFINED LINK_WORKING_DIRECTORY OR NOT IS_DIRECTORY "${LINK_WORKING_DIRECTORY}")
    message(FATAL_ERROR "OS/2 target working directory not found: ${LINK_WORKING_DIRECTORY}")
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
        --link-command-file "${LINK_COMMAND_FILE}"
        --working-directory "${LINK_WORKING_DIRECTORY}"
        --output "${OUTPUT_FILE}"
    RESULT_VARIABLE _generator_result)
if (NOT _generator_result EQUAL 0)
    message(FATAL_ERROR "OS/2 utility export generation failed (${_generator_result})")
endif()
