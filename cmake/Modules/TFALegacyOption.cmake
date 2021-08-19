#[=======================================================================[.rst:
TFALegacyOption
---------------

.. default-domain:: cmake

.. command:: tfa_legacy_option

Warn the user about a legacy build system configuration option, and offer
alternatives understood by the CMake build system.

.. code-block:: cmake

    tfa_legacy_option(OLD <old>... NEW <new>...)

Generate a warning when any of the variables specified by ``<old>...`` are
defined which directs the user to an alternative set of configuration options
defined by ``<new>...``.

.. code-block:: cmake
    :caption: Usage example
    :linenos:

    # cmake -DOLD_OPTION_B=ON ...

    tfa_legacy_option(
        OLD OLD_OPTION_A OLD_OPTION_B
        NEW NEW_OPTION_C NEW_OPTION_D)

    # The following configuration option relates to Trusted Firmware-A's legacy
    # build system:
    #
    #  - OLD_OPTION_B
    #
    # This option has been superceded.  Please see the documentation for the
    # following configuration options:
    #
    #  - NEW_OPTION_C
    #  - NEW_OPTION_D
#]=======================================================================]

include_guard()

function(tfa_legacy_option)
    set(options "")
    set(single-args "")
    set(multi-args "OLD;NEW")

    cmake_parse_arguments(
        ARG "${options}" "${single-args}" "${multi-args}" ${ARGN})

    list(LENGTH ARG_NEW count)

    foreach(old IN LISTS ARG_OLD)
        if(DEFINED ${old})
            if(ARG_NEW)
                string(CONCAT message
                    "The following configuration option relates to Trusted "
                    "Firmware-A's legacy build system:\n"

                    " - ${old}\n"

                    "This option has been superceded. Please see the "
                    "documentation for the following configuration options:")

                foreach(new IN LISTS ARG_NEW)
                    string(APPEND message "\n - ${new}")
                endforeach()
            else()
                string(CONCAT message
                    "The following configuration option relates to Trusted "
                    "Firmware-A's legacy build system, and is no longer "
                    "necessary:\n"

                    " - ${old}")
            endif()

            message(WARNING "${message}")
        endif()
    endforeach()
endfunction()
