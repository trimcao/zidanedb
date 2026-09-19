# C++ Syntax and Practical Tips

Teaching comments moved here from the source, together with their relevant code.
Comments about file formats, invariants, and important test behavior remain next
to the implementation.

## 1. Constructors, Filesystem Paths, and Const Member Functions

From [the database test helper](../../tests/database_test.cpp). This helper owns
dedicated temporary test files: its constructor and destructor remove those
files, so it must not be used with filenames belonging to real data.

```cpp
#include <filesystem>
#include <string>
#include <system_error>

/*
TemporaryDatabaseFile is a small test helper. Its job is:
1. Choose a path in the operating system’s temporary directory.
2. Ensure no old database exists at that path.
3. Let the test use the path.
4. Delete the test database when the test finishes.
*/
class TemporaryDatabaseFile {
  public:
    // note about the `explicit` keyword:
    // This prevents C++ from automatically converting a string
    // into a TemporaryDatabaseFile.
    explicit TemporaryDatabaseFile(const std::string& filename)
        : path_{
              // note: For filesystem paths, / is overloaded
              // to mean 'join these path components.'
              std::filesystem::temp_directory_path() / filename},
          idx_path_{path_} {
        idx_path_ += ".idx";
        remove();
    }

    ~TemporaryDatabaseFile() { remove(); }

    // note: The trailing const promises that calling path()
    // does not modify the TemporaryDatabaseFile object
    const std::filesystem::path& path() const { return path_; }

    const std::filesystem::path& idx_path() const { return idx_path_; }

  private:
    std::filesystem::path path_;
    std::filesystem::path idx_path_;

    void remove() {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
        std::filesystem::remove(idx_path_, ignored);
    }
};
```

The part after the constructor's `:` is a member initializer list. Members are
initialized before the body executes, in their declaration order in the class.
Here, `path_` is initialized first, then copied into `idx_path_`.

The two uses of `const` in `path()` have different meanings:

- `const std::filesystem::path&` returns a reference that callers cannot use to
  modify the stored path.
- The trailing `const` makes this a const member function, so it can also be
  called on a const `TemporaryDatabaseFile`.

The destructor provides scope-based cleanup: when the helper leaves scope, it
attempts to remove both files. This is an example of RAII (Resource Acquisition
Is Initialization).

## 2. Passing Read-Only Strings by Reference

From [the CLI command declarations](../../apps/zidane/commands.h):

```cpp
// note: use `std::string& key` to avoid copying the string key.
int run_get(const Database& database, const std::string& key,
            std::ostream& output, std::ostream& error);
```

The original comment above refers to references generally. The actual parameter
is `const std::string&`: it avoids copying an existing string and prevents the
function from modifying it through that reference. A plain `std::string&` would
allow modification and would not accept a const string or a temporary string.

## 3. Keep main() Focused on Wiring

From [the Zidane CLI](../../apps/zidane/zidane.cpp):

```cpp
/*
Guidelines for main(): keep main() thin
A good main() should do approximately five things:

- Define the CLI.
- Parse and validate arguments.
- Construct the database.
- Dispatch to the selected command.
- Translate failures into messages and exit codes.

*/
```

These excerpts show parsing and command dispatch; option declarations and the
other command branches are omitted:

```cpp
try {
    app.parse(argc, argv);
} catch (const CLI::ParseError& error) {
    return app.exit(error);
}

std::filesystem::path path{database_path};

try {
    zidanedb::Database db{path, num_index_buckets};

    // Other command branches omitted.
    if (*get_command) {
        return zidanedb::cli::run_get(db, get_key, std::cout, std::cerr);
    }
} catch (const std::exception& error) {
    std::cerr << "zidane: " << error.what() << '\n';
    return 1;
}
```

The storage logic belongs in `Database`; command handlers translate database
results into CLI output and exit codes.

## 4. CLI11 Subcommands

From [the Matrix CLI](../../apps/matrix/matrix.cpp):

```cpp
/* Notes on the use of subcommands

Subcommands have several advantages:

- Valid test names automatically appear in --help.
- Unknown tests are automatically rejected.
- Each test can have its own options.
- You avoid a growing string-based if chain.

*/
CLI::App app{"Matrix: Tormentor of ZidaneDB"};

auto* basic = app.add_subcommand("basic", "Verify persistence across reopening");

std::size_t pair_count = 10'000;
auto* perf_basic = app.add_subcommand("perf-basic", "Measure basic persistence performance");
perf_basic->add_option("-n,--pairs", pair_count, "Number of pairs");
```

The existing code still uses `if` branches to dispatch commands. CLI11 handles
recognizing and validating command names, so those branches do not need to
compare user-supplied strings themselves.

The Zidane CLI also limits how many subcommands a caller may select:

```cpp
// The following means: only one subcommand could be used.
// So only one among GET, PUT, DELETE could be used.
app.require_subcommand(1, 1);
```

The arguments are the minimum and maximum number of subcommands. `(1, 1)`
requires exactly one. In the actual Zidane CLI, the names are lowercase `get`,
`put`, and `del`.

## 5. Passing an Enum's Serialized Byte to CRC32C

Consider this attempted helper:

```cpp
std::uint32_t extend_checksum_record_type(
    std::uint32_t checksum,
    const zidanedb::RecordType& type
) {
    return crc32c::Extend(
        checksum,
        reinterpret_cast<std::uint8_t*>(type),
        sizeof(std::uint8_t)
    );
}
```

`crc32c::Extend()` expects a pointer to bytes, but `type` is a reference to an
enum value. `reinterpret_cast<std::uint8_t*>(type)` tries to convert the enum's
value into a pointer; it does not take the value's address.

There is also a constness problem: `type` is `const`, while `std::uint8_t*`
points to mutable data. `Extend()` expects a `const std::uint8_t*`.

The clearest solution is to explicitly convert the enum to its serialized byte:

```cpp
std::uint32_t extend_checksum_record_type(
    std::uint32_t checksum,
    zidanedb::RecordType type
) {
    const auto encoded_type = static_cast<std::uint8_t>(type);

    return crc32c::Extend(
        checksum,
        &encoded_type,
        sizeof(encoded_type)
    );
}
```

Passing this small enum by value is simpler than passing it by `const`
reference. The conversion makes the intention explicit:

```text
RecordType::Put
        ↓ static_cast
one uint8_t byte
        ↓ address
pointer passed to Extend()
```

It is technically possible to checksum the enum's in-memory representation:

```cpp
return crc32c::Extend(
    checksum,
    reinterpret_cast<const std::uint8_t*>(&type),
    sizeof(type)
);
```

The first version is preferable because it explicitly checksums the same
`std::uint8_t` representation intended for serialization. This assumes that the
enum has a fixed underlying type:

```cpp
enum class RecordType : std::uint8_t {
    Put = 1,
    Delete = 2
};
```

The pointer `&encoded_type` remains valid throughout the `Extend()` call. It is
fine that the local variable disappears afterward because `Extend()` consumes
the bytes during the call and does not retain the pointer.
