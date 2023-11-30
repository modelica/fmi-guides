# CAN Node FMU with Triggered output

This directory contains a demo FMU a CAN node which uses a triggered output clock.

The CAN node periodically sends a CAN frame with ID `0x1` and 4 bytes of payload every 300ms.

The FMU requires a simulator with support for triggered clocks, event mode and variable step size.

## Contents

This directory contains the source and description files of the FMU:
- `src`: Source code of the FMU
- `description`: Description files of the FMU
- `PackFmu.py`: Script to generate a source-code FMU

## Building the FMU

A script `PackFMU.py` is provided, which packages the demo source files, as well as all required FMI-LS-BUS headers, into a source code FMU.
This FMU can then be loaded by an importer or precompiled using, e.g., FMPy.

## Running the FMU

To run the FMU, a supported simulator and optionally a bus simulation FMU is required.
It can either be directly connected to another node FMU (or even another instance of this FMU) or to a bus simulation FMU using the `CanChannel` terminal.
