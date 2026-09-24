# API Design and Recovery

## API Design (Basic)
The basic question: what do PUT, GET, and DELETE actually mean?

PUT scenarios:
- PUT with empty value
- PUT overwrite

GET scenarios:
- GET a key with an empty value
- GET a non-existent key

DELETE scenarios:
- DELETE a non-existent key

What I want right now:
- PUT with empty value should be allowed.
- DELETE a non-existent key should notify the users, maybe an error is ok.

Current problems:
- Currently, the db file will use empty value as an indicator of a deleted
key. What should I do?
- If I want to do PUT with empty value, how to handle empty value
in the `zidane` command line?

Solutions:
- Having a type of db entry will help us differentiate between a deleted
key and an existing key with an empty value.
- CLI11 should handle empty value just fine. Just need to test it
thoroughly.

## Recovery

### Basic recovery path (summary)

```
Can I read the fixed-size header?
        ↓ no
incomplete tail

Are lengths sane and within remaining file size?
        ↓ no
corrupt/incomplete record

Can I read the entire payload + checksum?
        ↓ no
incomplete tail

Does checksum match?
        ↓ no
corrupted/torn record

Yes
        ↓
accept record
```

```
record checksum
    ↓
log scanner
    ↓
recover incomplete tail
    ↓
rebuild index
    ↓
deterministic crash points
    ↓
partial-write injection
    ↓
fsync experiments
    ↓
VM hard-reset experiments
```

### Source of truth
The `.zdb` data log is authoritative and the `.idx` file is derived state.
If the index disagrees with the log after a crash, recovery trusts the log.

### Every data record is self-describing
A valid record should contain the following information:
- Operation type (PUT/DELETE)
- Key length
- Key bytes
- Value length
- Value bytes
- Checksum

Goal: given an offset, can ZidaneDB tell whether there is one complete valid record there.

### Write a sequential log scanner
Write something like `scan_records(offset)` that starts at the offset byte and walks the `.zdb` file.
It should return every valid record until EOF.

### Scanner should tolerate an incomplete tail
If the DB file has an incomplete tail, do not treat that as random corruption,
ignore or truncate the incomplete tail.

### Rebuild the index entirely from the data log
Can reconstruct the hash index from the data log.
Question: Do I need to replay all of the `PUT` and `DELETE` to reconstruct?

### Idea of checkpoint
We don't need to rebuild the persistent index file from scratch.
The basic idea is
```
existing persistent index
        +
replay tail of data log
        =
current index
```

Add `last_applied_log_offset` to the index file header
```
magic
version
bucket_count
last_applied_log_offset
```

If `last_applied_log_offset == 0 (or missing)`, of course we will build
the index file from scratch.

This idea works well because a big concern for us right now is an
incomplete tail. So mostly we just need to check the last portion
of the db file.

How about the index file is corrupted itself, not the db file?
The simple approach is, after an unclean crash, rebuild the index file
from the scratch.

Add another marker in the index file header to indicate if the index file
shuts down cleanly or not.
```
magic
version
bucket_count
last_applied_log_offset
clean_shutdown
```

### Use checksums and distinguish truncation from corruption
I already included a checksum in my Data Records.


### Build crash injection into Matrix


### Hardware crash simulated with VMs (virtual machines)


## Design Notes
- Need to rework the `read_` and `write_` helper methods to express failure modes.
- `scan_records()` just scans and return the read status, `last_valid_offset`,
and `failing_record_offset`
- Recovery (including truncating) is done in a separate function. This will help
with testing, and also allows users to choose how they want to recover the db file.
- Capture the `record_start` and `record_end` so we can rebuild the index for each record.
- For checksum mismatch error, we should report corruption, and leave the file
unchanged.


## Appendix: Vendoring CRC32C for Offline Builds

ZidaneDB's existing dependencies are source-code snapshots committed into the
project, not separate Git repositories. This is documented in
[vendor/README.md](../vendor/README.md).

The process is: download an archive, extract it into `vendor/`, tell CMake to use
it, and commit the files.

### 1. Download a Specific Version

Run these commands from the ZidaneDB project directory:

```bash
crc32c_download_dir=$(mktemp -d)

curl -fL \
  https://github.com/google/crc32c/archive/refs/tags/1.1.2.tar.gz \
  -o "$crc32c_download_dir/crc32c.tar.gz"
```

