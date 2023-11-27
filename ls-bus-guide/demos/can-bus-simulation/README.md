# CAN Bus Simulation FMU

This directory contains a demo FMU implementing a bus simulation FMU for CAN.
The FMU provides terminals to connect exactly two nodes.

Currently, the bus simulation provides the following capabilities:
- Forwarding of CAN frames and sending confirmations
- Basic timing simulation based on baud rate
- Basic frame priorization

The FMU requires a simulator with support for triggered and countdown clocks, event mode and variable step size.

## Contents

This directory contains the source and description files of the FMU:
- `src`: Source code of the FMU
- `description`: Description files of the FMU
- `PackFmu.py`: Script to generate a source-code FMU

## Building the FMU

A script `PackFMU.py` is provided, which packages the demo source files, as well as all required FMI-LS-BUS headers, into a source code FMU.
This FMU can then be loaded by an importer or precompiled using, e.g., FMPy.

## Running the FMU

To run the FMU, a supported simulator and two CAN node FMUs (e.g., from the demo `can-node-triggered-output`) are required.
The node FMUs must be connected to the `Node1` and `Node2` terminals.
