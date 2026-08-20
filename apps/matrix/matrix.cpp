#include "matrix.h"
#include <CLI/CLI.hpp>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>

int main(int argc, char** argv)
{
    CLI::App app{"Matrix: Tormentor of ZidaneDB"};
    app.set_version_flag("--version", "Matrix 0.1.0");

    std::string test_name;

    app.add_option(
        "--test",
        test_name,
        "Name of the test"
    )->required();

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& error) {
        return app.exit(error);
    }

    try {
        if (test_name == "basic")
            matrix::cli::test_basic();
    } catch (const std::exception& error) {
        std::cerr << "matrix: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
