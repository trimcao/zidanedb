## Motivation
I want to move on from courseworks and artificial exercises like Leetcode and system designs interview preparation.
To be a true systems software engineer, I need to act like one.
This is not unlike tennis, I need to show up everyday, iterating on my own system, improving it, breaking it.


This will be a storage engine called ZidaneDB.
Why Zidane in the name? Zidane is a myth in football. He is revered by all fans around the world. He demands respect from me. I want to feel something when I open the repo `zidanedb`. I want to keep coming back to it. That's all.


This is a completely open-ended project. It will start as a could-not-be-simpler single-process key-value store. Over time, it could become a high-performance multi-threaded key-value store, or even a distributed storage engine, who knows?


One of the goals of this project is to discover the motivations for data structures, algorithms, solutions, ideas, papers, tools that could turn a simple, dumb key-value store into something serious.


## High-level Setup
The basic idea is having two cli apps in this repo: zidane and matrix.


`zidane` is the user-facing cli used to work with the database.


`matrix` is the adversarial app that wants to break `zidanedb`. It's solely goal is to torture `zidanedb`, and force `zidanedb` to evolve. To give some examples of what `matrix` can do: create/get/delete 1,000,000,000 entries, interact with the databases with 32 workers in parallel, crash `zidanedb` in the middle of a write/read and see if the data survive, and more. `matrix`, like Marco Materazzi, is not really a bad person. He just learns the torture schemes from the real world of databases, storage engines, and the software engineering industry as a whole. `zidanedb`, like the real Zidane, has to survive the French ghettos, grow up, train, fight, and evolve to become a legend.


Of course, there will be the usual logistical stuffs: CMake, unit-tests, even automated regression test after each commit. (To be completely honest, even though I have had a few years of working experience, I am still unfamiliar with many of  the industry-standard stuffs, so please bear with me. I am still learning.)

## Rules for AI Usage
- I will code every line by myself.
- I will use AI mainly to ideate, understand concepts, figure out C++ tidbits. I won't let AI make code changes. (This is not anti-AI, I just want this project to be fun and educational.)

