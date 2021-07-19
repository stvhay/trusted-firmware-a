#[=======================================================================[.rst:
ArmTargetLinkerScript
---------------------

.. default-domain:: cmake

.. command:: arm_target_linker_script

Set the linker script for a target.

.. code-block:: cmake

    arm_target_linker_script(
        TARGET <target> SCRIPT <script>
        [PREPROCESSOR SUBTARGET <subtarget> LANGUAGE <language>])

Sets the linker script for the target ``<target>`` to the script ``<script>``,
which is optionally first preprocessed with the preprocessor for the language
given by ``<language>``, which creates the target ``<subtarget>``.

When preprocessing, the following properties are automatically inherited from
the target ``<target>`` and may also be set on the sub-target ``<subtarget>`` in
order to pass additional information to the preprocessor:

 - :prop_tgt:`COMPILE_OPTIONS <prop_tgt:COMPILE_OPTIONS>`
 - :prop_tgt:`COMPILE_DEFINITIONS <prop_tgt:COMPILE_DEFINITIONS>`
 - :prop_tgt:`INCLUDE_DIRECTORIES <prop_tgt:INCLUDE_DIRECTORIES>`

Additionally, the linker script automatically inherits flags from both
:variable:`CMAKE_<LANG>_FLAGS <variable:CMAKE_<LANG>_FLAGS>` and
:variable:`CMAKE_<LANG>_FLAGS_<CONFIG> <variable:CMAKE_<LANG>_FLAGS_<CONFIG>>`.

.. code-block:: cmake
    :caption: Usage example
    :linenos:

    add_executable(my-executable "main.c")

    arm_target_linker_script(
        TARGET my-executable SCRIPT "linker.ld"
        PREPROCESSOR TARGET my-executable-lds LANGUAGE C)

    set_property(
        TARGET my-executable-lds APPEND
        PROPERTY COMPILE_DEFINITIONS "LINKER=1")
#]=======================================================================]

include_guard()

include(ArmAssert)
include(ArmPreprocessSource)

