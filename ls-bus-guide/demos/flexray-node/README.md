# FlexRay Node FMU

This directory contains a demo FMU a FlexRay node.

The CAN node periodically sends a FlexRay message in slots 1 and 3 or 2 and 4 of each cycle.
Which set of slots is used depends on the parameter `IsSecondNode`.

The FMU requires a simulator with support for countdown clocks, triggered clocks, event mode and variable step size.

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
It can either be directly connected to another node FMU (or even another instance of this FMU) or to a bus simulation FMU using the `FlexRayChannel` terminal.
This FMU can be instantiated two times per cluster, whereas the first instance must have the `IsSecondNode` parameter set to `false` and second instance must have it set to `true`.
The `PackFMU.py` generates two variants of the demo FMU with differing default values of the `IsSecondNode` parameter for ease of use.
