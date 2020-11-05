TODO {#todo}
============

* abstraction for the trigger/io system. This needs to be flexible because the
  system is going to change with future firmware udpates.

* examples
  - Minimal CrateConfig creation and writing it to file
  - Complete manual readout including CrateConfig setup for an MDPP-16

* Multicrate support (later)
  - Additional information needed and where to store it.
  - multi-crate-mini-daq
  - multi-crate-mini-daq-replay

* mini-daq and mini-daq-replay: load plugins like in mvme/listfile_reader This
  would make the two tools way more useful (and more complex). Specify plugins
  to load (and their args) on the command line.
