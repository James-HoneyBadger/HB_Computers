# SPDX-License-Identifier: MIT
#
# The warning policy, in one place.
#
# Every target in this tree is compiled with the same warnings and the same
# warnings-as-errors switch, so an edit to the policy is one edit.

function(hb_enable_warnings target)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
    if(HB_WERROR)
        target_compile_options(${target} PRIVATE -Werror)
    endif()
endfunction()

# A suite is an executable, the warnings, and a CTest entry: the three things
# every test in this tree is. Extra libraries follow the name.
#
#     hb_add_suite(hb2_tests hb::hb2)
function(hb_add_suite suite)
    add_executable(${suite} tests/${suite}.cpp)
    target_include_directories(${suite} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    if(ARGC GREATER 1)
        target_link_libraries(${suite} PRIVATE ${ARGN})
    endif()
    hb_enable_warnings(${suite})
    add_test(NAME ${suite} COMMAND ${suite})
endfunction()
