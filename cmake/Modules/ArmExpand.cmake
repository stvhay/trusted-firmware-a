#[=======================================================================[.rst:
ArmExpand
---------

.. default-domain:: cmake

.. command:: arm_expand

Force expansion of variables in a string.

.. code-block:: cmake

    arm_expand(OUTPUT <output> STRING <string> [ATONLY])

This function scans the input string ``<string>`` for CMake-style and @-style
variables and expands them into their current values, writing the result to
``<output>``. If ``ATONLY`` is specified, variable replacement is restricted to
references of the form ``@VAR@``.

.. code-block:: cmake
    :caption: Example usage
    :linenos:

    set(expand-me "@CMAKE_CURRENT_SOURCE_DIR@")

    arm_expand(OUTPUT expanded STRING "${expand-me}")
#]=======================================================================]

include_guard()

include(ArmAssert)

function(arm_expand)
    set(options ATONLY)
    set(single-args OUTPUT STRING)
    set(multi-args)

    cmake_parse_arguments(PARSE_ARGV 0 ARG
        "${options}" "${single-args}" "${multi-args}")

    arm_assert(
        CONDITION DEFINED ARG_OUTPUT
        MESSAGE "No value was given for the `OUTPUT` argument.")

    set(atonly-regex [[@([^@]+)@]])
    set(brace-regex [[\${([^}]+)}]])

    string(REGEX MATCH "${atonly-regex}" match "${ARG_STRING}")

    while(match)
        string(REPLACE "@${CMAKE_MATCH_1}@" "${${CMAKE_MATCH_1}}"
            ARG_STRING "${ARG_STRING}")
        string(REGEX MATCH ${atonly-regex} match "${ARG_STRING}")
    endwhile()

    if(NOT ARG_ATONLY)
        string(REGEX MATCH "${brace-regex}" match "${ARG_STRING}")

        while(match)
            string(REPLACE "\${${CMAKE_MATCH_1}}" "${${CMAKE_MATCH_1}}"
                ARG_STRING "${ARG_STRING}")
            string(REGEX MATCH "${brace-regex}" match "${ARG_STRING}")
        endwhile()
    endif()

    set(${ARG_OUTPUT} "${ARG_STRING}" PARENT_SCOPE)
endfunction()
