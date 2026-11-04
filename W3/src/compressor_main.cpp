#include "command_line_interface.hpp"
#include "production_adapters.hpp"

#include <iostream>

int main(int argc, char* argv[]) {
    worklet3::LZ77BlockProducer producer;
    worklet3::OutputHistorySink decoder;
    worklet3::PipelineController pipeline(producer, decoder);
    return worklet3::CommandLineInterface::run(argc, argv, pipeline,
                                               std::cout, std::cerr);
}
