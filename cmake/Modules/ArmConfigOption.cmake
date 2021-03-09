#[=======================================================================[.rst:
ArmConfigOption
---------------

.. default-domain:: cmake

.. command:: arm_config_option

Create a build system configuration option.

.. code-block:: cmake

    arm_config_option(
        NAME <name> HELP <help> [TYPE <type>]
        [DEFAULT <default>] [DEPENDS <depends-condition> [ELSE <else>]]
        [STRINGS <strings>... [FREEFORM]]
        [FORCE <force-condition>] [ADVANCED] [HIDDEN])

This helper function is intended to simplify some of the complex mechanics
involved in creating a robust, scalable configuration system for medium to
large projects. It incorporates basic dependency resolution, overrides and
stronger typing in order to provide a smoother experience for both build system
developers and users.

Basics
^^^^^^

Fundamentally, configuration options are a combination of normal CMake cache
variables and additional metadata. This module manages dependencies between
build system options, configurable default values, forced overrides, and user
interface cleanup.

Like cache variables, configuration options have a name, a single-line
documentation string, and a type. The types available to options are as follows
(their visual representations in :manual:`cmake-gui <manual:cmake-gui(1)>` and
:manual:`ccmake <manual:ccmake(1)>` are in parentheses):

- ``BOOL`` for booleans (represented as a toggle option).
- ``STRING`` for strings (represented as as a text box).
- ``PATH`` for directory paths (represented as a directory chooser).
- ``FILEPATH`` for file paths (represented as a file chooser).

Every configuration option must have a type, and it's important to choose the
right type for the option in order to provide a consistent experience for users
using the graphical and terminal CMake user interfaces.

In most cases, the type of a configuration option can be inferred from its
default value. The heuristic for this is as follows:

.. uml::

    if (Is ""TYPE"" provided?) then (yes)
      :""<type>"";
      kill
    else (no)
      if (Is ""STRINGS"" provided?) then (yes)
        :""STRING"";
        kill
      else (no)
        if (Is ""DEFAULT"" provided?) then (yes)
          if (Is ""<default>"" a boolean constant?) then (yes)
            :""BOOL"";
            kill
          else (no)
            :""STRING"";
            kill
          endif
        else (no)
          :""BOOL"";
          kill
        endif
      endif
    endif

Where boolean constants are considered to be any value matching the following
regular expression:

.. code-block::

    /^(N|Y|NO|YES|OFF|ON|FALSE|TRUE)$/i

.. code-block:: cmake

    arm_config_option(NAME XYZ ... TYPE BOOL) # BOOL
    arm_config_option(NAME XYZ ... TYPE STRING) # STRING
    arm_config_option(NAME XYZ ... TYPE PATH) # PATH
    arm_config_option(NAME XYZ ... TYPE FILEPATH) # FILEPATH

    arm_config_option(NAME XYZ ... DEFAULT ON) # BOOL
    arm_config_option(NAME XYZ ... DEFAULT "default") # STRING
    arm_config_option(NAME XYZ ... STRINGS ...) # STRING
    arm_config_option(NAME XYZ ...) # BOOL

Default Values
^^^^^^^^^^^^^^

Every configuration option has a default value, regardless of whether or not one
has been explicitly specified. The *default* default value varies based on the
type, and is derived according to the following heuristic:

.. uml::

    if (Is ""DEFAULT"" provided?) then (yes)
      :""<default>"";
      kill
    else (no)
      if (Is ""STRINGS"" provided?) then (yes)
        :First element of ""<strings>"";
        kill
      else (no)
        if (Is the type ""BOOL""?) then (yes)
          :""OFF"";
          kill
        else (no)
          :(empty string);
          kill
        endif
      endif
    endif

Note that the ``DEFAULT`` argument is ignored if there is a single value in
``<strings>`` and the ``FREEFORM`` flag has not been provided.

.. code-block:: cmake

    arm_config_option(NAME XYZ DEFAULT ON ... TYPE BOOL) # ON
    arm_config_option(NAME XYZ DEFAULT XYZ ... TYPE STRING) # "XYZ"

    arm_config_option(NAME XYZ ... STRINGS X Y Z) # "X"
    arm_config_option(NAME XYZ ... STRINGS X Y Z DEFAULT Y) # "Y"
    arm_config_option(NAME XYZ ... STRINGS X DEFAULT Y) # "X"

    arm_config_option(NAME XYZ ... TYPE BOOL) # OFF
    arm_config_option(NAME XYZ ... TYPE STRING) # ""
    arm_config_option(NAME XYZ ... TYPE PATH) # ""
    arm_config_option(NAME XYZ ... TYPE FILEPATH) # ""

