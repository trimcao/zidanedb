#include <CLI/CLI.hpp>
#include <iostream>
#include <string>

/*
Guidelines for main(): keep main() thin
A good main() should do approximately five things:

- Define the CLI.
- Parse and validate arguments.
- Construct the database.
- Dispatch to the selected command.
- Translate failures into messages and exit codes.

*/
int main(int argc, char** argv)
{
    CLI::App app{"A key-value storage engine"};
    app.set_version_flag("--version", "ZidaneDB 0.1.0");

    std::string database_path;

    app.add_option(
        "-d,--db",
        database_path,
        "Path to the database"
    )->required();

    std::string put_key;
    std::string put_value;
    auto* put_command =
        app.add_subcommand("put", "Store a key-value pair");
    put_command
        ->add_option("key", put_key, "Key to store")
        ->required();
    put_command
        ->add_option("value", put_value, "Value to store")
        ->required();

    std::string get_key;
    auto* get_command =
        app.add_subcommand("get", "Retrieve a value");
    get_command
        ->add_option("key", get_key, "Key to retrieve")
        ->required();

    std::string delete_key;
    auto* delete_command =
        app.add_subcommand("delete", "Delete a value");
    delete_command
        ->add_option("key", delete_key, "Key to delete")
        ->required();

    // The following means: only one subcommand could be used.
    // So only one among GET, PUT, DELETE could be used.
    app.require_subcommand(1, 1);

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& error) {
        return app.exit(error);
    }

    try {
        if (*put_command) {
            std::cout << std::format("Put {}={} to {}\n", put_key, put_value, database_path);
            // return run_put(database_path, put_key, put_value);
        }
        if (*get_command) {
            std::cout << std::format("Get {} from {}\n", get_key, database_path);
            // return run_get(database_path, put_key, put_value);
        }
        if (*delete_command) {
            std::cout << std::format("Delete {} from {}\n", delete_key, database_path);
            // return run_delete(database_path, put_key, put_value);
        }
    } catch (const std::exception& error) {
        std::cerr << "zidane: " << error.what() << '\n';
        return 1;
    }

    return 0;
}