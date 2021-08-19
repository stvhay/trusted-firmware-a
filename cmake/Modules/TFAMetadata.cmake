#[=======================================================================[.rst:
TFAMetadata
-----------

.. default-domain:: cmake

Metadata management utilities for TF-A.

TF-A uses a set of JSON-formatted metadata files to manage some of its
configuration data. This module provides a stable CMake interface for retrieving
values from these metadata files.
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

tfa_json_getter(tfa_metadata
    JSON "${global-metadata}")

#
# External global metadata API.
#
