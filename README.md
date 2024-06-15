# FlatDD
`FlatDD` achieves 34.81x speed-up and 1.93x memory reduction on commonly used quantum circuits compared to state-of-the-art quantum circuit simulators. 
Our computational artifact compares the performance of `FlatDD` with DD-based QCS `DDSIM` and array-based QCS `Quantum++` on 12 circuits. We also evaluate `FlatDD`'s scalability on different numbers of threads, `FlatDD`'s DD-to-array parallel conversion algorithm, `FlatDD`'s caching technique, and the DMAVM-aware gate-fusion algorithm. 
Our computational artifact includes `FlatDD`, `DDSIM`, `Quantum++` and the 12 circuits. 
All the third-party dependencies are packed in the artifact. 
Our computational artifact can reproduce all the experiments mentioned in the paper. 
We will make it open-source to benefit both HPC and the quantum computing community for accelerating quantum circuit simulation.

# Reproducibility of Experiments
## Environment
  (a) A Ubuntu 22.04.2 LTS machine with 64 Intel Xeon Gold 6226R CPUs at 2.9 GHz and 256~GB memory capacity.

  (b) `g++` compiler version 11.4.0.

  (c) `cmake` version 3.26.1.

  (d) `Python` version 3.8.3, `NumPy` version 1.22.4, `seaborn` version 0.13.2, `pandas` version 2.0.3 and `Matplotlib` version 3.7.5.

## File Hierarchy

  (a) `extern`: It contains all the third-party dependencies (`cxxopts`, `json`, and `taskflow`).

  (b) `include`: It contains the header files.

  (c) `src`: It contains the source code for the experiments.

  (d) `build`: It is a directory for building the executables.

  (e) `log`: It contains output logs from the experiments.

  (f) `apps`: It contains the two main function entrances for `FlatDD` and `DDSIM`.

  (g) `qpp`: It contains the code for `Quantum++`.
  
  (h) `circuits`: It contains the 12 quantum circuit files in `OpenQASM` language.

# Reference
+ [Advanced simulation of quantum computations](https://ieeexplore.ieee.org/abstract/document/8355954)
+ [Quantum++: A modern C++ quantum computing library](https://journals.plos.org/plosone/article?id=10.1371/journal.pone.0208073)