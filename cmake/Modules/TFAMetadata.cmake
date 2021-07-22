#[=======================================================================[.rst:
TFAMetadata
-----------

.. default-domain:: cmake

Metadata management utilities for TF-A.

TF-A uses a set of JSON-formatted metadata files to manage some of its
configuration data. This module provides a stable CMake interface for retrieving
values from these metadata files.

.. command:: tfa_platforms

.. code-block:: cmake

    tfa_platforms(<out-var>)

Return the list of supported platforms in ``<out-var>``.

.. command:: tfa_platform_path

.. code-block:: cmake

    tfa_platform_path(<out-var> PLATFORM <platform>)

Return the path to the platform ``<platform>`` in ``<out-var>``.
#]=======================================================================]

include_guard()

include(ArmAssert)
include(ArmExpand)

include(TFAJsonUtilities)

#
# Read the global metadata file. This is a JSON-formatted file that contains
# information about the contents of the repository that are relevant to the
# build system.
#

arm_assert(CONDITION EXISTS "${CMAKE_SOURCE_DIR}/metadata.json")

file(READ "${CMAKE_SOURCE_DIR}/metadata.json" global-metadata)
arm_expand(OUTPUT global-metadata STRING "${global-metadata}")

#
# Allow the user to provide their own platform list metadata. This allows
# developers to use out-of-tree platforms (platforms that live outside of this
# repository). The platforms list given by this file is superimposed onto the
# global metadata file.
#

arm_config_option(
    NAME TFA_METADATA_PLATFORMS_PATH ADVANCED
    HELP "Path to an alternative platforms metadata file."
    TYPE FILEPATH)

if(TFA_METADATA_PLATFORMS_PATH)
    cmake_path(GET TFA_METADATA_PLATFORMS_PATH
        PARENT_PATH TFA_METADATA_PLATFORMS_DIR)

    arm_assert(
        CONDITION EXISTS "${TFA_METADATA_PLATFORMS_PATH}"
        MESSAGE "The path to the platforms metadata file "
                "(`TFA_METADATA_PLATFORMS_PATH`) does not exist:\n"

                "${TFA_METADATA_PLATFORMS_PATH}")

    file(READ "${TFA_METADATA_PLATFORMS_PATH}" platforms-metadata)
    arm_expand(OUTPUT platforms-metadata STRING "${platforms-metadata}")

    tfa_json_get(platforms JSON "${platforms-metadata}" DECODE MEMBERS)

    foreach(platform IN LISTS platforms)
        #
        # Fix up relative paths in the platforms metadata JSON file so that any
        # relative paths are relative to the metadata file, rather than to the
        # current source directory.
        #

        tfa_json_get(platform-path JSON "${platforms-metadata}"
            PATH "${platform}" DECODE STRING)

        cmake_path(ABSOLUTE_PATH platform-path
            BASE_DIRECTORY "${TFA_METADATA_PLATFORMS_DIR}")

        tfa_json_encode_string(platform-path "${platform-path}")
        string(JSON platforms-metadata SET "${platforms-metadata}"
            "${platform}" "${platform-path}")
    endforeach()

    tfa_json_merge(global-metadata
        BOTTOM "${global-metadata}"
        BOTTOM_PATH "platforms"
        TOP "${platforms-metadata}")
endif()

#
# Internal global metadata API.
#

macro(tfa_metadata_platforms_platform_postprocess)
    #
    # Ensure relative paths are made relative to the current source directory,
    # which should be the top-level project directory. The initial JSON string
    # value is passed via the `value` variable.
    #

    tfa_json_decode_string(platform-path "${value}")

    cmake_path(ABSOLUTE_PATH platform-path
        BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

    tfa_json_encode_string(value "${platform-path}")
endmacro()

tfa_json_getter(tfa_metadata
    JSON "${global-metadata}")

tfa_json_getter(tfa_metadata_platforms
    JSON "${global-metadata}" PARENT tfa_metadata
    PATH "platforms")

tfa_json_getter(tfa_metadata_platforms_platform
    JSON "${global-metadata}" PARENT tfa_metadata_platforms
    PATH "@PLATFORM@" ARGUMENTS PLATFORM
    POSTPROCESS tfa_metadata_platforms_platform_postprocess
    ERROR_MESSAGE "No such platform: @PLATFORM@.")

#
# External global metadata API.
#

tfa_json_getter(tfa_platforms
    JSON "${global-metadata}" PARENT tfa_metadata_platforms
    DECODE MEMBERS)

tfa_json_getter(tfa_platform_path
    JSON "${global-metadata}" PARENT tfa_metadata_platforms_platform
    DECODE STRING)
