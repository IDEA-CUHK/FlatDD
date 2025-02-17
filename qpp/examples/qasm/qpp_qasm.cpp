// Executes an OpenQASM program read from the input stream, repeatedly if the
// number of repetitions is passed as the first argument. If there is a second
// argument (i.e., argc > 2), then the final quantum state is displayed.
// Source: ./examples/qasm/qpp_qasm.cpp
#include <iostream>
#include <string>
#include <omp.h>
#include "qpp.h"
#include <chrono>

int main(int argc, char** argv) {
    using namespace qpp;
    std::string filename;
    std::string output_pathname;
    std::string time_pathname;
    int num_cores = 0;
    // read the circuit from the input stream
    if (argc > 1) {
        filename = std::string(argv[1]);
        num_cores = std::stoi(argv[2]);                     // number of cores
        output_pathname = std::string(argv[3]);
        time_pathname = std::string(argv[4]);
    }
    else {
        std::cout<<"ERROR: arg input the file name";
        return 1;
    }
    omp_set_num_threads(num_cores);    
    QCircuit qc = qasm::read_from_file(filename);
    // initialize the quantum engine with a circuit
    QEngine q_engine{qc};
    

    // display the quantum circuit and its corresponding resources
    std::cout << qc << "\n\n" << qc.get_resources() << "\n\n";

    // execute the quantum circuit
    auto t1 = std::chrono::high_resolution_clock::now();
    std::cout<<"start execution ..."<<std::endl;
    q_engine.execute(); 
    auto t2 = std::chrono::high_resolution_clock::now();
    const std::chrono::duration<float> durationSimulation = t2 - t1;
    std::cout<<"time consumed: "<<durationSimulation.count()<<std::endl;
    // display the measurement statistics



    // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    // std::cout << q_engine << '\n';
    q_engine.output_finalstate(output_pathname);
    // +"/qpp_time.txt"
    q_engine.output_timerecord(time_pathname);



    // display the final state on demand
    // if (argc > 2) {
    //     std::cout << ">> Final state (transpose):\n";
    //     std::cout << disp(transpose(q_engine.get_psi())) << '\n';
    // }
}
