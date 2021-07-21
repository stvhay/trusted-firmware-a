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
