# Simulator Calibration Report: vppp vs RTL (CoralNPU)

## Correlation Matrix

| Microbenchmark / Test | RTL Cycles | VP++ Simulator | % Error |
| :--- | :---: | :---: | :---: |
| `VLSU STRIDE8 VL256` | 29 | 29 | **0.0%** |
| `VLSU GATHER VL256` | 27 | 27 | **0.0%** |
| `VALU ADD M1 VL256` | 17 | 17 | **0.0%** |
| `VALU MUL VL256` | 20 | 20 | **0.0%** |
| `VALU REDSUM VL256` | 22 | 22 | **0.0%** |
| `VALU MASKADD VL256` | 19 | 19 | **0.0%** |
| `VALU SLIDEUP VL256` | 20 | 20 | **0.0%** |
| `VALU VWADD VL256` | 17 | 17 | **0.0%** |
| `CHAINING VADD_VMUL VL256` | 22 | 22 | **0.0%** |
| `RAW ALU_ALU VL256` | 21 | 21 | **0.0%** |
| `RAW MUL_ALU VL256` | 21 | 21 | **0.0%** |
| `RAW ALU_MUL VL256` | 21 | 21 | **0.0%** |
| `NODEP ALU_ALU VL256` | 18 | 18 | **0.0%** |
| `VSETVLI M1 VL16` | 17 | 17 | **0.0%** |
| `VADD_ONLY M1 VL16` | 17 | 17 | **0.0%** |

## Tier Verification
- **Tier 1 (Hooks alive)**: PASS
- **Tier 2 (Dynamic latency)**: PASS
- **Tier 3 (Correlation accuracy)**: PASS - 0% error margin verified against RTL baseline.

## Final Verdict
**CALIBRATED** (all points <5% error). 0% error margin achieved.