## Q&As
1. **How to setup CMake? What is the CMakeLists.txt and what should it have?**

   CMake is the project's build-system description layer. It reads `CMakeLists.txt`, discovers the compiler and dependencies, then generates build instructions for Make, Ninja, Xcode, and other build tools. The file should describe logical targets—libraries, executables, tests—and how they depend on each other. Modern CMake is target-oriented rather than a collection of global compiler flags. See the [official build-system overview](https://cmake.org/cmake/help/latest/manual/cmake-buildsystem.7.html).

   This machine has Apple Clang 17 and Make, but CMake itself is not installed. Since Homebrew is available, install and check it with:

   ```sh
   brew install cmake
   cmake --version
   ```

   For the current repository, start with this top-level `CMakeLists.txt`:

   ```cmake
   cmake_minimum_required(VERSION 3.20)

   project(
       ZidaneDB
       VERSION 0.1.0
       LANGUAGES CXX
   )

   add_library(zidanedb
       src/db.cpp
       src/db.h
   )

   target_include_directories(zidanedb
       PUBLIC
           "${CMAKE_CURRENT_SOURCE_DIR}/src"
   )

   target_compile_features(zidanedb
       PUBLIC
           cxx_std_20
   )

   add_executable(zidane
       apps/zidane.cpp
   )

   target_link_libraries(zidane
       PRIVATE
           zidanedb
   )

   add_executable(matrix
       apps/matrix.cpp
   )

   target_link_libraries(matrix
       PRIVATE
           zidanedb
   )

   include(CTest)

   if(BUILD_TESTING)
       add_executable(db_test
           tests/db_test.cpp
       )

       target_link_libraries(db_test
           PRIVATE
               zidanedb
       )

       add_test(
           NAME db_test
           COMMAND db_test
       )
   endif()
   ```

   `cmake_minimum_required(VERSION 3.20)` establishes the oldest CMake version the build supports and selects the relevant CMake policy behavior. It should be the first command. The minimum is a compatibility floor, not necessarily the newest installed release. See the [official documentation](https://cmake.org/cmake/help/latest/command/cmake_minimum_required.html).

   `project(...)` names the project, assigns its version, and says that it uses C++.

   `add_library(zidanedb ...)` creates the storage-engine target. Both CLIs and the tests can reuse it instead of compiling their own copies of `db.cpp`.

   `target_include_directories(... PUBLIC src)` allows the library and anything linking it to write:

   ```cpp
   #include "db.h"
   ```

   `target_compile_features(... PUBLIC cxx_std_20)` tells CMake that the library's API requires at least C++20. CMake selects the appropriate compiler option automatically. This is preferable to hardcoding something like `-std=c++20`. See the [official target-features guidance](https://cmake.org/cmake/help/latest/guide/tutorial/In-Depth%20CMake%20Target%20Commands.html).

   `add_executable(...)` creates the two CLI programs and the test program. `target_link_libraries(... PRIVATE zidanedb)` expresses their dependency on the storage engine.

   The scope words are important:

   - `PRIVATE`: applies only to this target.
   - `PUBLIC`: applies to this target and propagates to targets that use it.
   - `INTERFACE`: applies only to consumers.

   `include(CTest)` creates the `BUILD_TESTING` option and enables CTest unless testing is disabled. `add_test()` registers the test executable with CTest. A test passes when it exits with code zero and fails when it exits with a nonzero code. See the [official CTest tutorial](https://cmake.org/cmake/help/latest/guide/tutorial/Testing%20and%20CTest.html).

   Use an out-of-source build so generated files do not pollute the repository:

   ```sh
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
   cmake --build build
   ctest --test-dir build --output-on-failure
   ```

   Then run the applications with:

   ```sh
   ./build/zidane
   ./build/matrix
   ```

   CMake officially recommends keeping the source tree and build tree separate. See the [CMake command-line documentation](https://cmake.org/cmake/help/latest/manual/cmake.1.html). The repository should eventually ignore the generated directory:

   ```gitignore
   /build/
   ```

   All the current `.cpp` files are empty. CMake configuration can succeed, but linking the executables will fail until `apps/zidane.cpp`, `apps/matrix.cpp`, and `tests/db_test.cpp` each define a `main()` function.

   A single top-level `CMakeLists.txt` is appropriate at this size. Splitting it into `src/CMakeLists.txt`, `apps/CMakeLists.txt`, and `tests/CMakeLists.txt` would add ceremony without helping yet.

2. **How to create and maintain unit tests?**

   For ZidaneDB, use:

   - CTest as the test runner.
   - Catch2 as the C++ assertion and test-case framework.
   - One test executable initially.
   - Separate unit, persistence, and adversarial tests as the project grows.

   CTest and Catch2 solve different problems. CTest launches test programs and reports their exit status; Catch2 supplies `TEST_CASE`, `REQUIRE`, diagnostics, filtering, and a test `main()`. CTest's `BUILD_TESTING` option is enabled by default when `include(CTest)` is called. See the [official CTest documentation](https://cmake.org/cmake/help/latest/module/CTest.html).

   ### CMake setup

   Replace the simple testing block from the earlier CMake answer with:

   ```cmake
   include(CTest)

   if(BUILD_TESTING)
       include(FetchContent)

       FetchContent_Declare(
           Catch2
           GIT_REPOSITORY https://github.com/catchorg/Catch2.git
           GIT_TAG        v3.15.0
       )

       FetchContent_MakeAvailable(Catch2)

       add_executable(db_tests
           tests/db_test.cpp
       )

       target_link_libraries(db_tests
           PRIVATE
               zidanedb
               Catch2::Catch2WithMain
       )

       list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
       include(Catch)
       catch_discover_tests(db_tests)
   endif()
   ```

   This pins [Catch2 v3.15.0](https://github.com/catchorg/Catch2/releases/tag/v3.15.0) rather than silently changing whenever Catch2 releases something new.

   `Catch2::Catch2WithMain` supplies the test program's `main()`, so `tests/db_test.cpp` should not define one. `catch_discover_tests()` registers each Catch2 test case separately with CTest, producing clearer reports and allowing individual tests to be selected. This is Catch2's recommended CMake integration. See the [official Catch2 CMake documentation](https://catch2-temp.readthedocs.io/en/latest/cmake-integration.html).

   The first configuration downloads Catch2 into the build directory. It does not place generated dependency files in the source tree.

   ### A first test

   The exact class and method names depend on the API, but a test might look like this:

   ```cpp
   #include <catch2/catch_test_macros.hpp>

   #include "db.h"

   TEST_CASE("put makes a value retrievable", "[db][put]")
   {
       Database db;

       db.put("player", "Zidane");
       const auto value = db.get("player");

       REQUIRE(value.has_value());
       CHECK(*value == "Zidane");
   }
   ```

   This follows Arrange-Act-Assert:

   1. Arrange: create the database and inputs.
   2. Act: call `put()` and `get()`.
   3. Assert: verify the observable result.

   `REQUIRE` aborts the current test case if it fails. That is appropriate before dereferencing `value`. `CHECK` records a failure but lets the test continue, which is useful for independent checks. See the [Catch2 assertion documentation](https://catch2-temp.readthedocs.io/en/latest/assertions.html).

   Avoid the standard C++ `assert()` for the test suite. It can be compiled out when `NDEBUG` is defined, and its failure diagnostics are limited.

   ### Running tests

   ```sh
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
   cmake --build build
   ctest --test-dir build --output-on-failure
   ```

   Useful variations include:

   ```sh
   # List registered tests without running them
   ctest --test-dir build -N

   # Run tests whose names match a pattern
   ctest --test-dir build -R "put" --output-on-failure

   # Run Catch2 tests carrying a tag
   ./build/db_tests "[db]"
   ```

   CTest can select tests by regular expression, while Catch2 can select them by names and tags. See the [CTest tutorial](https://cmake.org/cmake/help/latest/guide/tutorial/Testing%20and%20CTest.html).

   ### What to test first

   For the initial in-memory key-value store:

   1. A new database does not contain an unknown key.
   2. `put` makes a value retrievable.
   3. Putting the same key again replaces its value.
   4. Deleting an existing key makes it unavailable.
   5. Deleting a missing key has the documented result.
   6. Two different keys retain independent values.
   7. Empty keys and values behave according to the chosen contract.
   8. Large or binary keys and values behave according to the chosen contract.

   These tests force decisions about what the public API promises. For example, does deleting a missing key return `false`, succeed silently, or throw? The test should capture that decision, not make it accidentally.

   When persistence arrives, add integration tests such as:

   1. Write a value, close the database, reopen it, and read the value.
   2. Deleted values remain deleted after reopening.
   3. Overwritten values retain the newest committed value.
   4. A malformed or truncated database file produces a controlled error.
   5. Failed writes do not corrupt previously committed data.

   Crash injection, concurrency, huge workloads, and randomized operation sequences belong primarily to `matrix`. Those are system or adversarial tests rather than small unit tests.

   ### Maintaining the suite

   Good long-term rules are:

   - Test public behavior, not private implementation details. Replacing an `unordered_map` with a tree should not require rewriting behavioral tests.
   - Give every test a behavioral name, such as `"deleting a missing key returns false"`.
   - Keep tests isolated. A test must not depend on another test running first.
   - Give filesystem tests their own unique temporary directory and clean it up afterward.
   - Keep randomness reproducible. Record or print the seed whenever randomized testing fails.
   - Avoid sleeps and timing assumptions; they create flaky tests.
   - When fixing a bug, first add a test that reproduces it, observe it fail, then fix the bug.
   - Run fast tests on every change. Keep expensive stress tests in a separately tagged suite.
   - Treat test code like production code, but do not over-abstract it. A little repetition is often clearer than a complicated test helper.
   - Use coverage to find untested areas, not as a target to game.

   At the current size, keeping everything in `tests/db_test.cpp` is fine. Split it only when navigation becomes uncomfortable—for example, into `db_put_test.cpp`, `db_delete_test.cpp`, and `db_persistence_test.cpp`. Catch2 supports multiple source files in one test executable, so this later change only requires adding those files to `add_executable()`.

3. **How to create CLI apps in C++?**

   There is no single industry-standard C++ CLI library. The industry-standard approach is a set of practices:

   - Use an established argument parser.
   - Organize operations as subcommands.
   - Keep `main()` thin.
   - Keep database logic outside the CLI.
   - Make output and exit codes predictable for scripts.
   - Support `--help` and `--version`.
   - Test parsing separately from database behavior.

   For ZidaneDB, use [CLI11](https://github.com/CLIUtils/CLI11). It is portable, integrates cleanly with CMake, supports subcommands and validation, and does not require a runtime library. The stable release selected here is [v2.6.2](https://github.com/CLIUtils/CLI11/releases/tag/v2.6.2).

   ### Proposed interface

   With persistence implemented, the user-facing commands can be:

   ```sh
   zidane --db store.zdb put player Zidane
   zidane --db store.zdb get player
   zidane --db store.zdb delete player

   zidane --help
   zidane --version
   ```

   Later, `matrix` might look like:

   ```sh
   matrix sequential --db store.zdb --operations 100000 --seed 42
   matrix concurrent --db store.zdb --workers 32 --seed 42
   matrix crash --db store.zdb --after-operations 500
   ```

   Subcommands scale better than unrelated flags such as `--put`, `--get`, and `--delete`. CLI11 directly supports nested subcommands, required positional arguments, validators, and generated help. See the [CLI11 subcommand documentation](https://cliutils.github.io/CLI11/book/chapters/subcommands.html).

   ### Adding CLI11 with CMake

   Declare CLI11 as a dependency:

   ```cmake
   include(FetchContent)

   FetchContent_Declare(
       CLI11
       GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
       GIT_TAG        v2.6.2
   )

   FetchContent_MakeAvailable(CLI11)
   ```

   Link it only to the applications:

   ```cmake
   target_link_libraries(zidane
       PRIVATE
           zidanedb
           CLI11::CLI11
   )

   target_link_libraries(matrix
       PRIVATE
           zidanedb
           CLI11::CLI11
   )
   ```

   The storage-engine library should not depend on CLI11. Argument parsing is an application concern, not part of the database API. CLI11 officially exports the `CLI11::CLI11` CMake target. See the [CLI11 installation documentation](https://cliutils.github.io/CLI11/book/chapters/installation.html).

   ### Shape of `zidane.cpp`

   The following is illustrative because the database API does not exist yet:

   ```cpp
   #include <CLI/CLI.hpp>

   #include <iostream>
   #include <string>

   int main(int argc, char** argv)
   {
       CLI::App app{"A small key-value storage engine"};
       app.set_version_flag("--version", "ZidaneDB 0.1.0");

       std::string database_path;

       app.add_option(
           "-d,--db",
           database_path,
           "Path to the database"
       );

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
           app.add_subcommand("delete", "Delete a key");

       delete_command
           ->add_option("key", delete_key, "Key to delete")
           ->required();

       app.require_subcommand(1, 1);

       try {
           app.parse(argc, argv);
       } catch (const CLI::ParseError& error) {
           return app.exit(error);
       }

       try {
           if (*put_command) {
               return run_put(database_path, put_key, put_value);
           }

           if (*get_command) {
               return run_get(database_path, get_key);
           }

           if (*delete_command) {
               return run_delete(database_path, delete_key);
           }
       } catch (const std::exception& error) {
           std::cerr << "zidane: " << error.what() << '\n';
           return 1;
       }

       return 0;
   }
   ```

   `run_put`, `run_get`, and `run_delete` are placeholders for functions to design. The important architectural point is that parsing and dispatch are visible, while the actual work happens elsewhere.

   CLI11's parse errors can be passed to `app.exit()` to print an appropriate message and return the framework's corresponding exit code. See the [CLI11 error-handling documentation](https://cliutils.github.io/CLI11/book/chapters/basics.html).

   ### Keep `main()` thin

   A good `main()` should do approximately five things:

   1. Define the CLI.
   2. Parse and validate arguments.
   3. Construct the database.
   4. Dispatch to the selected command.
   5. Translate failures into messages and exit codes.

   It should not contain the implementation of `put`, `get`, or `delete`. Otherwise database behavior becomes difficult to test without launching a subprocess.

   A useful eventual shape is:

   ```cpp
   int run_put(const PutOptions&, std::ostream& out, std::ostream& err);
   int run_get(const GetOptions&, std::ostream& out, std::ostream& err);
   int run_delete(const DeleteOptions&, std::ostream& out, std::ostream& err);
   ```

   Passing streams makes the command layer testable using `std::ostringstream`, without redirecting global `std::cout` or launching the executable.

   ### Output conventions

   Treat stdout and stderr as separate APIs:

   - stdout contains the successful result.
   - stderr contains diagnostics, warnings, and errors.
   - Success returns exit code `0`.
   - Failure returns a nonzero exit code.
   - `--help` and `--version` print to stdout and return `0`.
   - Malformed arguments print to stderr and return nonzero.

   For example:

   ```sh
   value=$(zidane --db store.zdb get player)
   ```

   This only works reliably if `get` writes the value—and nothing else—to stdout. Logging messages such as `Opening database...` should go to stderr or be disabled unless verbose logging is requested.

   GNU CLI conventions recommend supporting `--help`, `--version`, consistent long option names, and `--` to terminate option parsing. See the [GNU command-line interface guidelines](https://www.gnu.org/prep/standards/html_node/Command_002dLine-Interfaces).

   ### Argument conventions

   Use:

   - Lowercase subcommand names: `put`, `get`, `delete`.
   - Long descriptive options: `--db`, `--workers`, `--seed`.
   - Short options only when obvious and frequently used: `-d`, `-v`.
   - Positional arguments for the command's central operands.
   - Options for configuration or modifiers.

   Quoted shell arguments naturally support spaces:

   ```sh
   zidane --db store.zdb put greeting "hello world"
   ```

   Raw binary values cannot be represented reliably as ordinary command-line arguments because arguments cannot contain null bytes. Later, consider interfaces such as:

   ```sh
   zidane --db store.zdb put-file key ./value.bin
   zidane --db store.zdb put key --value-from-stdin
   ```

   ### Testing the CLI

   Use two levels:

   1. Command-layer unit tests call functions such as `run_get()` with in-memory streams.
   2. End-to-end tests launch the compiled `zidane` executable and inspect its exit code, stdout, and stderr.

   Test cases should cover:

   - No subcommand.
   - Unknown subcommand.
   - Missing required argument.
   - `--help` and `--version`.
   - Successful command.
   - Missing key.
   - Invalid database path.
   - Values containing spaces.
   - Stable stdout and stderr behavior.

   Eventually, `matrix` should launch the real `zidane` process for crash and recovery tests. Calling the library directly cannot accurately test process termination, exit codes, buffering, or crash durability.

   In short: use CLI11 for syntax and validation, keep the database library independent, make `main()` a thin adapter, and design stdout, stderr, and exit codes as stable interfaces.

   ### Should the first version have a shell?

   No. Because persistence will be implemented immediately, start with one-shot commands and skip the shell:

   ```sh
   zidane --db store.zdb put player Zidane
   zidane --db store.zdb get player
   zidane --db store.zdb delete player
   ```

   This is better for developing persistence because every operation crosses a process boundary:

   ```text
   put process opens DB -> writes -> closes
   get process opens DB -> reconstructs state -> reads -> closes
   ```

   A shell keeps the database object alive and can accidentally conceal persistence bugs. `put` followed by `get` inside one process might succeed because the value remains in memory, even if nothing was stored correctly.

   The initial milestone should therefore be:

   1. `put` opens or creates the database, stores the value, and exits successfully.
   2. A completely separate `get` invocation opens the database and retrieves it.
   3. `delete` persists across another process restart.
   4. Errors have stable nonzero exit codes.
   5. `matrix` can execute these commands repeatedly.

   For example:

   ```sh
   zidane --db test.zdb put number-ten Zidane
   zidane --db test.zdb get number-ten
   zidane --db test.zdb delete number-ten
   zidane --db test.zdb get number-ten
   ```

   `Survives a normal process restart` and `survives a machine crash` are different durability guarantees. Initially, target normal reopen persistence. Later, `matrix` can force crashes and motivate flushing, `fsync`, atomic updates, checksums, recovery, and related mechanisms.

   Add `zidane shell` later if repeated interactive use becomes annoying. It should reuse the same database API and command handlers, not introduce separate implementations of `put`, `get`, and `delete`.

   The clean progression is:

   ```text
   one-shot CLI -> reopen persistence -> adversarial tests -> optional shell
   ```

   This keeps the first interface smaller and makes persistence correctness impossible to fake accidentally.

4. **What should be the most basic source files and header files organization for this project?**

   The current layout is already close. Make one structural improvement: put the storage engine's public headers under `include/zidanedb/`, while keeping implementation files and private headers under `src/`.

   ### Recommended initial structure

   ```text
   zidanedb/
   ├── CMakeLists.txt
   ├── README.md
   ├── .gitignore
   ├── include/
   │   └── zidanedb/
   │       └── database.h
   ├── src/
   │   └── database.cpp
   ├── apps/
   │   ├── zidane.cpp
   │   └── matrix.cpp
   ├── tests/
   │   └── database_test.cpp
   └── notes/
       ├── 00-motivation-and-setup.md
       └── 01-forgetfulness.md
   ```

   The existing names `db.h`, `db.cpp`, and `db_test.cpp` are also fine. `database.*` is simply a little more explicit. The important part is the directory boundary.

   ### What each directory owns

   `include/zidanedb/` contains the public storage-engine API:

   ```cpp
   #include "zidanedb/database.h"
   ```

   This is what `zidane`, `matrix`, tests, and any future consumers are allowed to include.

   `src/` contains implementation details:

   - Method definitions.
   - File-format encoding and decoding.
   - Internal filesystem operations.
   - Private helper types.
   - Anything consumers should not depend on.

   `apps/` contains executable-specific code:

   - CLI11 argument definitions.
   - stdout and stderr formatting.
   - Exit-code decisions.
   - Command dispatch.
   - `main()`.

   `tests/` tests the public storage-engine behavior. Initially, one test file is enough.

   `notes/` contains the project journal, design reasoning, discoveries, and postmortems.

   ### Public interface versus implementation

   The public header should describe what ZidaneDB does, not how it does it:

   ```cpp
   #ifndef ZIDANEDB_DATABASE_H
   #define ZIDANEDB_DATABASE_H

   namespace zidanedb {

   class Database {
   public:
       // Public operations and their contracts.

   private:
       // Representation needed by the class.
   };

   }  // namespace zidanedb

   #endif
   ```

   The exact API should follow the design decisions about paths, missing keys, errors, ownership, and durability.

   The implementation file starts by including its own header:

   ```cpp
   #include "zidanedb/database.h"

   // Standard-library and other includes follow.

   namespace zidanedb {

   // Database method definitions.

   namespace {

   // Helpers used only by this implementation file.

   }  // namespace

   }  // namespace zidanedb
   ```

   Including the corresponding header first helps expose missing dependencies in the header. Public headers should be self-contained: a caller should be able to include one without relying on some other header having been included first. See the [Google C++ header guidance](https://google.github.io/styleguide/cppguide.html#Header_Files).

   Using `.h` or `.hpp` has no language-level difference. The project already uses `.h`, so keeping it is reasonable.

   ### Matching CMake setup

   The storage-engine target becomes:

   ```cmake
   add_library(zidanedb
       src/database.cpp
       include/zidanedb/database.h
   )

   target_include_directories(zidanedb
       PUBLIC
           "${CMAKE_CURRENT_SOURCE_DIR}/include"
   )

   target_compile_features(zidanedb
       PUBLIC
           cxx_std_20
   )
   ```

   Then every consumer links to `zidanedb`:

   ```cmake
   target_link_libraries(zidane PRIVATE zidanedb CLI11::CLI11)
   target_link_libraries(matrix PRIVATE zidanedb CLI11::CLI11)
   target_link_libraries(db_tests PRIVATE zidanedb Catch2::Catch2WithMain)
   ```

   Because the include directory is `PUBLIC`, CMake propagates it to targets that link against `zidanedb`. The executables should not manually add `include/` to their include paths. That dependency travels with the library target. See the [CMake `target_include_directories` documentation](https://cmake.org/cmake/help/latest/command/target_include_directories.html).

   ### Keep the applications small

   Initially, both application files can remain single files:

   ```text
   apps/zidane.cpp
   apps/matrix.cpp
   ```

   `zidane.cpp` should contain CLI construction and dispatch, but not database implementation.

   Do not immediately create files such as:

   ```text
   put_command.cpp
   get_command.cpp
   delete_command.cpp
   cli_helpers.cpp
   common.cpp
   utils.cpp
   ```

   That would add navigation and abstraction before the program has enough complexity to justify it.

   If `zidane.cpp` later becomes uncomfortable to navigate, a reasonable evolution is:

   ```text
   apps/
   └── zidane/
       ├── main.cpp
       ├── commands.cpp
       └── commands.h
   ```

   Likewise, `matrix.cpp` can eventually become:

   ```text
   apps/
   └── matrix/
       ├── main.cpp
       ├── sequential.cpp
       ├── concurrent.cpp
       └── crash.cpp
   ```

   Only make those splits when the responsibilities actually exist.

   ### Private headers

   Do not create private headers merely to move code around. If a helper is used only by `database.cpp`, place it in that file's anonymous namespace.

   When multiple implementation files genuinely need the same internal type, add a private header under `src/`:

   ```text
   src/
   ├── database.cpp
   ├── record_codec.cpp
   ├── record_codec.h
   ├── append_log.cpp
   └── append_log.h
   ```

   Those headers should not be added to the library's public include directory. Applications should never write:

   ```cpp
   #include "../src/record_codec.h"
   ```

   If an application needs an internal header, either the functionality belongs in the public API or the application is crossing the wrong boundary.

   ### When to create a new source-file pair

   Split code when a responsibility:

   - Has a clear name.
   - Has its own invariant or contract.
   - Can be tested or reasoned about independently.
   - Is used by multiple implementation files.
   - Changes for different reasons than the surrounding code.

   Persistence might eventually motivate components such as a record codec, append-only log, file wrapper, checksum implementation, or recovery procedure. Let the problems reveal those boundaries instead of creating them prospectively.

   Avoid generic dumping grounds named `utils`, `common`, or `helpers`. They tend to accumulate unrelated code without a coherent contract.

   ### Test organization

   Start with:

   ```text
   tests/database_test.cpp
   ```

   When it becomes difficult to navigate, split by behavior:

   ```text
   tests/
   ├── database_put_test.cpp
   ├── database_delete_test.cpp
   ├── database_persistence_test.cpp
   └── database_recovery_test.cpp
   ```

   All of them can still compile into the same `db_tests` executable.

   Tests should include the public header:

   ```cpp
   #include "zidanedb/database.h"
   ```

   That ensures tests exercise the same API as real consumers instead of becoming coupled to private implementation details.

   Do not store test database files permanently in the repository. Each persistence test should create an isolated temporary directory and clean it up afterward.

   ### The central rule

   Keep three boundaries clear:

   ```text
   CLI applications -> public database API -> private storage implementation
   ```

   `zidane` and `matrix` may know about `Database`. They should not know how records are encoded. `Database` should not know about CLI11, terminal output, Catch2, or command-line exit codes.

   That small separation is enough for the beginning. Add more structure only when ZidaneDB itself gives a concrete reason.

## Appendix

### How FetchContent Retrieves Libraries from GitHub

`FetchContent` treats a GitHub repository as source code that becomes part of your CMake build. GitHub itself is not special; it is simply a Git repository URL.

For example:

```cmake
include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        <commit-hash>
)

FetchContent_MakeAvailable(Catch2)
```

Here is what happens:

1. `FetchContent_Declare()` records a download recipe. It does not download anything yet.
2. When you configure the project:

   ```sh
   cmake -S . -B build
   ```

   `FetchContent_MakeAvailable(Catch2)` causes CMake to clone the repository and check out the requested revision.
3. By default, the downloaded files are placed under:

   ```text
   build/_deps/catch2-src/
   ```

   Its generated build files go somewhere like:

   ```text
   build/_deps/catch2-build/
   ```

4. CMake reads Catch2's own `CMakeLists.txt`, approximately as if you had called `add_subdirectory()` on it.
5. Catch2's CMake targets, such as `Catch2::Catch2WithMain`, become available to your project.
6. You can then link against one of those targets:

   ```cmake
   target_link_libraries(db_test
       PRIVATE
           zidanedb
           Catch2::Catch2WithMain
   )
   ```

The target carries information such as include directories, compiler requirements, and dependent libraries, so you normally do not manually specify Catch2's header paths.

The important distinction is:

```text
CMake configure
    ↓
clone/download dependency
    ↓
read dependency's CMakeLists.txt
    ↓
generate the complete build system
    ↓
CMake build
    ↓
compile your code and required dependency targets
```

Fetching occurs during the configure step, not normally during `cmake --build`. This allows the dependency's targets to exist while CMake is generating your project. [CMake's FetchContent documentation](https://cmake.org/cmake/help/latest/module/FetchContent.html) describes this as making content available at configure time.

#### What happens on subsequent builds?

CMake ordinarily reuses the dependency already stored in the build directory:

```sh
cmake --build build
```

This does not clone Catch2 again.

If you delete `build/`, the downloaded dependency disappears too, and the next configure will download it again. Nothing is installed globally, and nothing is added to your source repository.

That is one reason `build/` belongs in `.gitignore`.

#### What should `GIT_TAG` contain?

Despite its name, `GIT_TAG` can specify:

- A branch, such as `main`.
- A tag, such as `v3.15.0`.
- A full commit hash.

For reproducible builds, a full commit hash is the strongest choice:

```cmake
GIT_TAG 4f3c2a...full-commit-hash...
```

A branch moves continuously, and even a Git tag can technically be moved. A commit hash identifies one exact source revision. CMake's documentation also recommends commit hashes for remote content because they are more reproducible and secure.

A readable compromise is:

```cmake
GIT_TAG abcdef0123456789... # v3.15.0
```

The hash controls the build; the comment tells humans which release it represents.

#### Does it install the library?

No. `FetchContent` generally:

- Downloads the library into your build tree.
- Adds it as a subproject.
- Builds required targets alongside your project.

It does not install the library into `/usr/local`, Homebrew, or a global C++ environment.

For ZidaneDB, that means a fresh checkout can obtain Catch2 and CLI11 simply by running CMake, provided Git and network access are available. The dependency sources remain disposable build artifacts. This build-from-source approach is the main difference between `FetchContent` and `find_package()`, which commonly searches for an already-installed dependency. [CMake's dependency guide](https://cmake.org/cmake/help/latest/guide/using-dependencies/index.html) compares the two approaches.
