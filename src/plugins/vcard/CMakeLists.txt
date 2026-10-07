project(vcard)

set(PLUGIN_NAME "vcard")
set(PLUGIN_DISPLAY_NAME "vCard")

include("vcard.cmake")
include("${CMAKE_SOURCE_DIR}/src/plugins/plugins.cmake")

if(BUILD_TESTING)
    add_executable(protocol_profile_action_policy_tests
        protocolprofileactionpolicy_test.cpp
    )
    target_link_libraries(protocol_profile_action_policy_tests
        Qt6::Core
    )
    target_include_directories(protocol_profile_action_policy_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/src
    )
    set_target_properties(protocol_profile_action_policy_tests PROPERTIES
        EXCLUDE_FROM_ALL ON
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED ON
    )

    add_executable(vcard_dialog_binding_tests
        vcard_dialog_binding_test.cpp
    )
    target_link_libraries(vcard_dialog_binding_tests
        Qt6::Core
    )
    set_target_properties(vcard_dialog_binding_tests PROPERTIES
        EXCLUDE_FROM_ALL ON
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED ON
    )
endif()