Validating Values
^^^^^^^^^^^^^^^^^

String-like configuration options may restrict the set of values that can be
used. The ``STRINGS`` argument is designed to facilitate this, and it takes a
list of valid values that the user may provide for this option.

Within the CMake user interfaces (:manual:`cmake-gui <manual:cmake-gui(1)>` and
:manual:`ccmake <manual:ccmake(1)>`) options that have been given a list of
``<strings>...`` do not take on the form of a standard text box, but instead
are given a drop-down list of values.

If the option is configured with a value outside of the strings list, then an
error is reported and configuration is aborted:

.. code-block:: cmake
    :caption: Example usage
    :linenos:

    # cmake -DPLAYER_CLASS=Paladin ...

    arm_config_option(
        NAME PLAYER_CLASS
        HELP "The player's class."
        STRINGS "Warrior" "Mage" "Archer")

    # Invalid value (`Paladin`) for `PLAYER_CLASS`! This configuration option
    # supports the following values:
    #
    # - Warrior
    # - Mage
    # - Archer

To allow arbitrary values for an option whilst still offering a drop-down
selection, the ``FREEFORM`` flag may be provided. In this case, validation
against the string list is disabled, and the user may provide any value.

Note that if the ``<strings>...`` list offers only one value and the
``FREEFORM`` flag has not been given, the option is forcibly set to the string
provided. This is to prevent situations where the default is incompatible with
the string list, but the user is unable to provide another value.

Advanced Options
^^^^^^^^^^^^^^^^

Configuration options can be marked as "advanced" by using the ``ADVANCED``
flag. In CMake's user interfaces, this hides the configuration option behind the
"advanced" toggle:

.. code-block:: cmake

    arm_config_option(NAME XYZ ...) # Always visible
    arm_config_option(NAME XYZ ... ADVANCED) # Visible only when requested

Hidden Options
^^^^^^^^^^^^^^

In some cases, it may make sense to a hide a configuration option. This is
predominantly useful for *internal* options - options that are not intended to
be configured by the user, but by some other part of the build system.

Hidden options cannot be modified from the UI, and so in that sense they are
similar to configuration options whose dependency never evaluates successfully:

.. code-block:: cmake

    arm_config_option(NAME XYZ ... HIDDEN) # Not directly configurable

Dependencies
^^^^^^^^^^^^

Dependencies between options can be modelled using the ``DEPENDS`` argument.
This argument takes an expression in :ref:`Condition Syntax`, which determines
whether a value can be provided for the option, and whether or not the option
will appear in the user interfaces.

For example, if you have a feature flag ``foo``, and you have a feature flag
``bar`` that only makes sense when ``foo`` is enabled, you might use:

.. code-block:: cmake
    :caption: Example usage
    :linenos:

    arm_config_option(
        NAME MYPROJECT_ENABLE_FOO
        HELP "Enable the foo feature.")

    arm_config_option(
        NAME MYPROJECT_ENABLE_BAR
        HELP "Enable the bar feature."
        DEPENDS MYPROJECT_ENABLE_FOO)

Configuration options whose dependencies have not been met are hidden from the
user interface (that is, the cache variable is given the ``INTERNAL`` type), and
the default value is restored. If you need a value *other* than the default to
be set if the dependency is not met, then use the ``ELSE`` argument.

In the following example, the program can be configured with either a separate
stack and heap, or a combined stack-heap as is common in deeply-embedded
microcontroller firmware. In this example build system, whether a combined
stack-heap is used or not is determined by the ``ENABLE_STACKHEAP`` option. When
the stack-heap is enabled, the ``STACKHEAP_SIZE`` option is exposed, but when
the stack-heap is disabled the ``STACK_SIZE`` and ``HEAP_SIZE`` options are
instead exposed:

.. code-block:: cmake
    :caption: Example usage
    :linenos:

    arm_config_option(
        NAME ENABLE_STACKHEAP
        HELP "Enable a combined stack-heap?"
        DEFAULT ON)

    arm_config_option(
        NAME STACKHEAP_SIZE
        HELP "Stack-heap size."
        DEFAULT 65536
        DEPENDS ENABLE_STACKHEAP
        ELSE 0)

    arm_config_option(
        NAME STACK_SIZE
        HELP "Stack size (in bytes)."
        DEFAULT 512
        DEPENDS NOT ENABLE_STACKHEAP
        ELSE 0)

    arm_config_option(
        NAME HEAP_SIZE
        HELP "Heap size (in bytes)."
        DEFAULT 65536
        DEPENDS NOT ENABLE_STACKHEAP
        ELSE 0)

