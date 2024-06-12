import numpy as np
from pyqpp import *

# quantum circuit with 3 qubits and 2 classical bits
qc = qasm.read_from_file("/home/student/workspace/quantum/QASMBench/medium/dnn_n16/dnn_n16.qasm")
# initialize the quantum engine with a circuit
engine = QEngine(qc)

# display the quantum circuit and its corresponding resources
print(qc)
print()
print(qc.get_resources())
print()

# execute the entire circuit
engine.execute()

# display the measurement statistics
print(engine)
print()

# # verify that the teleportation was successful
# psi_in = np.matmul(U, states.z0)
# psi_out = engine.get_psi()
# print("Teleported state:\n", psi_out)
# print("Norm difference:\n", norm(psi_out - psi_in))