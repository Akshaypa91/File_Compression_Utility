#pragma once

#include "pipeline_controller.hpp"

#include <iosfwd>

using namespace std;

namespace worklet3 {

class CommandLineInterface {
public:
    static int run(int argc,
                   char* argv[],
                   PipelineController& pipeline,
                   ostream& output,
                   ostream& errors);
    static string usage();
};

} // namespace worklet3