Forcing Updates
^^^^^^^^^^^^^^^

In some cases you may need to forcibly update the value of a configuration
option when certain conditions are met. You can do this using the ``FORCE``
argument which, like ``DEPENDS``, accepts :ref:`Condition Syntax`.

In the following example, ``FORCE`` is used to replace the default value of the
:variable:`CMAKE_BUILD_TYPE <variable:CMAKE_BUILD_TYPE>` cache variable with
one defined by the build system configuration:

.. code-block:: cmake
    :caption: Example usage
    :linenos:

    arm_config_option(
        NAME CMAKE_BUILD_TYPE
        HELP "Build type."
        STRINGS "Debug" "RelWithDebInfo" "MinSizeRel" "Release"
        DEFAULT "MinSizeRel"
        FORCE CMAKE_BUILD_TYPE STREQUAL "")

Detecting Modifications
^^^^^^^^^^^^^^^^^^^^^^^

It is sometimes necessary to know whether a configuration option has been
modified, either to react elsewhere in the build system or to offer diagnostics.
All configuration options have three additional cache variables describing
modification-related information:

- ``<name>_CHANGED`` is a boolean value that indicates whether the value of the
  option has changed since configuration was last run.
- ``<name>_OLD`` is the value of the option after the previous configuration
  run.
- ``<name>_NEW`` is the value of the option during the current configuration
  run, and is identical to ``<name>``.

.. code-block:: cmake
    :caption: Example usage
    :linenos:

    arm_config_option(
        NAME ENABLE_FEATURE
        HELP "Enable the feature.")

    if(ENABLE_FEATURE_CHANGED)
        message(WARNING "The feature's been toggled!")
    endif()

Overrides
^^^^^^^^^

.. command:: arm_config_option_override

Override the default or final value of a configuration option defined by
:command:`arm_config_option`.

.. note::

    Configuration options can only be overridden if their dependencies are met,
    and :command:`arm_config_option_override` must always come before its
    associated :command:`arm_config_option`. This ensures the configuration
    space is always in a valid state.

Overriding Defaults
^^^^^^^^^^^^^^^^^^^

.. code-block:: cmake

    arm_config_option_override(NAME <name> DEFAULT <default>
                               [ERROR_MESSAGE <error-message>])

Overrides the default value of the configuration option ``<name>`` with the
value ``<default>``. If an error message ``<error-message>`` has been supplied
then this error message will be printed instead of the default one.

.. code-block:: cmake
    :caption: Example usage
    :linenos:

    arm_config_option_override(
        NAME MYPROJECT_USE_FOO
        DEFAULT ON)

    arm_config_option(
        NAME MYPROJECT_USE_FOO
        HELP "Use foo.")

In this situation, the configuration option ``USE_FOO`` is created with a
default value of ``ON``. This is most often useful in larger projects where
certain default values make more sense under certain conditions (such as when
an optional component is included).

.. note::

    Multiple default value overrides may be specified, but only the last will be
    used.

Overriding Values
^^^^^^^^^^^^^^^^^

.. code-block:: cmake

    arm_config_option_override(NAME <name> VALUE <value>)

Overrides the value of the configuration option ``<name>`` with ``<value>``.

In the following example, ``USE_FOO`` will be set to ``ON`` hidden from the
CMake user interface by the override. Users may no longer configure this value
themselves. Attempting to change the value of the configuration option will
cause a configuration failure, including by other value overrides:

.. code-block:: cmake
    :caption: Example usage
    :linenos:

    arm_config_option_override(
        NAME MYPROJECT_USE_FOO
        VALUE ON)

    arm_config_option(
        NAME MYPROJECT_USE_FOO
        HELP "Use foo.")

.. note::

    Only one value override may be specified for a given configuration option -
    multiple value overrides with different values will trigger an error. Value
    overrides will replace any default overrides specified for the configuration
    option.
#]=======================================================================]

include_guard()

include(ArmAssert)
include(ArmExpand)

#
# Invoke the configuration option clean-up logic when CMake leaves the current
# source directory. This utilises a recent CMake feature that allows us to defer
# a function call to when the script exits a directory.
#

cmake_language(DEFER
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    CALL _arm_config_option_cleanup)

