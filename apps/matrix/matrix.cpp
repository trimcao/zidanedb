#include "matrix.h"
#include <CLI/CLI.hpp>
#include <iostream>
#include <string>

/* Notes on the use of subcommands

Subcommands have several advantages:

- Valid test names automatically appear in --help.
- Unknown tests are automatically rejected.
- Each test can have its own options.
- You avoid a growing string-based if chain.

*/

int main(int argc, char** argv) {
    CLI::App app{"Matrix: Tormentor of ZidaneDB"};
    app.set_version_flag("--version", "Matrix 0.1.0");

    auto* basic = app.add_subcommand("basic", "Verify persistence across reopening");

    std::size_t pair_count = 10'000;

    auto* perf_basic = app.add_subcommand("perf-basic", "Measure basic persistence performance");
    perf_basic->add_option("-n,--pairs", pair_count, "Number of pairs");

    app.require_subcommand(1, 1);

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& error) {
        return app.exit(error);
    }

    try {
        if (*basic) {
            return matrix::tests::run_basic();
        }

        if (*perf_basic) {
            return matrix::tests::run_perf_basic(pair_count);
        }
    } catch (const std::exception& error) {
        std::cerr << "matrix: " << error.what() << '\n';
        return 1;
    }

    return 1; // Defensive:: successful parsing should select a test.
}