Here, `1.1.2` is an existing
[upstream version tag](https://github.com/google/crc32c/tags). Choosing a version
instead of `main` makes the dependency choice explicit.

The commands mean:

- `mktemp -d`: create a temporary directory.
- `curl`: download a file.
- `-f`: report an error if the server returns an HTTP error.
- `-L`: follow redirects, which GitHub uses for downloads.
- `-o`: specify where to save the downloaded archive.

Run the following extraction commands in the same shell so that
`crc32c_download_dir` still refers to that temporary directory. Continue only if
the download succeeded.

### 2. Extract It into `vendor/crc32c`

```bash
mkdir vendor/crc32c && \
tar -xzf "$crc32c_download_dir/crc32c.tar.gz" \
  --strip-components=1 \
  -C vendor/crc32c
```

If `vendor/crc32c` already exists, stop and inspect it instead of extracting over
it. The `&&` runs extraction only if creating the directory succeeds.

The `tar` options mean:

- `-x`: extract.
- `-z`: decompress gzip.
- `-f`: read the specified archive.
- `-C`: extract into this directory.
- `--strip-components=1`: remove the archive's outer directory.

For example:

```text
crc32c-1.1.2/include/crc32c/crc32c.h
                ↓
vendor/crc32c/include/crc32c/crc32c.h
```

Unlike `git clone`, this creates no nested `.git` directory. Keep the included
license files.

### 3. Connect It to CMake

In the root [CMakeLists.txt](../CMakeLists.txt), keep the existing CLI11
`add_subdirectory()` line only once. Add the following block alongside it, before
`add_library(zidanedb ...)`:

```cmake
set(CRC32C_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CRC32C_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(CRC32C_USE_GLOG OFF CACHE BOOL "" FORCE)
set(CRC32C_INSTALL OFF CACHE BOOL "" FORCE)

# Compatibility with this dependency's older CMake policy version.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

add_subdirectory(vendor/crc32c EXCLUDE_FROM_ALL)
```

Disabling those optional components avoids needing CRC32C's test, benchmark, and
logging dependencies. These options come from its
[CMake configuration](https://github.com/google/crc32c/blob/1.1.2/CMakeLists.txt).

For CMake 4.x, the policy setting accommodates this older dependency without
editing its files, as described in
[CMake's documentation](https://cmake.org/cmake/help/latest/variable/CMAKE_POLICY_VERSION_MINIMUM.html).

Then, after the `add_library(zidanedb ...)` block, add:

```cmake
target_link_libraries(zidanedb PRIVATE crc32c)
```

This supplies the library and its include path. The implementation can then use:

```cpp
#include <crc32c/crc32c.h>
```

### 4. Record and Commit the Dependency

Add CRC32C's version and upstream URL to `vendor/README.md`, then review and stage
the relevant files:

```bash
git add vendor/crc32c vendor/README.md CMakeLists.txt
git diff --cached --stat
```

Staging is not committing; commit when satisfied with the changes. If
`CMakeLists.txt` has unrelated edits in progress, use `git add -p CMakeLists.txt`
instead of staging that whole file.

Once committed, CRC32C's source travels with ZidaneDB. Only the initial download
needs internet access; subsequent builds use the local copy.

## Appendix: Stream Write Error Handling

### Why `write_uint32()` Can Return `void`

`write_uint32()` can reasonably return `void` when writing code uses stream
exceptions. Whether it should return `bool` depends on the error-handling model
chosen for the serialization layer.

#### Why Reading Returns `bool`

Reading can fail for normal input-related reasons:

- EOF.
- A truncated integer.
- A corrupted record.
- Insufficient bytes.

The caller needs to branch on that result:

```cpp
std::uint32_t value{};

if (!read_uint32(stream, value)) {
    return false;
}
```

A read failure often means that the input is incomplete or invalid, not
necessarily that the program itself failed.

#### Why Writing Can Return `void`

When writing, there is no equivalent of valid but incomplete input. The complete
number is already available. The expected outcome is:

```text
write succeeds
or
an I/O error occurs
```

If exceptions are enabled:

```cpp
stream.exceptions(std::ios::failbit | std::ios::badbit);
```

then `stream.write()` throws `std::ios_base::failure` on failure. The helper does
not need a return value:

```cpp
void write_uint32(std::ostream& stream, std::uint32_t value) {
    // Construct the little-endian bytes.
    stream.write(/* bytes and size */);
}
```

This fits ZidaneDB's database write path, where the outer operation catches
stream failures and translates them into a database error.

#### Returning `bool` Is Also Possible

Without stream exceptions, `stream.write()` normally sets the stream's error
state instead of throwing. A self-contained helper could return that state:

```cpp
bool write_uint32(std::ostream& stream, std::uint32_t value) {
    const std::array<std::uint8_t, 4> bytes{
        static_cast<std::uint8_t>(value),
        static_cast<std::uint8_t>(value >> 8),
        static_cast<std::uint8_t>(value >> 16),
        static_cast<std::uint8_t>(value >> 24),
    };

    stream.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size())
    );

    return static_cast<bool>(stream);
}
```

The caller must then always check it:

```cpp
if (!write_uint32(stream, value)) {
    return false;
}
```

Changing only `write_uint32()` would leave an inconsistent design. The same
decision would also need to be made for:

```text
write_uint64()
write_uint8()
write_string()
write_record()
```

Otherwise, a checked integer write could succeed while a later unchecked
payload write fails.

#### Recommendation for ZidaneDB

For ZidaneDB's current design, keep write helpers returning `void` and enable
stream exceptions around the complete write operation:

```cpp
file.exceptions(std::ios::failbit | std::ios::badbit);

try {
    write_record(file, record);
    file.flush();
} catch (const std::ios_base::failure&) {
    // Translate to a database-level error.
}
```

This gives one failure path for the entire record instead of checking every
individual field. The asymmetry is intentional:

```text
read helper  -> bool because incomplete or corrupt input must be examined
write helper -> void because an I/O failure becomes an exception
```

The important requirement is consistency: every production stream using the
`void` write helpers must either convert write failures into exceptions or have
its state checked after writing.

## Appendix: Checksum Recovery and Read Error Classification

A checksum mismatch means that a record is corrupt and must not be applied. A
checksum detects damage; it cannot identify which bytes are correct or repair
the record.

### Recommended Recovery Policy

When scanning the log, handle each result according to its meaning:

| Result | Meaning | Recommended action |
| --- | --- | --- |
| `Success` | Complete, valid record | Apply it to the index. |
| `EndOfFile` | Clean end between records | Finish scanning normally. |
| `Truncated` | A record started but did not finish | Stop; optionally truncate to `record_start` during explicit recovery. |
| `ChecksumMismatch` | A complete-looking record contains corrupted bytes | Stop, report corruption, and leave the file unchanged. |
| `InvalidLength` | A stored length exceeds the permitted limit | Stop and report corruption. |
| `InvalidType` | The type is neither `Put` nor `Delete` | Stop and report corruption. |
| I/O failure | The disk, device, or stream failed | Throw and do not modify the file. |

Normal database opening should be non-destructive: it should report corruption
rather than silently resizing the file. A later explicit recovery operation
could:

1. Back up the database file.
2. Keep the longest valid prefix.
3. Truncate at the start of the bad record.
4. Rebuild the index from that prefix.

For `ChecksumMismatch`, truncation should be an explicit user choice because it
discards the damaged record and everything after it.

Do not try to search for the next valid record. ZidaneDB currently has
variable-length records without a synchronization marker, so after corruption
there is no reliable way to determine where the next record begins.

### Why `bool` Loses Necessary Information

For example, `read_string()` currently returns `false` for all of these cases:

- The length prefix is completely absent.
- The length prefix is partially present.
- The stored length exceeds `max_length`.
- The string payload is incomplete.
- The stream has another failure.

These cases have different meanings to the scanner, so `bool` is no longer
expressive enough. A simple replacement could be:

```cpp
enum class ReadStatus {
    Success,

    // Zero bytes were available when the operation began.
    EndOfInput,

    // Some required bytes were missing.
    Truncated,

    // A stored length exceeded its allowed maximum.
    InvalidLength
};
```

The helpers can retain their output parameters:

```cpp
ReadStatus read_uint32(std::istream& stream, std::uint32_t& result);

ReadStatus read_string(
    std::istream& stream,
    std::string& result,
    std::uint32_t max_length);
```

This is simpler for the current C++20 project than introducing a custom
`expected`-like result type.

### `EndOfInput` Depends on Context

Suppose `read_uint32()` reports `EndOfInput`:

- If no record has started, this can mean clean EOF.
- If the record type has already been read, the missing integer means the
  record is truncated.

The helper reports what happened while reading the bytes. `read_record()` then
interprets that result in the context of a record:

```cpp
// Reading the first byte of a new record:
EndOfInput -> RecordReadStatus::EndOfFile

// Reading anything after the first byte:
EndOfInput -> RecordReadStatus::Truncated
Truncated  -> RecordReadStatus::Truncated
```

For a four-byte integer:

- Reading four bytes produces `Success`.
- Reading zero bytes produces `EndOfInput`.
- Reading one to three bytes produces `Truncated`.

For a string:

- A completely missing length prefix produces `EndOfInput`.
- A partial length prefix produces `Truncated`.
- A length greater than the permitted maximum produces `InvalidLength`.
- A valid length with insufficient payload bytes produces `Truncated`.

### Suggested `read_record()` Validation Order

A reasonable order is:

1. Read the type.
2. If no type byte exists, return `EndOfFile`.
3. Validate that the type is `Put` or `Delete`.
4. Read the key and classify its error.
5. Read the value and classify its error.
6. Read the stored checksum.
7. Compare the computed and stored checksums.
8. Return `Success`.

The existing `RecordReadStatus` contains the principal results needed. The
helpers must provide enough information for `read_record()` to return those
results accurately.

One limitation remains: a corrupted length can claim that a record is longer
than the remaining file. This looks identical to a genuinely interrupted
write. With the current format, `Truncated` means that the record could not be
completed, not that a crash definitely happened.

### Write Helpers Can Still Return `void`

Reads encounter expected control-flow conditions such as clean EOF, so status
values are useful. Write failures are normally exceptional conditions, such as
a full disk, a device error, a closed stream, or a filesystem problem.

The write helpers can continue returning `void` if output stream exceptions are
enabled:

```cpp
file.exceptions(std::ios::failbit | std::ios::badbit);
```

Then oversized input can throw `std::length_error`, while a stream failure
throws `std::ios_base::failure`. This avoids returning a `bool` from every write
helper and relying on every caller to check it.

### Do Not Expose Partially Read Records

Build a record in a temporary variable and update the caller's `Record` only
after complete validation:

```cpp
Record candidate;

// Read and validate candidate...

record = std::move(candidate);
return RecordReadStatus::Success;
```

Otherwise, `read_record()` can leave its output partially populated after
truncation or a checksum failure, and a caller could accidentally use that
invalid record.

A practical implementation order is to improve `read_uint32()` and
`read_string()` first, then map their richer results inside `read_record()`.
After that, the scanner's recovery decisions become straightforward.

## Appendix: Public Headers Must Not Depend on Private Headers

The build failure caused by including `record.h` from `database.h` comes from a
public/private include-directory mismatch.

`database.h` is a public header under `include/zidanedb`, but `record.h` is a
private implementation header under `src`:

```text
include/zidanedb/database.h
src/record.h
```

The CMake target exposes the directories differently:

```cmake
target_include_directories(zidanedb
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
```

While compiling the `zidanedb` library, both directories are searchable, so
the library source can find `record.h`. A consumer such as the CLI inherits
only the public `include` directory. When it includes `database.h`, it cannot
resolve this line:

```cpp
#include "record.h"
```

This explains why the library target can build before compilation fails in the
CLI target.

### Why `database.h` Started Depending on `record.h`

The dependency is introduced by the scan result:

```cpp
struct RecordScanResult {
    RecordReadStatus status;
    std::uint64_t last_valid_offset;
    std::uint64_t failing_record_offset;
};
```

`RecordReadStatus` is defined in the private `src/record.h` header.

### Recommended Design: Keep the Scan Types Private

Because `scan_records()` and its result are private implementation details,
keep `record.h` private. Forward-declare the scan result in `database.h`:

```cpp
namespace zidanedb {

struct RecordScanResult;

class Database {
    // ...
    RecordScanResult scan_records(std::uint64_t start_offset = 0);
};

} // namespace zidanedb
```

Place the complete definition in `database.cpp`, where `record.h` is already
available:

```cpp
#include "record.h"

namespace zidanedb {

struct RecordScanResult {
    RecordReadStatus status;
    std::uint64_t last_valid_offset;
    std::uint64_t failing_record_offset;
};

} // namespace zidanedb
```

A function may be declared as returning an incomplete type. The type must be
complete when the function is defined or its result is used. Since this
function is private and implemented in `database.cpp`, this arrangement fits
the current design.

### Alternative: Make Records Part of the Public API

If ZidaneDB users should work directly with `Record`, `RecordType`, and
`RecordReadStatus`, move the header to:

```text
include/zidanedb/record.h
```

Public headers should then include it using its public path:

```cpp
#include "zidanedb/record.h"
```

As a public, self-contained header, `record.h` should explicitly include
`<iosfwd>` because it declares functions using `std::istream` and
`std::ostream`.

### Do Not Expose `src` to Consumers

Avoid changing the `src` include directory from `PRIVATE` to `PUBLIC` merely to
make the include compile:

```cmake
PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src"
```

That would expose all implementation headers to library consumers and conceal
the underlying public/private boundary problem. For the current design,
keeping `record.h` private and defining the scan-result type in `database.cpp`
is the cleaner choice.