#
# Mark a configuration option as active.
#
# Marking a configuration option as active consists of adding it to the list of
# active options. Because it is a global property the list is automatically
# cleared at the beginning of each configuration, so it only ever consists of
# the configuration options that were activated during the current configuration
# run.
#

function(_arm_config_option_activate name)
    #
    # Add the option to the active options property, which contains only options
    # activated in this configuration run.
    #

    get_property(actives GLOBAL PROPERTY _ARM_CONFIG_OPTION_ACTIVES)

    list(APPEND actives ${name})
    list(REMOVE_DUPLICATES actives)

    set_property(GLOBAL PROPERTY _ARM_CONFIG_OPTION_ACTIVES "${actives}")

    #
    # Add the option to the active options cache, which also contains options
    # that were activated in previous configuration runs.
    #

    list(APPEND _ARM_CONFIG_OPTION_CACHE ${ARG_NAME})
    list(REMOVE_DUPLICATES _ARM_CONFIG_OPTION_CACHE)

    set(_ARM_CONFIG_OPTION_CACHE "${_ARM_CONFIG_OPTION_CACHE}" CACHE INTERNAL
        "Cache of created configuration options." FORCE)
endfunction()

#
# Check whether a configuration option is active.
#

function(_arm_config_option_is_active result name)
    get_property(actives GLOBAL PROPERTY _ARM_CONFIG_OPTION_ACTIVES)

    if(name IN_LIST actives)
        set(${result} yes PARENT_SCOPE)
    else()
        set(${result} no PARENT_SCOPE)
    endif()
endfunction()

#
# Clean up a configuration option.
#
# This is only done for inactive configuration options, and it consists simply
# of removing any cache variables associated with it.
#

function(_arm_config_option_cleanup_option name)
    unset(${name} CACHE)

    unset(${name}_CHANGED CACHE)

    unset(${name}_OLD CACHE)
    unset(${name}_NEW CACHE)
endfunction()

#
# Clean up inactive configuration options.
#
# Inactive configuration options are those that were created at some point in
# previous configuration runs, but which were used in the current one.
#
# Why clean up inactive options? The answer is simple: too many options clutters
# up the user interface. Options that are not actively in use serve no purpose,
# and on large, highly configurable projects there can be many, many inactive
# options.
#

function(_arm_config_option_cleanup)
    #
    # For each configuration option that has been defined at some point - and
    # that might have been in a previous configuration run - figure out if it
    # was defined in the current configuration run and, if not, remove it.
    #

    foreach(option-name IN LISTS _ARM_CONFIG_OPTION_CACHE)
        _arm_config_option_is_active(active ${option-name})

        if(NOT active)
            _arm_config_option_cleanup_option(${option-name})
        endif()
    endforeach()
endfunction()

#
# Normalize a boolean value.
#
# Normalizes a boolean value into its `ON`/`OFF` form. This form is chosen
# because the CMake user interfaces use it, rather than for any ideological
# reason.
#

macro(_arm_config_option_normalize_bool var)
    if(${var})
        set(${var} ON)
    else()
        set(${var} OFF)
    endif()
endmacro()

#
# Identify a boolean value.
#

function(_arm_config_option_is_bool out-var var)
    string(TOUPPER "${${var}}" upper)

    if(upper MATCHES "^(N|Y|NO|YES|OFF|ON|FALSE|TRUE)$")
        set("${out-var}" TRUE PARENT_SCOPE)
    else()
        set("${out-var}" FALSE PARENT_SCOPE)
    endif()
endfunction()

#
# Try to normalize a boolean value.
#

function(_arm_config_option_try_normalize_bool var)
    _arm_config_option_is_bool(is-bool "${var}")

    if(is-bool)
        _arm_config_option_normalize_bool("${var}")
    endif()

    set("${var}" "${${var}}" PARENT_SCOPE)
endfunction()

