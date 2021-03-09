Variables
=========

.. default-domain:: cmake

.. contents::

This page documents variables that are provided by the build system, grouped by
the component that manages them. For general information on variables in CMake,
see the :ref:`Variables <CMake Language Variables>` section of the
:manual:`cmake-language(7) <manual:cmake-language(7)>` manual.

Intrinsic Variables
-------------------

Variables defined by CMake, but which have a value or meaning defined by the
Trusted Firmware-A build system. For documentation on *all* intrinsic CMake
variables, see the :manual:`cmake-variables(7) <manual:cmake-variables(7)>`
manual.

.. toctree::
    :glob:
    :maxdepth: 1

Common Variables
----------------

Variables which are always defined.

.. toctree::
    :glob:
    :maxdepth: 1

    variable/common/*

Bootloader Variables
--------------------

Variables defined by individual bootloader images. These variables may not be
defined if the bootloader image is not compatible with the platform.

Library Variables
-----------------

Variables defined by individual libraries and other non-core components. These
variables may not be defined if the library or component is not being included
in the build.

Driver Variables
----------------

Variables defined by individual drivers. These variables may not be defined if
the driver is not being included in the build.

Platform Variables
------------------

Variables defined by individual platforms. These variables may not be defined if
the platform is not being included in the build.

--------------

*Copyright (c) 2021, Arm Limited and Contributors. All rights reserved.*
