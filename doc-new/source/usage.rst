Usage
*****

Building the library
====================

mesytec-mvlc works under Linux and Windows. The only required external
dependency is zlib. Optionally if `libzmq <https://github.com/zeromq/libzmq>`_
is found an additional zmq based readout data transport is enabled.

The build process requires gcc/clang with c++17 support, `CMake
<https://cmake.org>`_ and make or `Ninja <https://ninja-build.org/>`_. Windows
builds currently only work in an `MSYS2 <https://www.msys2.org/>`_ environment,
MSVC is not supported.

Build steps:::

    git clone https://github.com/flueke/mesytec-mvlc
    mkdir mesytec-mvlc/build
    cd mesytec-mvlc/build
    cmake -DCMAKE_BUILD_TYPE=Release ..
    make

Under windows add ``-G"MSYS Makefiles"`` to the CMake command line to generate
the proper Makefiles.

The ``MVLC_BUILD_TESTS`` CMake variable can be set to enable building of unit
tests. ``MVLC_BUILD_DOCS`` controls whether the documentation is built
(requires sphinx, breathe and doxygen).

Installing the library
======================

.. note::
   Installation is not required when the library is used as part of a CMake based
   project via *add_subdirectory()*.

Add ``-DCMAKE_INSTALL_PREFIX=/path/to/directory`` to the cmake invocation above
or set the variable in ``CMakeCache.txt``, then run ``make install`` to copy
the installation files.

Usage with CMake
================

The ``mesytec-mvlc`` library can be used as a CMake subproject by copying the
library directory into you project and adding::

    add_subdirectory(mesytec-mvlc)

to your ``CMakeLists.txt``.

An installed version of mesytec-mvlc can be located and used like this:::

    find_package(mesytec-mvlc REQUIRED)
    target_link_libraries(<my-target> PRIVATE mesytec-mvlc::mesytec-mvlc)

.. todo::
   For ``find_package(mesytec-mvlc)`` to work with non-standard installation
   paths the ``CMAKE_PREFIX_PATH`` environment variable has to be updated to
   contain the path to XXX
