#include "benchmarking.hpp"
#include "container_format.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

using namespace std;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        cerr << "Usage: " << argv[0] << " <container.cmp2> [--csv | --json]\n";
        cerr << "  Reads and benchmarks all blocks inside the specified CMP2 container.\n";
        return 1;
    }

    const string path = argv[1];
    string format = "text";
    if (argc >= 3) {
        const string opt = argv[2];
        if (opt == "--csv") format = "csv";
        else if (opt == "--json") format = "json";
    }

    try {
        const cmp2::FileBenchmark b = cmp2::benchmark_container_file(path);

        if (format == "csv") {
            cout << cmp2::format_benchmark_csv(b);
        } else if (format == "json") {
            cout << cmp2::format_benchmark_json(b);
        } else {
            cmp2::print_benchmark_report(cout, b);
        }
        return 0;
    } catch (const cmp2::ContainerError& e) {
        cerr << "Container Error: " << e.what() << "\n";
        return 2;
    } catch (const exception& e) {
        cerr << "Error: " << e.what() << "\n";
        return 3;
    }
}
