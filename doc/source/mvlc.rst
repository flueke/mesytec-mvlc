MVLC
****

Test 1234

Readout data stream format
==========================

Ethernet packet format and packet loss handling
-----------------------------------------------

ETH header words
~~~~~~~~~~~~~~~~

Output producing stack commands
===============================

+----------------+------+----------------+------------------------------------------------------------+
| Command        | Code | # output words | Notes                                                      |
+----------------+------+----------------+------------------------------------------------------------+
| VMERead        | 0x12 | 1              | single read VME amods (0x09, 0x0D, ...) stack accu not set |
+----------------+------+----------------+------------------------------------------------------------+
| WriteMarker    | 0xC2 | 1              | produces the static marker value                           |
+----------------+------+----------------+------------------------------------------------------------+
| WriteSpecial   | 0xC1 | 1              | MVLC timestamp or current accu value                       |
+----------------+------+----------------+------------------------------------------------------------+
| VMERead        | 0x12 | N              | block VME amods (0x0b, 0x08, ...) or stack accu set        |
+----------------+------+----------------+------------------------------------------------------------+
| VMEMBLTSwapped | 0x13 | N              | only MBLT amods allowed (0x08, 0x0c)                       |
+----------------+------+----------------+------------------------------------------------------------+

.. .. doxygenclass:: StackCommandType
.. .. doxygennamespace:: mesytec::mvlc::stack_commands
.. doxygenenum:: mesytec::mvlc::stack_commands::StackCommandType

Outer Framing
=============

Inner BlockRead Framing
=======================
