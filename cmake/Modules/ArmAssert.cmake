#[=======================================================================[.rst:
ArmAssert
---------

.. default-domain:: cmake

.. command:: arm_assert

Assert an invariant, and fail the build if the invariant is broken.

.. code-block:: cmake

    arm_assert(CONDITION <condition> [MESSAGE <message>])

This function takes a condition ``<condition>`` in :ref:`Condition Syntax`,
evaluates it, and fails the build with the message ``<message>`` if evaluation
does not yield a truthy result. If no message is provided, the ``<condition>``
is instead printed.

.. code-block:: cmake
    :caption: Example usage (with message)
    :linenos:

    arm_assert(
        CONDITION STACK GREATER_EQUAL 256
        MESSAGE "The stack must be at least 256 bytes.")

    # CMake Error at cmake/Modules/ArmAssert.cmake:42 (message):
    #   The stack must be at least 256 bytes.
    # Call Stack (most recent call first):
    #   CMakeLists.txt:42 (arm_assert)

    # ... and is functionally identical to...

    if(NOT STACK GREATER_EQUAL 256)
        message(FATAL_ERROR "The stack must be at least 256 bytes.")
    endif()

.. code-block:: cmake
    :caption: Example usage (without message)
    :linenos:

    arm_assert(CONDITION STACK GREATER_EQUAL 256)

    # CMake Error at cmake/Modules/ArmAssert.cmake:42 (message):
    #   An assertion was triggered: STACK GREATER_EQUAL 256
    # Call Stack (most recent call first):
    #   CMakeLists.txt:42 (arm_assert)

    # ... and is functionally identical to...

    if(NOT STACK GREATER_EQUAL 256)
        message(FATAL_ERROR "An assertion was triggered: STACK GREATER_EQUAL 256")
    endif()
#]=======================================================================]

include_guard()

function(arm_assert)
    set(options "")
    set(single-args "")
    set(multi-args "CONDITION;MESSAGE")

    cmake_parse_arguments(PARSE_ARGV 0 ARG
        "${options}" "${single-args}" "${multi-args}")

    if(NOT DEFINED ARG_MESSAGE)
        set(ARG_MESSAGE "An assertion was triggered: " ${ARG_CONDITION})
    endif()

    string(REPLACE ";" "" ARG_MESSAGE "${ARG_MESSAGE}")

    if(NOT (${ARG_CONDITION}))
        message(FATAL_ERROR "${ARG_MESSAGE}")
    endif()
endfunction()