function(arm_config_option)
    set(options "ADVANCED;HIDDEN;FREEFORM")
    set(single-args "NAME;HELP;TYPE")
    set(multi-args "DEFAULT;STRINGS;DEPENDS;ELSE;FORCE")

    cmake_parse_arguments(PARSE_ARGV 0 ARG
        "${options}" "${single-args}" "${multi-args}")

    arm_assert(
        CONDITION DEFINED ARG_NAME
        MESSAGE "No value was given for the `NAME` argument.")

    arm_assert(
        CONDITION DEFINED ARG_HELP
        MESSAGE "No value was given for the `HELP` argument.")

    arm_assert(
        CONDITION ((NOT DEFINED ARG_TYPE) OR
                   (ARG_TYPE MATCHES "BOOL|STRING|PATH|FILEPATH")) AND
                  (NOT TYPE IN_LIST ARG_KEYWORDS_MISSING_VALUES)
        MESSAGE "An invalid value for the `TYPE` argument was given. Valid "
                "values are:\n"

                " - `BOOL` (boolean options)\n"
                " - `STRING` (string options)\n"
                " - `PATH` (directory path options)\n"
                " - `FILEPATH` (file path options)")

    arm_assert(
        CONDITION NOT DEFAULT IN_LIST ARG_KEYWORDS_MISSING_VALUES
        MESSAGE "An empty value was given for the `DEFAULT` argument.")

    arm_assert(
        CONDITION NOT DEPENDS IN_LIST ARG_KEYWORDS_MISSING_VALUES
        MESSAGE "An empty value was given for the `DEPENDS` argument.")

    arm_assert(
        CONDITION NOT ELSE IN_LIST ARG_KEYWORDS_MISSING_VALUES
        MESSAGE "An empty value was given for the `ELSE` argument.")

    arm_assert(
        CONDITION NOT FORCE IN_LIST ARG_KEYWORDS_MISSING_VALUES
        MESSAGE "An empty value was given for the `FORCE` argument.")

    arm_assert(
        CONDITION NOT STRINGS IN_LIST ARG_KEYWORDS_MISSING_VALUES
        MESSAGE "An empty value was given for the `STRINGS` argument.")

    arm_assert(
        CONDITION (NOT ARG_FREEFORM) OR (DEFINED ARG_STRINGS)
        MESSAGE "The `FREEFORM` flag requires the `STRINGS` argument.")

    arm_assert(
        CONDITION (NOT DEFINED ARG_ELSE) OR (DEFINED ARG_DEFAULT)
        MESSAGE "The `ELSE` argument requires the `DEFAULT` argument.")

    arm_assert(
        CONDITION (NOT DEFINED ARG_ELSE) OR (DEFINED ARG_DEPENDS)
        MESSAGE "The `ELSE` argument requires the `DEPENDS` argument.")

    #
    # Determine whether there is only a single possible value for the option.
    # This can occur with non-freeform options with a string list containing
    # only one value. When this is the case, we can override the user's default
    # because it will be either identical to the first entry in the list, or
    # invalid.
    #

    if(DEFINED ARG_STRINGS AND NOT ARG_FREEFORM)
        list(LENGTH ARG_STRINGS strings-count)

        if(strings-count EQUAL 1)
            arm_config_option_override(
                NAME "${ARG_NAME}"
                VALUE "${ARG_STRINGS}"
                ERROR_MESSAGE
                    "A value override (`@override-value@`) has been specified "
                    "for the `${ARG_NAME}` option, but it conflicts with the "
                    "only possible value for the option (`${ARG_STRINGS}`).")
        endif()
    endif()

    #
    # Attempt to derive the type from the other arguments given, with the
    # following heuristic:
    #
    # 1. If the `STRINGS` argument was passed, the type is `STRING`.
    # 2. If the `DEFAULT` argument was passed:
    #     2.1. If the value is a boolean constant, the type is `BOOL`.
    #     2.2. The type is `STRING`.
    # 3. The type is `BOOL`.
    #
    # Boolean constants are considered to be the following (case-insensitive):
    #
    # - `N` or `Y`
    # - `NO` or `YES`
    # - `OFF` or `ON`
    # - `FALSE` or `TRUE`
    #

    if(NOT DEFINED ARG_TYPE)
        set(ARG_TYPE "BOOL")

        if(DEFINED ARG_STRINGS)
            set(ARG_TYPE "STRING")
        elseif(DEFINED ARG_DEFAULT)
            _arm_config_option_is_bool(is-bool ARG_DEFAULT)

            if(NOT is-bool)
                set(ARG_TYPE "STRING")
            endif()
        endif()
    endif()

    #
    # Identify a reasonable default if one has not been provided. For `BOOL`
    # this is `OFF`. If `STRINGS` has been provided then we take the first
    # entry in the list. For any other type we use an empty string.
    #

    if(NOT DEFINED ARG_DEFAULT)
        if(ARG_TYPE STREQUAL "BOOL")
            set(ARG_DEFAULT "OFF")
        elseif(DEFINED ARG_STRINGS)
            list(GET ARG_STRINGS 0 ARG_DEFAULT)
        else()
            set(ARG_DEFAULT "")
        endif()
    endif()

    #
    # If no `DEPENDS` condition has been provided, it defaults to `TRUE`. That
    # is, the dependency can never be broken.
    #

    if(NOT DEFINED ARG_DEPENDS)
        set(ARG_DEPENDS "TRUE")
    endif()

    #
    # If no `ELSE` value has been provided, it is the same as the default value.
    # This means that if the dependency evaluates false, it is forced to the
    # initial default value.
    #

    if(NOT DEFINED ARG_ELSE)
        set(ARG_ELSE "${ARG_DEFAULT}")
    endif()

    #
    # If no force condition has been provided, it defaults to `FALSE`. In this
    # case, the option is never forcibly updated.
    #

    if(NOT ARG_FORCE)
        set(ARG_FORCE "FALSE")
    endif()

    #
    # If the type is boolean, normalize the `DEFAULT` and `ELSE` values. This
    # allows us to safely do string comparisons of these values against other
    # boolean values.
    #

    if(ARG_TYPE STREQUAL "BOOL")
        _arm_config_option_normalize_bool(ARG_DEFAULT)
        _arm_config_option_normalize_bool(ARG_ELSE)
    endif()

    #
    # If the dependency evaluates to false, then force the option to its else
    # value.
    #

    if(NOT (${ARG_DEPENDS}))
        arm_config_option_override(
            NAME "${ARG_NAME}" VALUE "${ARG_ELSE}" TYPE "${ARG_TYPE}"
            ERROR_MESSAGE
                "The value of `${ARG_NAME}` cannot be overridden with "
                "`@override-value@` because its dependency has not been met, "
                "and its value has therefore been set to `${ARG_ELSE}`:\n"

                ${ARG_DEPENDS})
    endif()

    #
    # At this point, we have derived everything we can from the parameters we've
    # been given. We now need to look at the overrides to determine whether the
    # value or the default value has been overridden elsewhere.
    #

    get_property(override-exists GLOBAL
        PROPERTY "_ARM_CONFIG_OPTION_${ARG_NAME}_TYPE" SET)

    if(override-exists)
        get_property(override-type GLOBAL
            PROPERTY "_ARM_CONFIG_OPTION_${ARG_NAME}_TYPE")
        get_property(override-value GLOBAL
            PROPERTY "_ARM_CONFIG_OPTION_${ARG_NAME}_VALUE")

        if(override-type STREQUAL "DEFAULT")
            set(default-override-exists yes)
            set(value-override-exists no)
        elseif(override-type STREQUAL "VALUE")
            set(default-override-exists no)
            set(value-override-exists yes)
        endif()

        #
        # The override function is not aware of the type of the option because
        # it has to be used prior to creating the option, so we need to
        # normalize boolean options at this point.
        #

        if(type STREQUAL "BOOL")
            _arm_config_option_normalize_bool(override-value)
        endif()
    else()
        set(value-override-exists no)
        set(default-override-exists no)
    endif()

    arm_assert(
        CONDITION (${ARG_DEPENDS}) OR (NOT value-override-exists) OR
                  (ARG_ELSE STREQUAL override-value)
        MESSAGE "A value override exists for the `${ARG_NAME}` option, but the "
                "option's dependency has not been met and the overridden "
                "conflicts with the option's `ELSE` value.\n"

                " - The current value is: ${ARG_ELSE}\n"
                " - The proposed override value is: ${override-value}\n")

    #
    # If an override exists, apply its value now.
    #

    if(override-exists)
        set(ARG_DEFAULT "${override-value}")
    endif()

    #
    # If a value override has been given then the option is hidden.
    #

    if(value-override-exists)
        set(ARG_HIDDEN YES)
    endif()

    #
    # Determine whether the configuration option has already been created.
    #

    if(DEFINED CACHE{${ARG_NAME}})
        set(preexisting yes)

        get_property(preexisting-type CACHE "${ARG_NAME}" PROPERTY TYPE)
        get_property(preexisting-value CACHE "${ARG_NAME}" PROPERTY VALUE)

        if(preexisting-type STREQUAL "BOOL")
            _arm_config_option_normalize_bool(preexisting-value)
        endif()

        #
        # It's possible for the user to create a cache variable with a type that
        # differs from what we're expecting. If that's the case, force the cache
        # variable to adopt the type provided to us directly.
        #

        if(NOT preexisting-type MATCHES "${ARG_TYPE}|INTERNAL|UNINITIALIZED")
            message(WARNING
                "This configuration option has already been defined, but with "
                "a different type (`${preexisting-type}`). Its type has been "
                "reset.")

            set_property(CACHE "${ARG_NAME}" PROPERTY TYPE ${ARG_TYPE})
        endif()

        #
        # If a hidden configuration option has been given a value that differs
        # from the one that we're about to give it, warn the user that their
        # choice might have been overridden.
        #

        if(ARG_HIDDEN AND (NOT preexisting-value STREQUAL ARG_DEFAULT))
            message(WARNING
                "The `${ARG_NAME}` configuration option has been forcibly set "
                "to `${ARG_DEFAULT}`, overwriting its previous value of "
                "`${preexisting-value}`.")

            set(ARG_FORCE TRUE)
        endif()

        #
        # If a non-hidden configuration option has the `INTERNAL` type, it's
        # because its dependency previously failed. If the dependency is no
        # longer failing, we need to make it visible again.
        #

        if((NOT ARG_HIDDEN) AND (preexisting-type STREQUAL "INTERNAL"))
            set(ARG_FORCE TRUE)
        endif()
    else()
        set(preexisting no)
    endif()

    #
    # If the option is hidden, the real type of the cache variable is
    # `INTERNAL`.
    #

    set(type-keyword ${ARG_TYPE})

    if(ARG_HIDDEN)
        set(type-keyword INTERNAL)
    endif()

    #
    # If the force condition evaluates truthfully, pass `FORCE` to the cache
    # variable.
    #

    set(force-keyword)

    if(${ARG_FORCE})
        set(force-keyword FORCE)
    endif()

    #
    # Create the cache variable that represents the configuration option, and
    # activate it for this configuration run.
    #

    set("${ARG_NAME}" "${ARG_DEFAULT}"
        CACHE "${type-keyword}" "${ARG_HELP}" ${force-keyword})

    if(ARG_ADVANCED)
        mark_as_advanced("${ARG_NAME}")
    endif()

    _arm_config_option_activate(${ARG_NAME})

    #
    # If we've been given a list of valid values, update the STRINGS property of
    # the cache variable with that list.
    #

    if(DEFINED ARG_STRINGS)
        set_property(CACHE "${ARG_NAME}"
            PROPERTY STRINGS "${ARG_STRINGS}")

        #
        # If we haven't been asked to offer a freeform text box, let the user
        # know if they've provided something out of bounds.
        #

        if((NOT ARG_FREEFORM) AND (NOT "${${ARG_NAME}}" IN_LIST ARG_STRINGS))
            set(values "")

            foreach(string IN LISTS ARG_STRINGS)
                string(APPEND values "\n - ${string}")
            endforeach()

            message(FATAL_ERROR
                "Invalid value (`${${ARG_NAME}}`) for `${ARG_NAME}`! This "
                "configuration option supports the following values: ${values}")
        endif()
    endif()

    #
    # Update the change-tracking variables. These are:
    #
    # - ${name}_OLD: The previous value of the option.
    # - ${name}_NEW: The new value of the option.
    # - ${name}_CHANGED: Whether the value of the option changed recently.
    #
    # This is a pretty simple algorithm: when we create a new configuration
    # option we also create the `${name}_NEW` variable with the newly-created
    # value. If the value of `${name}_NEW` and `${name}` differs at any point in
    # further configuration runs, it's because something (either the user or the
    # script) has changed the value of the option.
    #

    if(DEFINED ${ARG_NAME}_NEW)
        set(old "${${ARG_NAME}_NEW}")
        set(new "${${ARG_NAME}}")

        if("${old}" STREQUAL "${new}")
            set(changed no)
        else()
            set(changed yes)
        endif()
    else()
        set(old "${${ARG_NAME}}")
        set(new "${${ARG_NAME}}")

        set(changed no)
    endif()

    set("${ARG_NAME}_OLD" "${old}"
        CACHE INTERNAL "Previous value of `${ARG_NAME}`." FORCE)

    set("${ARG_NAME}_NEW" "${new}"
        CACHE INTERNAL "Latest value of `${ARG_NAME}`." FORCE)

    set("${ARG_NAME}_CHANGED" ${changed}
        CACHE INTERNAL "Has `${ARG_NAME}` just changed?" FORCE)
