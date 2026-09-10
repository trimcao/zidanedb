#include "commands.h"
#include "zidanedb/database.h"
#include <CLI/CLI.hpp>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>

/*
Guidelines for main(): keep main() thin
A good main() should do approximately five things:

- Define the CLI.
- Parse and validate arguments.
- Construct the database.
- Dispatch to the selected command.
- Translate failures into messages and exit codes.

*/
int main(int argc, char** argv) {
    CLI::App app{"A key-value storage engine"};
    app.set_version_flag("--version", "ZidaneDB 0.1.0");

    std::string database_path;
    std::uint64_t num_index_buckets = 1'000'000;

    app.add_option("-d,--db", database_path, "Path to the database")->required();
    app.add_option("-b,--num-buckets", num_index_buckets,
                   "Number of hash buckets used by the index file");

    std::string put_key;
    std::string put_value;
    auto* put_command = app.add_subcommand("put", "Store a key-value pair");
    put_command->add_option("key", put_key, "Key to store")->required();
    put_command->add_option("value", put_value, "Value to store")->required();

    std::string get_key;
    auto* get_command = app.add_subcommand("get", "Retrieve a value");
    get_command->add_option("key", get_key, "Key to retrieve")->required();

    std::string delete_key;
    auto* delete_command = app.add_subcommand("delete", "Delete a value");
    delete_command->add_option("key", delete_key, "Key to delete")->required();

    // The following means: only one subcommand could be used.
    // So only one among GET, PUT, DELETE could be used.
    app.require_subcommand(1, 1);

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& error) {
        return app.exit(error);
    }

    // build the database
    std::filesystem::path path{database_path};

    try {
        zidanedb::Database db{path, num_index_buckets};

        if (*put_command) {
            return zidanedb::cli::run_put(db, std::move(put_key), std::move(put_value), std::cout);
        }
        if (*get_command) {
            return zidanedb::cli::run_get(db, get_key, std::cout, std::cerr);
        }
        if (*delete_command) {
            return zidanedb::cli::run_delete(db, delete_key, std::cout, std::cerr);
        }
    } catch (const std::exception& error) {
        std::cerr << "zidane: " << error.what() << '\n';
        return 1;
    }

    return 0;
}