CMAKE_BUILD_TYPE
================

.. default-domain:: cmake

.. variable:: CMAKE_BUILD_TYPE

Specifies the build type on single-configuration generators. See
:variable:`CMAKE_BUILD_TYPE <variable:CMAKE_BUILD_TYPE>`.

Supported values are as follows:

+--------------------+---------+---------------+------------+
| Value              | Profile | Optimizations | Assertions |
+====================+=========+===============+============+
| ``Debug``          | Debug   | None          | Enabled    |
+--------------------+---------+---------------+------------+
| ``RelWithDebInfo`` | Release | Performance   | Enabled    |
+--------------------+---------+---------------+------------+
| ``Release``        | Release | Performance   | Disabled   |
+--------------------+---------+---------------+------------+
| ``MinSizeRel``     | Release | Size          | Disabled   |
+--------------------+---------+---------------+------------+

--------------

*Copyright (c) 2021, Arm Limited and Contributors. All rights reserved.*
