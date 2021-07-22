TFA_METADATA_PLATFORMS_PATH
===========================

.. default-domain:: cmake

.. variable:: TFA_METADATA_PLATFORMS_PATH

Path to an out-of-tree platforms metadata file. This file may contain
describe additional platforms not distributed with |TF-A|.

This file follows the ``/platforms`` sub-schema of global metadata file schema,
which can be found :download:`here <../../../../../schemas/metadata.schema.json>`.

For example, if you have two platforms - ``PlatformA`` and ``PlatformB`` - your
platforms metadata file might look a little like the following:

.. code-block:: json

    {
      "PlatformA": "path/to/platform-a",
      "PlatformB": "path/to/platform-b"
    }

Where ``path/to/platform-x`` represents the path to the platform source
directory relative to the platforms metadata file.

--------------

*Copyright (c) 2021, Arm Limited and Contributors. All rights reserved.*
