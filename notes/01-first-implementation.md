# First Implementation of my Storage Engine

## Motivation
Implement a very, very basic key-value store. Make sure the database lib code works, the `zidane` cli and `matrix` cli work. Learn to add some unit tests.

## Takeaways (my own words)
- C++ is not like C at all.
- Modern C++ is pretty "modern" (boring). I can definitely see why C++ is still heavily used today.
- References in C++ are similar to, but not quite, pointers.
- `std::move()` is important regarding ownership. Yes, I am learning serious programming now.
- This project will be a hell of a ride.

## Lessons

### 1. Why does `std::filesystem` not exist on my setup?

`std::filesystem` does exist on this setup. It was added to the C++ standard library in C++17 and is provided by the `<filesystem>` header.

The problem is the **C++ language mode** used when the header is compiled. On this machine, Apple Clang 17 produces this result:

```sh
c++ -fsyntax-only -x c++ include/zidanedb/database.h
```

```text
error: no member named 'filesystem' in namespace 'std'
```

The compiler's default mode is older than C++17. Both of these commands successfully compile the header:

```sh
c++ -std=c++17 -fsyntax-only -x c++ include/zidanedb/database.h
c++ -std=c++20 -fsyntax-only -x c++ include/zidanedb/database.h
```

The project's CMake configuration already requests C++20 correctly:

```cmake
target_compile_features(zidanedb
    PUBLIC
        cxx_std_20
)
```

CMake will add the compiler's standard-selection flag when one is needed. Because `database.h` is part of the library's public interface, `PUBLIC` is appropriate: targets that consume the header also inherit the C++20 requirement.

Therefore, build the project through CMake:

```sh
cmake -S . -B build
cmake --build build
```

If an editor still underlines `std::filesystem`, its code analyzer is probably not using the CMake configuration and may still be parsing as an older version of C++. Configure that editor or language server to use the project's CMake compile settings.

