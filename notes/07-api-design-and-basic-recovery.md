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
Write something like `scan_records()` that starts at byte 0 and walks the `.zdb` file.
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


### Build crash injection into Matrix


### Hardware crash simulated with VMs (virtual machines)

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
