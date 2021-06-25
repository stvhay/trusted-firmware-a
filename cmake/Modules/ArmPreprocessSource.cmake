#[=======================================================================[.rst:
ArmPreprocessSource
-------------------

.. default-domain:: cmake

.. command:: arm_preprocess_source

Preprocess a file.

.. code-block:: cmake

    arm_preprocess_source(TARGET <target> OUTPUT <output>
                          SOURCE <source> LANGUAGE <language>
                          [INHIBIT_LINEMARKERS])

Creates a target ``<target>`` which preprocesses a source file ``<source>`` and
outputs the result to the path ``<output>``. The compiler used to preprocess the
file is determined by ``<language>``; specifically, via the
:variable:`CMAKE_<LANG>_COMPILER <variable:CMAKE_<LANG>_COMPILER>` variable.

To pass preprocessor definitions, include directories or command line options to
the preprocessor, you can apply the following properties to the target
``<target>``:

 - :prop_tgt:`COMPILE_OPTIONS <prop_tgt:COMPILE_OPTIONS>`
 - :prop_tgt:`COMPILE_DEFINITIONS <prop_tgt:COMPILE_DEFINITIONS>`
 - :prop_tgt:`INCLUDE_DIRECTORIES <prop_tgt:INCLUDE_DIRECTORIES>`

For example, if you wish to preprocess a file ``foo.c`` with the preprocessor
definition ``-DFOO=BAR`` you might use:

.. code-block:: cmake
    :caption: Example usage
    :linenos:

    arm_preprocess_source(LANGUAGE C
        TARGET foo SOURCE "foo.c" OUTPUT "foo.c.i")

    set_target_properties(foo
        PROPERTIES COMPILE_DEFINITIONS "FOO=BAR")

The ``INHIBIT_LINEMARKERS`` flag inhibits linemarkers on compilers that produce
them by default. These are often intended by the preprocessor to communicate
source line information to the compiler, but can interfere with tools that do
not expect them.

.. note::

    The created target automatically inherits flags from the
    :variable:`CMAKE_<LANG>_FLAGS <variable:CMAKE_<LANG>_FLAGS>` and
    :variable:`CMAKE_<LANG>_FLAGS_<CONFIG> <variable:CMAKE_<LANG>_FLAGS_<CONFIG>>`
    variables.
#]=======================================================================]

include_guard()

include(ArmAssert)

function(arm_preprocess_source)
    set(options "INHIBIT_LINEMARKERS")
    set(single-args "TARGET;SOURCE;OUTPUT;LANGUAGE")
    set(multi-args "")

    cmake_parse_arguments(PARSE_ARGV 0 ARG
        "${options}" "${single-args}" "${multi-args}")

    arm_assert(
        CONDITION DEFINED ARG_TARGET
        MESSAGE "No value was given for the `TARGET` argument.")

    arm_assert(
        CONDITION DEFINED ARG_SOURCE
        MESSAGE "No value was given for the `SOURCE` argument.")

    arm_assert(
        CONDITION DEFINED ARG_OUTPUT
        MESSAGE "No value was given for the `OUTPUT` argument.")

    arm_assert(
        CONDITION DEFINED ARG_LANGUAGE
        MESSAGE "No value was given for the `LANGUAGE` argument.")

    set(inhibit-linemarkers "${ARG_INHIBIT_LINEMARKERS}")

    #
    # Make the source path absolute so that there are no issues with path
    # resolution during preprocessing.
    #

    cmake_path(ABSOLUTE_PATH ARG_SOURCE NORMALIZE
        BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    cmake_path(ABSOLUTE_PATH ARG_OUTPUT NORMALIZE
        BASE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

    #
    # CMake's toolchain detection logic creates a special variable -
    # `CMAKE_<LANGUAGE>_CREATE_PREPROCESSED_SOURCE` - which describes the
    # command line required to preprocess a source file for that language. For
    # example, here it is for the GNU C preprocessor:
    #
    #     <CMAKE_C_COMPILER> <DEFINES> <INCLUDES> <FLAGS> -E <SOURCE> >
    #     <PREPROCESSED_SOURCE>
    #
    # We do some processing on this variable to convert these
    # bracket-surrounded names to variables we set. For example, `<DEFINES>`
    # is replaced with `@DEFINES@`. We then need to do some string replacement
    # magic to expand that string out to the value of the actual variable.
    #
    # The values for some of these, namely include directories, definitions
    # and other compiler options, come from properties set on the target by
    # the caller. These are typically taken from the target that this
    # preprocessed source file belongs to.
    #

    arm_assert(
        CONDITION DEFINED CMAKE_${ARG_LANGUAGE}_CREATE_PREPROCESSED_SOURCE
        MESSAGE "Unable to determine the preprocessor command line for the "
                "${ARG_LANGUAGE} language. Please report this to the "
                "developer.")

    set(command "${CMAKE_${ARG_LANGUAGE}_CREATE_PREPROCESSED_SOURCE}")

    #
    # Ensure the paths we provide to the command line are in OS-native form. We
    # don't want the compiler complaining that the paths are malformed.
    #

    cmake_path(NATIVE_PATH ARG_SOURCE NORMALIZE SOURCE)
    cmake_path(NATIVE_PATH ARG_OUTPUT NORMALIZE PREPROCESSED_SOURCE)

    #
    # Split up the command into a list.
    #

    separate_arguments(command NATIVE_COMMAND "${command}")

    set(FLAGS "$<TARGET_PROPERTY:${ARG_TARGET},COMPILE_OPTIONS>")
    set(DEFINES "$<TARGET_PROPERTY:${ARG_TARGET},COMPILE_DEFINITIONS>")
    set(INCLUDES "$<TARGET_PROPERTY:${ARG_TARGET},INCLUDE_DIRECTORIES>")

    #
    # Until we come across an exception to the rule, use `-D` for definitions
    # and `-I` for include directories.
    #

    set(DEFINES "$<$<BOOL:${DEFINES}>:-D$<JOIN:${DEFINES},$<SEMICOLON>-D>>")
    set(INCLUDES "$<$<BOOL:${INCLUDES}>:-I$<JOIN:${INCLUDES},$<SEMICOLON>-I>>")

    #
    # Apply compiler-specific behaviours. The default assumption is that we're
    # using a toolchain that behaves like GCC, as many tools attempt to emulate
    # it.
    #

    if(ARG_LANGUAGE STREQUAL "ASM")
        list(APPEND FLAGS "-x" "assembler-with-cpp")
    elseif(language STREQUAL "C")
        list(APPEND FLAGS "-x" "c")
    elseif(language STREQUAL "CXX")
        list(APPEND FLAGS "-x" "c++")
    endif()

    if(ARG_INHIBIT_LINEMARKERS)
        list(APPEND FLAGS "-P")
    endif()

    #
    # Here's the magic part. For every angle bracket-wrapped value in the
    # command, replace it with its at-variable form, then replace it with the
    # value of that variable.
    #

    string(REGEX REPLACE "<([[A-Z_]+)>" [[@\1@]] command "${command}")
    arm_expand(OUTPUT command STRING "${command}" ATONLY)

    #
    # Finally, add the command which generates the preprocessed file.
    #

    add_custom_target(${ARG_TARGET}
        SOURCES "${ARG_SOURCE}"
        DEPENDS "${ARG_OUTPUT}")

    add_custom_command(
        OUTPUT "${ARG_OUTPUT}"
        MAIN_DEPENDENCY "${ARG_SOURCE}"
        COMMAND "${command}"
        VERBATIM COMMAND_EXPAND_LISTS)
endfunction()