The C++ standards committee adopted the filesystem library into C++17 in [P0218R1](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2016/p0218r1.html). The [CMake target commands guide](https://cmake.org/cmake/help/latest/guide/tutorial/In-Depth%20CMake%20Target%20Commands.html) explains how `target_compile_features()` selects a target's minimum language standard.

### 2. What is `[[nodiscard]]`?

`[[nodiscard]]` is a standard C++ attribute that tells the compiler that ignoring a result is probably a mistake.

For example:

```cpp
[[nodiscard]]
std::optional<std::string> get(const std::string& key) const;
```

This call ignores the returned value:

```cpp
database.get("player");
```

The compiler will normally issue a warning because calling `get()` without examining its result does nothing useful.

This call uses the result:

```cpp
auto player = database.get("player");
```

`[[nodiscard]]` does not change how the function runs, and ignoring the result is not automatically a compilation error. It gives the compiler information that it can use to diagnose a likely bug.

It is especially useful for:

- A lookup result that says whether a value exists.
- A status or error result that the caller should inspect.
- A value whose entire purpose is to be consumed by the caller.

It is less useful on a function returning `void`, since there is no result to discard.

### 3. How does `std::optional` work?

`std::optional<T>` represents either:

- One value of type `T`.
- No value.

Include it with:

```cpp
#include <optional>
```

For this database:

```cpp
std::optional<std::string> get(const std::string& key) const;
```

the two possible states are:

```cpp
return it->second;       // contains a string
return std::nullopt;     // contains no string
```

A possible implementation is:

```cpp
std::optional<std::string> Database::get(const std::string& key) const
{
    auto it = data_.find(key);

    if (it == data_.end()) {
        return std::nullopt;
    }

    return it->second;
}
```

The caller can check it like this:

```cpp
auto player = database.get("player");

if (player.has_value()) {
    std::cout << player.value() << '\n';
} else {
    std::cout << "key not found\n";
}
```

A common shorter form is:

```cpp
if (player) {
    std::cout << *player << '\n';
}
```

The important operations are:

| Expression | Meaning |
|---|---|
| `player.has_value()` | Returns whether a value is present. |
| `if (player)` | Another way to test whether a value is present. |
| `*player` | Accesses the value; only use after confirming it exists. |
| `player.value()` | Accesses the value and throws `std::bad_optional_access` if absent. |
| `player.value_or("unknown")` | Returns the value or the supplied fallback. |
| `std::nullopt` | Represents the no-value state. |

This solves an ambiguity in a plain `std::string` return value. Without `optional`, an empty string could mean either:

- The key was found and its value was `""`.
- The key was not found.

With `optional`, those states are distinct:

```cpp
return std::string{};   // found, and the stored value is empty
return std::nullopt;    // not found
```

Conceptually, an `optional` contains its value plus knowledge of whether that value is present. It is not a pointer, and an `optional<std::string>` returned by value does not refer back to the map entry. The C++ standards committee's original [`optional` proposal](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2013/n3527.html) describes it as a type that can answer whether it contains a `T` and provide that value when present.

### 4. What does `const` at the end of `get()` mean?

Consider:

```cpp
std::optional<std::string> get(const std::string& key) const;
```

The final `const` applies to the `Database` object on which the member function is called. It promises that `get()` will not modify that object's ordinary data members.

Inside a non-static member function, C++ supplies an implicit pointer named `this`. A useful mental model is:

```cpp
// C++ member function
value = database.get(key);

// Rough C-like mental model
value = database_get(&database, key);
```

For a trailing-`const` member function, the implicit object parameter behaves roughly like a pointer to const:

```cpp
database_get(const Database* database, const std::string& key);
```

Therefore, this is not allowed inside `get() const`:

```cpp
data_[key] = "changed";
```

Reading from `data_` is allowed:

```cpp
auto it = data_.find(key);
```

The trailing `const` also allows the function to be called through a const database object:

```cpp
const Database database{"store.zdb"};
auto player = database.get("player");
```

A non-const member function such as `put()` cannot normally be called on that object.

The `const` must appear in both the declaration and definition:

```cpp
// database.h
std::optional<std::string> get(const std::string& key) const;

// database.cpp
std::optional<std::string> Database::get(const std::string& key) const
{
    // ...
}
```

The two occurrences of `const` in this signature have separate jobs:

```cpp
get(const std::string& key) const
    ^                         ^
    |                         |
    do not modify key         do not modify this Database
```

### 5. Is `std::string& key` the C++ equivalent of `std::string* key` in C?

They are related forms of indirection, but they are not the same language construct.

First, `std::string` itself is a C++ standard-library type and does not exist in C. The exact pointer spelling below is therefore also C++, although its pointer behavior resembles C:

```cpp
std::string* key;          // pointer to a string
std::string& key_ref = s;  // reference to a string
```

A C++ reference acts as another name, or alias, for an existing object:

```cpp
void uppercase(std::string& text)
{
    // Modifications to text affect the caller's string.
}

std::string name = "zidane";
uppercase(name);
```

A pointer requires explicit address-taking and dereferencing:

```cpp
void uppercase(std::string* text)
{
    if (text == nullptr) {
        return;
    }

    // Access with *text or text->...
}

std::string name = "zidane";
uppercase(&name);
```

The major differences are:

| Reference: `T&` | Pointer: `T*` |
|---|---|
| Must refer to an existing object when created. | May be `nullptr`. |
| Cannot be reseated to refer to a different object. | Can be assigned another address. |
| Used with ordinary object syntax: `key.size()`. | Requires pointer syntax: `key->size()` or `(*key).size()`. |
| Caller passes `key`. | Caller passes `&key`. |
| Usually expresses a required, non-owning object. | Can express an optional object, array traversal, or other pointer semantics. |

These three parameter declarations also mean different things:

| Declaration | Meaning |
|---|---|
| `std::string key` | Make a new parameter object by copying or moving the argument. |
| `std::string& key` | Borrow the caller's string and allow it to be modified. |
| `const std::string& key` | Borrow the caller's string without copying and do not modify it. |

For `get()`, the database only needs to inspect the key, so this communicates the intent well:

```cpp
get(const std::string& key)
```

Read it from right to left as: **`key` is a reference to a const `std::string`**. This spelling means the same thing:

```cpp
get(std::string const& key)
```

Passing `put()` parameters by value can still be reasonable because the database needs to store them:

```cpp
void put(std::string key, std::string value);
```

The implementation can then move those parameter objects into the map rather than copying them again:

```cpp
data_.insert_or_assign(std::move(key), std::move(value));
```

That requires `<utility>` for `std::move`. The important principle is not to change every parameter mechanically to a reference. Choose based on intent:

- Borrow and read: `const T&`.
- Borrow and modify the caller's object: `T&`.
- Accept no object as a valid possibility: often `T*` or a more specific nullable type.
- Take or create your own value: often `T` by value.

The C++ standards committee paper [N3538](https://www.open-std.org/JTC1/SC22/WG21/docs/papers/2013/n3538.html) summarizes const-reference parameters as combining pass-by-value-like syntax with indirect access that avoids copying.

### 6. How do I fix `std::filesystem` errors in VS Code?

The project itself is configured correctly. CMake's generated flags for `zidanedb` contain:

```text
-std=gnu++20
```

The editor error comes from clangd, the C++ language server installed in VS Code. clangd needs to know the same include paths, language standard, definitions, and other flags that CMake uses to compile each source file.

The standard way to provide that information is a **compilation database** named `compile_commands.json`.

#### Step 1: Generate the compilation database

Configure the existing build directory with:

```sh
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

This creates:

```text
build/compile_commands.json
```

The file records the exact compiler command for every C++ translation unit. In this project, that includes the required `-std=gnu++20` flag. CMake documents the option in [`CMAKE_EXPORT_COMPILE_COMMANDS`](https://cmake.org/cmake/help/latest/variable/CMAKE_EXPORT_COMPILE_COMMANDS.html).

#### Step 2: Tell VS Code's clangd where the file is

Create `.vscode/settings.json` if it does not already exist:

```json
{
    "clangd.arguments": [
        "--compile-commands-dir=${workspaceFolder}/build"
    ]
}
```

This tells clangd to read `build/compile_commands.json` instead of guessing how the project is compiled.

The [official clangd VS Code extension documentation](https://github.com/clangd/vscode-clangd) recommends generating `compile_commands.json` with CMake so clangd receives the project's real compile flags.

#### Step 3: Restart clangd

After saving the setting and configuring CMake:

1. Open the VS Code command palette with `Cmd+Shift+P`.
2. Run **clangd: Restart language server**.
3. If that command is not present, run **Developer: Reload Window**.

The false `std::filesystem` error should then disappear.

#### How to verify the fix

Open VS Code's **Output** panel and select **clangd**. Its log should say that it loaded a compilation database from the project's `build` directory. The inferred compile command should contain a C++20 flag such as:

```text
-std=gnu++20
```

If the real CMake build succeeds while VS Code still shows a red underline, the underline remains an editor-analysis problem rather than a compiler error.

Avoid fixing this by adding only a hard-coded `-std=c++20` fallback flag. A compilation database is better because it also communicates include directories and target-specific options, and it stays synchronized with the CMake project.

### 7. A brief introduction to `std::filesystem`

`std::filesystem` is the part of the C++ standard library for representing paths and interacting with files and directories. It became part of standard C++ in C++17.

Include it:

```cpp
#include <filesystem>

// Convenient inside a .cpp file:
namespace fs = std::filesystem;
```

Inside an implementation file, you can optionally create the shorter alias `namespace fs = std::filesystem;`. In a public header, spelling out `std::filesystem` keeps the interface explicit and avoids introducing a global alias into every including file.

Its central type is `std::filesystem::path`:

```cpp
fs::path database_path{"data/store.zdb"};
```

A `path` represents a filesystem path. Constructing one does not create or open the corresponding file, and the path does not have to exist yet.

Compared with storing a path as a plain `std::string`, `fs::path` communicates intent and provides path-specific operations:

```cpp
fs::path directory{"data"};
fs::path database_path = directory / "store.zdb";

database_path.filename();      // "store.zdb"
database_path.parent_path();   // "data"
database_path.extension();     // ".zdb"
```

The `/` operator joins path components using the conventions of the current operating system.

The namespace also provides functions for inspecting and changing the filesystem:

```cpp
fs::exists(database_path);
fs::is_regular_file(database_path);
fs::file_size(database_path);
fs::create_directories("data/backups");
fs::rename("old.zdb", "new.zdb");
fs::remove(database_path);
```

Directory contents can be traversed with `fs::directory_iterator` or `fs::recursive_directory_iterator`.

For ZidaneDB, an early use is storing the database location:

```cpp
class Database {
public:
    explicit Database(std::filesystem::path path);

private:
    std::filesystem::path path_;
};
```

The constructor might receive `"data/store.zdb"`, store it in `path_`, and then use that path with file streams after including `<fstream>`:

```cpp
std::ifstream input{path_, std::ios::binary};
std::ofstream output{path_, std::ios::binary | std::ios::app};
```

Most filesystem operations have two error-handling styles. One throws `std::filesystem::filesystem_error` on failure:

```cpp
auto size = fs::file_size(path_);
```

Another accepts `std::error_code` and reports the error without throwing:

```cpp
std::error_code error;
auto size = fs::file_size(path_, error);

if (error) {
    // Handle the failure.
}
```

One important boundary: `std::filesystem` helps with paths and filesystem operations, but it does not provide a database format, record serialization, buffering policy, crash recovery, or durability guarantees. Those remain responsibilities of ZidaneDB's storage implementation.

The C++ standards committee adopted the library into C++17 through [P0218R1](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2016/p0218r1.html).

### 8. What is a namespace in C++?

A namespace is a named scope used to organize declarations and prevent name collisions.

Suppose two libraries both define a class named `Database`. Without separate namespaces, those names would conflict. Namespaces give each class a distinct fully qualified name:

```cpp
namespace zidanedb {
class Database {};
}

namespace analytics {
class Database {};
}
```

The two types are:

```cpp
zidanedb::Database
analytics::Database
```

The `::` token is the **scope-resolution operator**. It means “look for the name on the right inside the scope on the left.”

The relevant name hierarchy in this project looks roughly like:

```text
global namespace
├── std
│   ├── string
│   ├── optional
│   └── filesystem
│       └── path
└── zidanedb
    └── Database
```

Therefore:

```cpp
std::string
std::optional
std::filesystem::path
zidanedb::Database
```

are qualified names identifying exactly which scope contains each name.

#### Declaring and using a namespace

The public header places `Database` inside the project's namespace:

```cpp
namespace zidanedb {

class Database {
    // ...
};

}  // namespace zidanedb
```

Code outside that namespace uses the qualified name:

```cpp
zidanedb::Database database{"store.zdb"};
```

Code already inside `namespace zidanedb` can use the shorter name `Database` because it is already looking in the correct scope.

A namespace can be reopened. The declarations do not have to appear in one continuous block:

```cpp
namespace zidanedb {
class Database;
}

namespace zidanedb {
class Options;
}
```

Both classes belong to the same `zidanedb` namespace. This is how declarations spread across multiple headers and source files can share one project namespace.

#### Defining a member function in the source file

There are two common spellings. The first uses the fully qualified class name:

```cpp
std::optional<std::string>
zidanedb::Database::get(const std::string& key) const
{
    // ...
}
```

The second reopens the namespace:

```cpp
namespace zidanedb {

std::optional<std::string>
Database::get(const std::string& key) const
{
    // ...
}

}  // namespace zidanedb
```

They define the same function. The second style is convenient when a source file contains several definitions from the same namespace.

#### What is `namespace std`?

The C++ standard library places its declarations in the namespace named `std`:

```cpp
std::string
std::unordered_map
std::filesystem::path
```

This keeps standard-library names separate from names defined by applications and other libraries. Application code should not place its own ordinary declarations inside `namespace std`; that namespace belongs to the standard library.

#### Namespace aliases

A namespace alias gives an existing namespace a shorter alternate name:

```cpp
namespace fs = std::filesystem;

fs::path path{"store.zdb"};
```

`fs` is not a copy of `std::filesystem` and does not create another namespace. It is simply another name for the same namespace.

Aliases are useful in implementation files when a namespace is long and used frequently. Avoid placing broad convenience aliases in public headers because every file including the header would see them.

#### `using` declarations and directives

A **using declaration** brings one specific name into the current scope:

```cpp
using std::string;

string key;
```

A **using directive** makes names from an entire namespace available for unqualified lookup:

```cpp
using namespace std;

string key;
filesystem::path path;
```

Avoid `using namespace std;` in headers. It affects every file that includes the header and can create collisions or make it unclear where names originate. Explicit names such as `std::string` are clearer in a public API.

Using a single selected name in a limited implementation scope is less risky, but fully qualified names are often still the clearest choice.

#### Unnamed namespaces

An unnamed namespace makes declarations private to one translation unit, normally one `.cpp` file:

```cpp
namespace {

constexpr int record_version = 1;

bool is_valid_record(/* ... */)
{
    // ...
}

}  // namespace
```

This is useful for helper functions, constants, and implementation details used only by `database.cpp`. Do not put such helpers in the public `zidanedb` API merely so the implementation can name them.

#### Namespaces and directories are independent

A source directory does not automatically create a C++ namespace. The directory:

```text
include/zidanedb/
```

and the declaration:

```cpp
namespace zidanedb {
}
```

are connected only by project convention. The filesystem organizes files; C++ namespace declarations organize program names.

For this project, the practical rule is:

- Put the public database API in `namespace zidanedb`.
- Keep standard-library names qualified with `std::`.
- Use a namespace alias such as `fs` only in a limited implementation scope.
- Put `.cpp`-only helpers in an unnamed namespace.
- Avoid `using namespace std;` in headers.

The standard's namespace-alias syntax is described by the C++ standards committee, including in [N1344](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2002/n1344.pdf), and nested namespace declarations were standardized through [N4230](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2014/n4230.html).

### 9. How do I define `Database` methods in `database.cpp`?

The header declares the class inside `namespace zidanedb`. The source file can reopen that same namespace and define each member there:

```cpp
#include "zidanedb/database.h"

#include <utility>

namespace zidanedb {

Database::Database(std::filesystem::path path)
    : path_(std::move(path))
{
}

std::optional<std::string>
Database::get(const std::string& key) const
{
    auto it = data_.find(key);

    if (it == data_.end()) {
        return std::nullopt;
    }

    return it->second;
}

void Database::put(std::string key, std::string value)
{
    data_.insert_or_assign(std::move(key), std::move(value));
}

bool Database::erase(const std::string& key)
{
    return data_.erase(key) != 0;
}

}  // namespace zidanedb
```

This is only an in-memory implementation. It stores the path but does not yet load or write persistent data.

The important names are `Database::Database` for the constructor and `Database::get`, `Database::put`, and `Database::erase` for the member functions. Opening `namespace zidanedb` means the definitions do not need the longer `zidanedb::Database::` prefix. They still need `Database::` because C++ must be told that each function belongs to the class rather than being a free function in the namespace.

The alternative is not to open a namespace block and instead qualify every definition fully:

```cpp
zidanedb::Database::Database(std::filesystem::path path)
    : path_(std::move(path))
{
}

std::optional<std::string>
zidanedb::Database::get(const std::string& key) const
{
    // ...
}
```

Both styles are valid. Reopening the namespace is usually easier when one source file defines several methods from the same class.

There are three other details to notice:

1. `explicit` appears on the constructor declaration inside the class, but not on its out-of-class definition.
2. `[[nodiscard]]` on the declaration is sufficient; it does not need to be repeated on the definition.
3. The trailing `const` is part of `get()`'s signature, so it must appear on both the declaration and definition.

Because `database.h` names `std::optional`, that header must directly include its declaration:

```cpp
#include <optional>
```

Headers should include what they use rather than depending on another standard header to include it indirectly.

### 10. Why can `zidane.cpp` not include `<CLI/CLI.hpp>`?

The include spelling is correct:

```cpp
#include <CLI/CLI.hpp>
```

The problem is that the CMake project does not currently obtain CLI11 or give its include directory to the `zidane` target. The generated compile command for `zidane.cpp` contains only ZidaneDB's own public include directory, so neither the compiler nor clangd has anywhere to find `CLI/CLI.hpp`.

#### Fetch CLI11

Load `FetchContent` and declare CLI11 outside the `BUILD_TESTING` block because `zidane` needs it even when tests are disabled. A suitable location is after `project()` and before the executable targets:

```cmake
include(FetchContent)

FetchContent_Declare(
    CLI11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG        v2.6.2
)

FetchContent_MakeAvailable(CLI11)
```

The existing `include(FetchContent)` inside `if(BUILD_TESTING)` then becomes redundant and can be removed. The Catch2 declaration itself can remain inside that conditional block.

#### Attach CLI11 to the executable that uses it

Add CLI11's CMake target to `zidane`:

```cmake
target_link_libraries(zidane
    PRIVATE
        zidanedb
        CLI11::CLI11
)
```

CLI11 is header-only, so this does not link a compiled CLI11 binary. `CLI11::CLI11` is a CMake interface target that propagates the correct header search path and other usage requirements to `zidane`.

Do not attach CLI11 to the `zidanedb` library. Command-line parsing belongs to the application, while the database library should remain independent of the CLI framework. Attach it separately to `matrix` later only if `matrix.cpp` also includes CLI11.

#### Reconfigure and build

After changing `CMakeLists.txt`, run:

```sh
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build --target zidane
```

The first configure downloads CLI11 into the build tree. The generated compile command for `zidane.cpp` should then contain an include path similar to:

```text
build/_deps/cli11-src/include
```

If the terminal build succeeds but VS Code keeps showing the old error, restart clangd from the command palette so it reloads `build/compile_commands.json`.

Changing angle brackets to quotes or writing a relative path into `build/_deps` is not the right fix. The source code should keep the library's documented include spelling, while CMake supplies the physical include directory.

The [official CLI11 repository](https://github.com/CLIUtils/CLI11) describes CLI11 as a header-only library, and its [basic example](https://cliutils.github.io/CLI11/book/chapters/basics.html) uses `#include "CLI/CLI.hpp"`. The current release used here is [v2.6.2](https://github.com/CLIUtils/CLI11/releases/tag/v2.6.2).

### 11. Why does `zidane.cpp` call `app.require_subcommand(1, 1)`?

```cpp
app.require_subcommand(1, 1);
```

The two arguments set the allowed range of selected subcommands:

```text
minimum subcommands: 1
maximum subcommands: 1
```

Therefore, the user must select exactly one of:

```text
put
get
delete
```

These are valid invocations:

```sh
./build/zidane --db store.zdb put player Zidane
./build/zidane --db store.zdb get player
./build/zidane --db store.zdb delete player
```

This is invalid because no subcommand was selected:

```sh
./build/zidane --db store.zdb
```

This is invalid because multiple operations were requested:

```sh
./build/zidane --db store.zdb put player Zidane get player
```

Without the requirement, the invocation with no subcommand would parse without selecting an operation. All three dispatch conditions would then be false:

```cpp
if (*put_command) { /* ... */ }
if (*get_command) { /* ... */ }
if (*delete_command) { /* ... */ }
```

The program would reach the end and silently return success without doing anything. CLI11 also permits multiple subcommands by default, so the upper limit establishes a useful invariant: after successful parsing, exactly one dispatch branch can run.

This shorter form is equivalent:

```cpp
app.require_subcommand(1);
```

The two-argument form is useful here because it makes both the minimum and maximum explicit. Calling `require_subcommand()` without arguments means one or more subcommands, so it would not enforce the upper limit. [CLI11's subcommand documentation](https://cliutils.github.io/CLI11/book/chapters/subcommands.html) describes these forms.

This setting controls only subcommands. It does not make the database option mandatory. If every operation needs a database path, that option needs its own requirement:

```cpp
app.add_option("-d,--db", database_path, "Path to the database")
    ->required();
```

### 12. Does `std::move()` transfer ownership?

Often, yes—but more precisely:

> `std::move()` gives permission to transfer resources from an object because its current value is no longer needed.

`std::move()` itself does not perform the transfer. It changes how an expression is treated so that the receiving constructor, assignment operator, or function can use move semantics.

For example:

```cpp
std::string destination = std::move(source);
```

Conceptually:

1. `std::move(source)` marks `source` as expendable.
2. `std::string`'s move constructor runs.
3. It may transfer its internal character buffer to `destination` instead of copying every character.
4. `source` remains a valid object, but its value is unspecified.

With `std::unique_ptr`, moving represents literal ownership transfer:

```cpp
auto destination = std::move(source);
```

Afterward, `destination` owns the managed object and `source` is null.

For a simple type, there may be no resource to transfer:

```cpp
int destination = std::move(source);
```

An integer is effectively copied because it has no separately owned resource.

Therefore, in ZidaneDB:

```cpp
data_.insert_or_assign(std::move(key), std::move(val));
```

the intent is:

> The map may take the resources of these local strings; their current values will not be needed afterward.

This is safe because `key` and `val` are by-value parameters local to `put()` and are about to be destroyed when the function returns.

A useful mental rename for `std::move` is **`allow_move_from`**, not **`perform_move`**.

After an object has potentially been moved from, it can safely be destroyed or assigned a new value. Do not otherwise depend on its previous value unless the type explicitly documents its moved-from state.

`std::move()` is declared in:

```cpp
#include <utility>
```

C++ move semantics were introduced to [avoid logically unnecessary expensive copies](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2006/n2027.html).

### 13. What does the `const` keyword mean, and is it required?

`const` is a compiler-enforced promise that something will not be modified through a particular variable, reference, pointer, or method. It is not merely a convention, and it normally has no runtime cost.

It is not required everywhere, but using it correctly makes interfaces clearer and lets the compiler catch accidental modification.

#### A `const` variable

```cpp
const int maximum_size = 100;

// Error: maximum_size is read-only.
maximum_size = 200;
```

Use this when a variable should not be reassigned after initialization:

```cpp
const auto result = database.get("player");
```

Here, `result` cannot later be assigned a different optional value.

#### A `const` reference parameter

The `get()` method has:

```cpp
std::optional<std::string>
get(const std::string& key) const;
```

This part:

```cpp
const std::string& key
```

combines two ideas:

- `&` avoids copying the string.
- `const` prevents `get()` from changing the caller's string through this reference.

For example:

```cpp
void inspect(const std::string& value)
{
    std::cout << value;

    // Error: value is read-only through this reference.
    value = "something else";
}
```

Without `const`, a reference parameter communicates that the function may modify the caller's object:

```cpp
void modify(std::string& value)
{
    value = "changed";
}
```

`const&` does not make the original string permanently immutable. It only prevents this function from modifying it through that reference.

#### A `const` member function

The second `const` in `get()` has a different purpose:

```cpp
get(const std::string& key) const;
//                          ^^^^^
```

This trailing `const` means:

> Calling `get()` will not modify the observable state of this `Database`.

The compiler enforces that promise:

```cpp
std::optional<std::string>
Database::get(const std::string& key) const
{
    // Allowed: this only reads data_.
    auto it = data_.find(key);

    // Error: get() is a const member function.
    data_.erase(key);
}
```

It also allows `get()` to be called on a const database:

```cpp
const zidanedb::Database db{"example.db"};

db.get("player");       // Allowed because get() is const.
db.put("player", "Z");  // Error because put() is not const.
```

The methods in `Database` therefore have appropriate `const` usage:

```cpp
std::optional<std::string> get(const std::string& key) const;
bool erase(const std::string& key);
```

`get()` is `const` because it only observes the database. `erase()` cannot be `const` because it changes the database.

#### `const` and pointers

Pointer declarations can initially look strange:

```cpp
const std::string* pointer;
```

The string is read-only through the pointer, but the pointer may point somewhere else.

```cpp
std::string* const pointer = &value;
```

The pointer cannot point somewhere else, but the string can be modified.

```cpp
const std::string* const pointer = &value;
```

Neither the pointer nor the string can be changed through it. A useful reading trick is to start next to the variable name:

```cpp
std::string* const pointer;
//           ^ pointer is const
```

#### Practical guidelines

- Use `const T&` for input objects that should not be copied or modified.
- Mark member functions `const` when they do not change the object's logical state.
- Consider making local variables `const` when they should never be reassigned.
- Do not use `const` when the object genuinely needs to be modified or moved from.

For example, the `put()` parameters should remain non-const:

```cpp
void Database::put(std::string key, std::string val)
{
    data_.insert_or_assign(std::move(key), std::move(val));
}
```

`key` and `val` are local by-value parameters whose resources are moved into the map. Making them `const` would interfere with efficient moving.

Therefore, `const` is both a safe-programming practice and part of designing an accurate C++ interface. It documents intent, but unlike a comment, the compiler verifies it.

### 14. Why does `Database::put()` return `void`, and why does `run_put()` not receive an error stream?

These are two separate design decisions at two different layers:

- `Database::put()` performs a database operation.
- `run_put()` translates that operation into CLI output and an exit code.

#### Why does `Database::put()` return `void`?

The current API follows this contract:

```cpp
void Database::put(std::string key, std::string value);
```

Its meaning is:

> `put()` succeeds, or it throws an exception.

Therefore, a normal return already communicates success:

```cpp
database.put("player", "Zidane");

// Reaching here means put() succeeded.
```

A failure, such as being unable to write the database file, would interrupt normal execution:

```cpp
try {
    database.put("player", "Zidane");
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
}
```

Returning `bool` might look useful:

```cpp
bool put(...);
```

However, a bare `false` would be ambiguous. It might mean that the file could not be written, the key already existed, the value was invalid, or the database was corrupted.

Returning something is appropriate if the caller needs additional information. For example:

```cpp
enum class PutResult {
    inserted,
    replaced
};

[[nodiscard]]
PutResult put(std::string key, std::string value);
```

This would let the caller distinguish between creating and replacing a value:

```cpp
const auto result = database.put("player", "Zidane");

if (result == PutResult::inserted) {
    // A new key was created.
}
```

If ZidaneDB treats insertion and replacement as the same successful operation, there is no useful result to return. In that case, `void` is the cleanest interface.

#### Why does `run_put()` return `int`?

Unlike `Database::put()`, `run_put()` is part of the CLI layer. Its return value represents a process exit code:

```cpp
return 0; // Command succeeded.
return 1; // Command failed.
```

This allows `main()` to return the command's status directly:

```cpp
return zidanedb::cli::run_put(/* ... */);
```

The `int` is not the result of the database operation. It is the result of executing the CLI command.

It would also be valid to make `run_put()` return `void` and let `main()` return zero afterward:

```cpp
run_put(/* ... */);
return 0;
```

Returning `int` becomes useful when individual commands decide their own exit status.

#### Why does `run_put()` not receive an error stream?

In the example, errors thrown by `Database::put()` are handled centrally by `main()`:

```cpp
try {
    return run_put(database, key, value, std::cout);
} catch (const std::exception& error) {
    std::cerr << "zidane: " << error.what() << '\n';
    return 1;
}
```

Therefore, `run_put()` only needs the success output stream:

```cpp
int run_put(
    Database& database,
    std::string key,
    std::string value,
    std::ostream& output);
```

It does not need an error stream because it does not print errors itself.

By comparison, `run_get()` may handle a missing key directly:

```cpp
const auto value = database.get(key);

if (!value) {
    error << "Key not found\n";
    return 1;
}
```

A missing key is represented by `std::optional`, not by an exception, so `run_get()` needs somewhere to print that expected error.

The resulting responsibility split is:

```text
Database I/O failure -> exception -> main() prints to std::cerr
Missing get key      -> optional  -> run_get() prints to its error stream
Successful command   -> run_*()   -> prints to its output stream
```

Do not add an `error` parameter to `run_put()` until it has an error condition that it handles itself. An unused parameter would only make the interface noisier.

### 15. What is the philosophy behind `try`, `throw`, and `catch`?

At a high level, exceptions let a function say:

> I cannot complete my promised operation, and I cannot meaningfully recover here. Let the caller decide what to do.

#### The basic mechanism

```cpp
try {
    database.put("player", "Zidane");
} catch (const std::exception& error) {
    std::cerr << "zidane: " << error.what() << '\n';
}
```

The flow is:

1. Code inside `try` executes normally.
2. A lower-level function detects a failure and uses `throw`.
3. Normal execution stops.
4. C++ searches upward for a compatible `catch`.
5. Local objects are safely destroyed while moving upward.
6. The `catch` block handles or reports the failure.

For example:

```cpp
void Database::put(std::string key, std::string value)
{
    if (/* writing to disk failed */) {
        throw std::runtime_error{"Could not write database file"};
    }
}
```

The important benefit is that every intermediate function does not need to repeatedly check and forward an error code:

```text
run_put()
    -> Database::put()
        -> write_database_file()
            -> operating-system file operation fails
```

The exception can travel directly from the failing operation to an appropriate boundary, such as `main()`.

#### Expected outcomes versus exceptional failures

Exceptions are usually best for situations where a function cannot fulfill its contract:

- The database file cannot be opened.
- A disk write fails.
- The database file is corrupted.
- Permissions prevent an operation.
- An internal invariant has been violated.

Routine outcomes should normally use regular return values:

```cpp
const auto value = database.get("missing");

if (!value) {
    // Missing keys are expected, so get() returns std::nullopt.
}
```

A useful rule is:

> If callers are expected to make ordinary decisions based on an outcome, return it. If the operation cannot be completed normally, consider reporting an error.

The boundary is sometimes subjective. Different professional C++ projects choose exceptions, explicit result objects, or error codes depending on their requirements.

#### Where should exceptions be caught?

Do not put `try`/`catch` around every function call. Catch an exception when the code can do something useful:

- Recover from it.
- Retry safely.
- Add meaningful context and rethrow it.
- Translate it for the user.
- Stop it from escaping an application boundary.

For ZidaneDB, `main()` is a natural boundary:

```cpp
int main(int argc, char** argv)
{
    try {
        // Parse arguments and run the selected operation.
    } catch (const std::exception& error) {
        std::cerr << "zidane: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
```

The database library reports the failure. The CLI translates it into a message and nonzero exit code.

#### How does a professional-grade database handle failures?

A production database usually handles failures in layers:

- **Classification:** Distinguish missing data, invalid requests, transient I/O failures, corruption, and internal bugs.
- **Clear error information:** Use typed errors or result objects with useful error codes and context.
- **State protection:** Ensure a failed write does not leave half-written or inconsistent data.
- **Recovery:** Use techniques such as transactions, write-ahead logs, checksums, and startup recovery.
- **Cleanup:** Reliably release locks, files, memory, and other resources.
- **Boundary translation:** Convert internal failures into API responses, CLI messages, logs, or process exit codes.
- **Observability:** Record enough information for operators to understand failures.
- **Careful retries:** Retry only failures that are genuinely transient and safe to repeat.

The central promise is usually:

> Either the operation completes successfully, or the database remains in a known consistent state and reports why it could not complete.

For the first version of ZidaneDB, the current philosophy is reasonable:

- `get()` uses `std::optional` for an expected missing key.
- `erase()` uses `bool` to report whether a key existed.
- `put()` returns normally on success and throws if it cannot complete.
- `main()` catches unexpected operational failures and translates them into CLI errors.
