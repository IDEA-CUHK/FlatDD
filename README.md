# FlatDD
`FlatDD` achieves 34.81x speed-up and 1.93x memory reduction on commonly used quantum circuits compared to state-of-the-art quantum circuit simulators. 
Our computational artifact compares the performance of `FlatDD` with DD-based QCS `DDSIM` and array-based QCS `Quantum++` on 12 circuits. We also evaluate `FlatDD`'s scalability on different numbers of threads, `FlatDD`'s DD-to-array parallel conversion algorithm, `FlatDD`'s caching technique, and the DMAVM-aware gate-fusion algorithm. 
Our computational artifact includes `FlatDD`, `DDSIM`, `Quantum++` and the 12 circuits. 
All the third-party dependencies are packed in the artifact. 
Our computational artifact can reproduce all the experiments mentioned in the paper. 
We will make it open-source to benefit both HPC and the quantum computing community for accelerating quantum circuit simulation.

# Reference
+ [Advanced simulation of quantum computations](https://ieeexplore.ieee.org/abstract/document/8355954)
+ [Quantum++: A modern C++ quantum computing library](https://journals.plos.org/plosone/article?id=10.1371/journal.pone.0208073)