endfunction()

function(arm_config_option_override)
    set(options "")
    set(single-args "NAME;DEFAULT;TYPE")
    set(multi-args "VALUE;ERROR_MESSAGE")

    cmake_parse_arguments(PARSE_ARGV 0 ARG
        "${options}" "${single-args}" "${multi-args}")

    arm_assert(
        CONDITION DEFINED ARG_NAME
        MESSAGE "Please provide the `NAME` of the configuration option to "
                "override.")

    arm_assert(
        CONDITION (DEFINED ARG_DEFAULT) OR (DEFINED ARG_VALUE)
        MESSAGE "Please specify either a `DEFAULT` or a `VALUE`.")

    arm_assert(
        CONDITION NOT ((DEFINED ARG_DEFAULT) AND (DEFINED ARG_VALUE))
        MESSAGE "A `DEFAULT` and a `VALUE` cannot both be specified.")

    arm_assert(
        CONDITION (NOT DEFAULT IN_LIST ARG_KEYWORDS_MISSING_VALUES)
        MESSAGE "An empty value was given for the `DEFAULT` argument.")

    arm_assert(
        CONDITION (NOT VALUE IN_LIST ARG_KEYWORDS_MISSING_VALUES)
        MESSAGE "An empty value was given for the `VALUE` argument.")

    arm_assert(
        CONDITION ((NOT DEFINED ARG_TYPE) OR
                   (ARG_TYPE MATCHES "BOOL|STRING|PATH|FILEPATH")) AND
                  (NOT TYPE IN_LIST ARG_KEYWORDS_MISSING_VALUES)
        MESSAGE "An invalid value for the `TYPE` argument was given. Valid "
                "values are:\n"

                " - `BOOL` (boolean options)\n"
                " - `STRING` (string options)\n"
                " - `PATH` (directory path options)\n"
                " - `FILEPATH` (file path options)")

    #
    # Give some nicer names to these arguments, some of which we will need to
    # manipulate later.
    #

    if(DEFINED ARG_DEFAULT)
        set(type "DEFAULT")
        set(value "${ARG_DEFAULT}")
    elseif(DEFINED ARG_VALUE)
        set(type "VALUE")
        set(value "${ARG_VALUE}")
    endif()

    #
    # Try to normalize boolean values so that we don't get override conflicts
    # between, e.g. `FALSE` and `OFF`.
    #

    if((NOT DEFINED ARG_TYPE) OR (ARG_TYPE STREQUAL "BOOL"))
        _arm_config_option_try_normalize_bool(value)
    endif()

    #
    # Configuration options cannot be overridden after they've been created
    # because it causes a split-brain situation where the option is created with
    # one value, and then anything past the override gives the option a new
    # value.
    #

    _arm_config_option_is_active(active ${ARG_NAME})

    if(active)
        message(FATAL_ERROR
            "The configuration option `${ARG_NAME}` has already been created. "
            "Configuration options must be overridden prior to their creation.")
    endif()

    #
    # Multiple overrides are generally permitted, but the exception is for
    # multiple value overrides where the values conflict.
    #

    get_property(override-exists GLOBAL
        PROPERTY "_ARM_CONFIG_OPTION_${ARG_NAME}_TYPE" SET)

    if(override-exists)
        get_property(override-type GLOBAL
            PROPERTY "_ARM_CONFIG_OPTION_${ARG_NAME}_TYPE")
        get_property(override-value GLOBAL
            PROPERTY "_ARM_CONFIG_OPTION_${ARG_NAME}_VALUE")

        set(error-message
            "A value override already exists for this option:\n"

            " - Existing override value: ${override-value}\n"
            " - Proposed override value: ${value}")

        if(DEFINED ARG_ERROR_MESSAGE)
            arm_expand(OUTPUT error-message STRING "${ARG_ERROR_MESSAGE}")
        endif()

        arm_assert(
            CONDITION ((NOT "${type}" STREQUAL "VALUE") OR
                       (NOT "${override-type}" STREQUAL "VALUE")) OR
                      ("${value}" STREQUAL "${override-value}")
            MESSAGE ${error-message})

        #
        # Don't update the override if the existing one has higher precedence.
        #

        if((type STREQUAL "DEFAULT") AND (override-type STREQUAL "VALUE"))
            return()
        endif()
    endif()

    #
    # Export the override values as properties. These are picked up by the
    # configuration option when it's created. We don't use cache values for
    # these because we don't want them to persist across configuration runs in
    # case the override is not called again.
    #

    set_property(GLOBAL
        PROPERTY "_ARM_CONFIG_OPTION_${ARG_NAME}_TYPE" "${type}")
    set_property(GLOBAL
        PROPERTY "_ARM_CONFIG_OPTION_${ARG_NAME}_VALUE" "${value}")
endfunction()
