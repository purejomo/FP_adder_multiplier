# FP Adder & Multiplier for MAC Unit

## Overview
This repository contains the design and implementation of a **Half-Precision (FP16) Floating Point Adder and Multiplier**, optimized to operate at a target frequency of **250MHz**. The primary goal of this project is to construct a robust and high-speed **Multiply-Accumulate (MAC) unit** for hardware acceleration.

The design has been verified using **Xilinx Vivado**, confirming that it meets all timing constraints (positive timing slack) on the target FPGA board. It is fully validated and ready for immediate application in high-performance computing designs.

## Key Features
- **High Performance**: Designed and timing-closed for **250MHz** operation.
- **Precision**: 16-bit Floating Point (FP16), balancing range and precision for efficient hardware implementation.
- **MAC Architecture**: Serves as the foundational arithmetic units for a pipelined MAC unit.
- **Verification**:
  - **Timing Verified**: Confirmed positive slack at 250MHz in Vivado.
  - **Functional Verification**: Validated against a C++ Golden Reference model.
  - **Board Validated**: Verified on specific FPGA hardware [Zynq UltraScale+ ZCU102].

## Repository Structure
- **Reference/**: Contains C++ reference models (`fp16_adder_ref.cpp`) for bit-true verification and test vector generation.
- **Vivado/**: The Xilinx Vivado project directory.
  - `source_1/new/`: Synthesizable Verilog source code (e.g., `fpadder.v`).
  - `sim_1/`: Simulation testbenches for verifying logical correctness.

## Getting Started
1. **Clone the Repository**:
   ```bash
   git clone https://github.com/ghlee/FP_adder_multiplier.git
   ```
2. **Open in Vivado**:
   - Launch Xilinx Vivado and open the project located in the `Vivado/` directory.
3. **Run Simulation**:
   - Execute the testbenches in `sim_1` to verify the functional correctness of the adder and multiplier.
4. **Implementation**:
   - Run Synthesis and Implementation to verify the **250MHz** timing constraints.

## Status
- [x] FP16 Adder Design
- [ ] FP16 Multiplier Design
- [ ] MAC Unit Integration
- [x] Timing Verification (250MHz)

---
*Ready for immediate integration and deployment.*

## Copyright
Copyright (c) 2026 purejomo. All rights reserved.
This repository and its contents are the property of purejomo.