function(arm_target_linker_script)
    set(options "")
    set(single-args "TARGET;SCRIPT")
    set(multi-args "PREPROCESSOR")

    cmake_parse_arguments(PARSE_ARGV 0 ARG
        "${options}" "${single-args}" "${multi-args}")

    arm_assert(
        CONDITION DEFINED ARG_TARGET
        MESSAGE "No value was given for the `TARGET` argument.")

    arm_assert(
        CONDITION DEFINED ARG_SCRIPT
        MESSAGE "No value was given for the `SCRIPT` argument.")

    cmake_path(ABSOLUTE_PATH ARG_SCRIPT NORMALIZE
        BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

    if(DEFINED ARG_PREPROCESSOR)
        _arm_target_linker_script_preprocess(${ARG_PREPROCESSOR}
            TARGET "${ARG_TARGET}" SCRIPT "${ARG_SCRIPT}"
            OUTPUT ARG_SCRIPT)
    endif()

    get_target_property(language "${ARG_TARGET}" LINKER_LANGUAGE)

    if(CMAKE_${language}_COMPILER_ID STREQUAL "ARMClang")
        target_link_options("${ARG_TARGET}"
            PUBLIC "LINKER:--scatter" "LINKER:${ARG_SCRIPT}")
    else()
        target_link_options("${ARG_TARGET}"
            PUBLIC "LINKER:-T" "LINKER:${ARG_SCRIPT}")
    endif()
endfunction()

function(_arm_target_linker_script_preprocess)
    set(options "")
    set(single-args "TARGET;SCRIPT;OUTPUT;SUBTARGET;LANGUAGE")

    cmake_parse_arguments(PARSE_ARGV 0 ARG
        "${options}" "${single-args}" "${multi-args}")

    arm_assert(
        CONDITION (DEFINED ARG_SUBTARGET) AND
                  (DEFINED ARG_LANGUAGE)
        MESSAGE "The preprocessor `SUBTARGET` and `LANGUAGE` arguments must "
                "both be provided when preprocessing.")

    _arm_target_linker_script_preprocess_path(
        TARGET "${ARG_SUBTARGET}" SCRIPT "${ARG_SCRIPT}"
        OUTPUT path)

    arm_preprocess_source(
        TARGET "${ARG_SUBTARGET}" LANGUAGE "${ARG_LANGUAGE}"
        SOURCE "${ARG_SCRIPT}" OUTPUT "${path}"
        INHIBIT_LINEMARKERS)

    set(compile-options "$<TARGET_PROPERTY:${ARG_TARGET},COMPILE_OPTIONS>")
    set(compile-definitions "$<TARGET_PROPERTY:${ARG_TARGET},COMPILE_DEFINITIONS>")
    set(include-directories "$<TARGET_PROPERTY:${ARG_TARGET},INCLUDE_DIRECTORIES>")

    foreach(config IN LISTS CMAKE_BUILD_TYPE CMAKE_CONFIGURATION_TYPES)
        string(TOUPPER "${config}" config)

        separate_arguments(config-compile-options
            NATIVE_COMMAND "${CMAKE_${preprocessor-language}_FLAGS_${config}}")
        list(PREPEND compile-options
            "$<$<CONFIG:${config}>:${config-compile-options}>")
    endforeach()

    separate_arguments(global-compile-options
        NATIVE_COMMAND "${CMAKE_${preprocessor-language}_FLAGS}")
    list(PREPEND compile-options "${global-compile-options}")

    set_target_properties("${ARG_SUBTARGET}"
        PROPERTIES COMPILE_OPTIONS "${compile-options}"
                   COMPILE_DEFINITIONS "${compile-definitions}"
                   INCLUDE_DIRECTORIES "${include-directories}")

    add_dependencies(${ARG_TARGET} "${ARG_SUBTARGET}")

    set(${ARG_OUTPUT} "${path}" PARENT_SCOPE)
endfunction()

function(_arm_target_linker_script_preprocess_path result target script)
    set(options "")
    set(single-args "OUTPUT;TARGET;SCRIPT")
    set(multi-args "")

    cmake_parse_arguments(PARSE_ARGV 0 ARG
        "${options}" "${single-args}" "${multi-args}")

    #
    # Figure out where we're going to place our preprocessed file. This depends
    # on whether we're using a multi-config generator or not:
    #
    # - Single-config: CMakeFiles/${subtarget}.dir
    # - Multi-config: CMakeFiles/${subtarget}.dir/$<CONFIG>
    #

    get_property(multi-config GLOBAL
        PROPERTY GENERATOR_IS_MULTI_CONFIG)

    set(path "${CMAKE_CURRENT_BINARY_DIR}")

    cmake_path(APPEND_STRING path "${CMAKE_FILES_DIRECTORY}")
    cmake_path(APPEND path "${ARG_TARGET}.dir")

    if(multi-config)
        cmake_path(APPEND path "$<CONFIG>")
    endif()

    #
    # Try to mirror the behaviour of CMake when deciding the relativized path
    # for the preprocessed file. If the source file is a child of the current
    # source directory we use its path relative to that, but otherwise we take
    # its relative path part. As an example:
    #
    # - ${CMAKE_CURRENT_SOURCE_DIR}/foo/bar.c -> foo/bar.c.i
    # - C:/foo/bar.c -> foo/bar.c.i
    #

    cmake_path(IS_PREFIX CMAKE_CURRENT_SOURCE_DIR "${ARG_SCRIPT}" is-child)

    if(is-child)
        cmake_path(RELATIVE_PATH ARG_SCRIPT OUTPUT_VARIABLE relative-script)
    else()
        cmake_path(GET ARG_SCRIPT RELATIVE_PART relative-script)
    endif()

    cmake_path(APPEND path "${relative-script}.i")

    set(${ARG_OUTPUT} "${path}" PARENT_SCOPE)
endfunction()
