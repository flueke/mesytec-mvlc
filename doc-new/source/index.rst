mesytec-mvlc - driver and utilities for the mesytec MVLC VME controller
***********************************************************************

Overview
========

.. _Mesytec MVLC VME controller: https://mesytec.com/products/nuclear-physics/MVLC.html
.. _mvme: https://mesytec.com/downloads/mvme.html


**mesytec-mvlc** is a driver and utility library for the `Mesytec MVLC VME
controller`_ written in C++. The library consists of low-level code for
accessing MVLCs over USB or Ethernet and higher-level communication logic for
reading and writing internal MVLC registers and accessing VME modules.
Additionally the basic blocks needed to build high-performance MVLC based DAQ
readout systems are provided:

* Configuration holding the setup and readout information for a single VME
  crate containing multiple VME modules.

* Multithreaded readout worker and listfile writer using fast data compression
  (LZ4 or ZIP deflate).

* Readout parser which is able to handle potential ethernet packet loss.

* Live access to readout data on a best effort basis. Sniffing at the data
  stream does not block or slow down the readout.

* Examples showing how to combine the above parts into a working readout system
  and how to replay data from a previously recorded listfile.

* Various counters for monitoring the system.

**mesytec-mvlc** is used in the `mvme`_ DAQ software to implement MVLC support.

Contents
========

.. toctree::
   :maxdepth: 2

   usage
   api
