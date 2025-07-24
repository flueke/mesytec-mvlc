# Currently known MVLC software integrations

## FRIBDAQ

This is part of the mesytec-mvlc driver package. Readout data is written to
FRIB/NSCLDAQ ring buffers. Designed as a drop in replacement for VMUSB based
readouts. Supports FRIB slow controls and more.

Credits and thanks to Ron Fox for the implementation.

https://github.com/FRIBDAQ
https://github.com/flueke/mesytec-mvlc/tree/main/extras/fribdaq/Readme.md


## GANIL

Uses ZeroMQ to transport readout data into the GANIL processing code.
Headed by John Frankland <john.frankland@ganil.fr>

https://gitlab.in2p3.fr/mesytec-ganil/mesytec_data
https://mesytec-ganil.pages.in2p3.fr/mesytec_data/

## GSI MBS

https://www.gsi.de/en/work/research/experiment_electronics/data_processing/data_acquisition/mbs
https://www.gsi.de/fileadmin/EE/MBS/MBS_release_v70.pdf

USB only. mvme is used to perform the DAQ initialization, is then terminated and
a dedicated process takes over the readout. Pumps data into MBS frames. Depends
on the GSI TRIVA7 module and is thus limited to single-event mode but integrates
with the rest of the MBS infra. Uses the MVLCs stack accumulator feature to
dispatch TRIVA7 generated triggers to MVLC readout command stacks.

## chalmers.se drasi/nurdlib

https://gitlab.com/chalmers-subexp

Custom C implementation and integration into the drasi/nurdlib software.

## Others

Other experiments use mvme for the DAQ and basic analysis, then export to ROOT
via `mvme_root_client`.
