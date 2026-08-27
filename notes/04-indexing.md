# Indexing

The problem we currently want to solve is: How to avoid loading all of the database to memory (load time),
but still have fast and efficient lookup.

In the previous notes, we already tried two options:
- Do not use a `map`, do not read anything in startup, but need to find a key by scanning the whole db file.
- Use a `map`, load the whole database into that map, then lookup time is very fast. However, we want
to avoid loading the whole database into memory.

## Basic Indexing

Instead of loading all of the key-value pairs, where the values can take lot of memories, we only
need to load the index. If all keys are unique in the db, we still load all of the keys into memory,
but since indices are only numbers, the amount of memory we have to use is reduced.

There is a trade off here: by reducing the total amount of memory used, we increase the time required
for each `GET` op, because now for each `GET` we have to read from the db file.

Unfortunately, we sill haven't solved the `slow startup` problem. What can we do?

Anyway, for now, let's have a set of small objectives:
- Keep the current startup logic mostly the same, only change from value to offset.
- Read from the db file, and write to the db file using offsets.
- Open the file in binary mode

If I implement a persistent index file:
- Have a separate, persistent index file.
- Figure out how to maintain entries in the index file.

## Why Indexing?

Remember with basic indexing, we still haven't solved our `slow startup` problem.
So why bother with indexing? Because we want to separate the indexing from the database itself,
that will help us experiment with other data structures and algorithms

A rough roadmap from ChatGPT:
```
Current ZidaneDB
---------------
Scan ALL data
Build hash index
READY

        ↓

Persisted hash-index snapshot
-----------------------------
Load smaller index file
Build hash index
READY

        ↓

Index snapshot + tail replay
----------------------------
Load snapshot
Replay only recent writes
READY

        ↓

Disk-resident persistent index
------------------------------
Read tiny metadata/root
READY
```

A shorter plan:
```
1. In-memory hash index
       ↓
2. Persisted hash-index snapshot
       +
   checkpoint offset
       ↓
3. Snapshot + replay only the new tail
       ↓
4. Discover remaining limitations
       ↓
5. THEN perhaps B+ tree
```

## New Matrix Tests

- One workload with large values.
- One workload that is overwrite-heavy

```
CURRENT
no index
GET = 256 ops/sec 😭

↓ implement

HASH INDEX
key -> offset
GET should become fast again

↓ Matrix attacks with

LARGE VALUES
Does rebuilding by reading all values make sense?

OVERWRITE-HEAVY HISTORY
Why scan 1,000,000 records to recover 10,000 live keys?

↓ implement

INDEX SNAPSHOT + CHECKPOINT OFFSET
Load snapshot + replay tail

↓ Matrix attacks with

MILLIONS OF LIVE KEYS
Does rebuilding a giant unordered_map itself become painful?

↓ perhaps

DISK-RESIDENT INDEX
B+ tree becomes naturally motivated
```

## Current Tasks

### Binary Mode for Files
The motivation for binary mode:
- No text-mode translation. For example, on Windows, `\n` can be translated to `\r\n`.
- Do not need to use base64 encoding to handle multi-line key/value anymore.
- We have total control over the file format, so we can work with offsets properly.
Indexing becomes easier to create.

Resolution: Done

Effect:

- PUT time increases, probably because we need to write 2 things for each string,
the length and the bytes.
- LOAD time decreases, probably because loading and reading a binary file is faster
than a text file.

### Naive Indexing
Instead of building the whole key-value map, we only build the key-value index.
The map type will be `unordered_map<std::string,std::uint64_t>`.

The motivation is to separate indexing from the full database. It will solve a
few problems:
- Suppose we have one workload with large values. Indexing will help us reduce
the amount of used memory significantly.
- If we have an overwrite-heavy workload, i.e. 10000 entries but with 10,000,000
puts, then the index file will have only 10000 entries, not 10,000,000.

Implementation notes:
- First, I need to learn how to work with offsets.
- The first indexing scheme is very simple: no persistent index yet, at startup,
read the whole db file and construct a map of key-value_offset.


### Matrix New Tests
Following the motivation of indexing above, we will create two new workloads for
Matrix:
- One workload with large values.
- One workload that is overwrite-heavy

### Persistent Index
I was a little confused when implementing the first basic indexing scheme above.
I was thinking about a persistent index, but the first indexing implementation
is just building an index from the whole db. Not very interesting yet, but at
least I learned about the file offsets, how to do `get` when given an offset,
and what an offset means in this context. All pretty important stuffs.

Now it's time to think about a persistent index.

Implementation notes:
- The index format is probably: [key-length][key][value-offset].
- How do we update the index? The map will store the latest index that we have.
We can either: (1) write the whole index from scratch when quit, or (2) search
for the key in the index and update the offset.
- I am leaning towards (2) because it's the more interesting challenge.
The performance will be shit either way, so it's probably more educational
to learn how to navigate the index file.
- One small note: take care of the cases when we have a new key, when we update
an existing key, and when we delete a key.
- Let's assume offset = 0 means the key has been deleted or the key has no value.

Here is the current performance after implenting a persistent index:
```
./build/matrix perf-basic --pairs 10000
Database file: "/var/folders/2z/bttptg2d17d0w39r921_h2200000gp/T/matrix-performance.zdb"
Pairs:        10000
Put time:     2.9613 seconds
Put rate:     3376.89 ops/second
Load time:    0.00210379 seconds
Verify time:  0.136157 seconds
Get rate:     73444.5 ops/second
File size:    257780 bytes
```

`PUT` rate is definitely not great. We need to do many things for a PUT op. It's ok for now.

### Using `fstream` instead of `ifstream` and `ofstream`
To be able to have full control of the db file and the index file, it's better to use
`fstream`.
Let's see if using `fstream` helps us improve the performance of PUT ops.

Notes:
- The interfaces for IO stream include: `istream`, `ostream` and `iostream`.
- `fstream` supports both `istream` and `ostream`.
- We can create helper methods that accept `istream` and `ostream`, then
we can pass `fstream` object into those methods.

The performance for PUT marginally improves
```
./build/matrix perf-basic --pairs 10000
Database file: "/var/folders/2z/bttptg2d17d0w39r921_h2200000gp/T/matrix-performance.zdb"
Pairs:        10000
Put time:     2.65331 seconds
Put rate:     3768.88 ops/second
Load time:    0.00205233 seconds
Verify time:  0.133622 seconds
Get rate:     74837.9 ops/second
File size:    257780 bytes
```

## What's Next
The next challenge will be: how to avoid reading the db file from scratch?
Definitely we can have checkpoint and continue reading from that checkpoint.
Potentially our storage engine will be super fast compared to the current
implementation.


## Appendix

### Opening, Reading, and Writing Files in Binary Mode

Binary mode is useful when a file should contain exactly the bytes that the program writes, without
treating characters such as newlines specially. For a database, this allows keys and values to
contain newlines, `\0` bytes, or other arbitrary data without Base64 encoding.

To replace a file and write it in binary mode:

```cpp
std::ofstream file{
    path,
    std::ios::binary | std::ios::trunc
};
```

To append to a binary file:

```cpp
std::ofstream file{
    path,
    std::ios::binary | std::ios::app
};
```

To read a binary file:

```cpp
std::ifstream file{
    path,
    std::ios::binary
};
```

The flags mean:

- `std::ios::binary` disables text-mode translation.
- `std::ios::app` places every write at the end of the file.
- `std::ios::trunc` replaces the existing contents with an empty file.

On Linux and macOS, text and binary modes usually behave the same. On Windows, text mode can
translate `\n` into the two bytes `\r\n`; binary mode prevents that translation.

#### Writing Bytes

Use `write()` to write a specific number of bytes:

```cpp
std::string value{"line1\nline2"};

file.write(
    value.data(),
    static_cast<std::streamsize>(value.size())
);

if (!file) {
    std::cerr << "Write failed\n";
}
```

This writes the string's bytes exactly, but the file does not automatically record where the string
ends. A simple solution is to write the string's length before its contents:

```cpp
#include <cstdint>
#include <fstream>
#include <string>

void write_string(std::ofstream& file, const std::string& value)
{
    const std::uint32_t length =
        static_cast<std::uint32_t>(value.size());

    file.write(
        reinterpret_cast<const char*>(&length),
        sizeof(length)
    );

    file.write(
        value.data(),
        static_cast<std::streamsize>(value.size())
    );
}
```

A key-value record can then be written as:

```cpp
write_string(file, key);
write_string(file, value);
```

The resulting layout is:

```text
[key length][key bytes][value length][value bytes]
```

Because the lengths are stored explicitly, keys and values may contain newlines.

#### Reading Bytes

To read a string, first read its length and then read exactly that many bytes:

```cpp
bool read_string(std::ifstream& file, std::string& result)
{
    std::uint32_t length{};

    if (!file.read(
            reinterpret_cast<char*>(&length),
            sizeof(length))) {
        return false;
    }

    result.resize(length);

    if (!file.read(
            result.data(),
            static_cast<std::streamsize>(length))) {
        return false;
    }

    return true;
}
```

A sequence of key-value records can then be read with:

```cpp
std::string key;
std::string value;

while (read_string(file, key)) {
    if (!read_string(file, value)) {
        std::cerr << "Incomplete database record\n";
        break;
    }

    // Process key and value.
}
```

`std::ios::binary` does not automatically serialize C++ objects. It only controls how the stream
interacts with the file. The program must still define its own file format.

For example, do not write the `std::string` object itself:

```cpp
// Do not do this.
file.write(
    reinterpret_cast<const char*>(&value),
    sizeof(value)
);
```

A `std::string` object contains internal bookkeeping and possibly a pointer, not just its character
data. The length-prefix approach is a reasonable format for learning. A more durable format would
eventually define its byte order, maximum record sizes, format version, and corruption detection.

### Using `std::fstream` for Both Reading and Writing

`std::fstream` supports reading and writing through the same stream object:

```cpp
#include <fstream>

std::fstream file{
    path,
    std::ios::in |
    std::ios::out |
    std::ios::binary
};
```

The flags mean:

- `std::ios::in` allows reading.
- `std::ios::out` allows writing.
- `std::ios::binary` performs binary I/O without text translation.

One important detail is that opening an `std::fstream` with `in | out` normally requires the file
to already exist.

#### Creating the File If It Is Missing

A safe approach is to create a missing file first and then reopen it for both reading and writing:

```cpp
std::fstream file{
    path,
    std::ios::in |
    std::ios::out |
    std::ios::binary
};

if (!file) {
    std::ofstream create_file{
        path,
        std::ios::binary
    };

    if (!create_file) {
        throw std::runtime_error{"Could not create database file"};
    }

    create_file.close();

    file.open(
        path,
        std::ios::in |
        std::ios::out |
        std::ios::binary
    );

    if (!file) {
        throw std::runtime_error{"Could not open database file"};
    }
}
```

Do not add `std::ios::trunc` merely to make a file get created:

```cpp
// Dangerous for an existing database: this erases its contents.
std::ios::in | std::ios::out | std::ios::trunc
```

#### Separate Read and Write Positions

An `std::fstream` maintains two positions:

- The get position controls reading.
- The put position controls writing.

Use `seekg()` to position the reader:

```cpp
file.clear();
file.seekg(offset, std::ios::beg);
file.read(buffer, byte_count);
```

Use `seekp()` to position the writer:

```cpp
file.clear();
file.seekp(0, std::ios::end);
file.write(data, byte_count);
file.flush();
```

The names can be remembered as:

```text
seekg -> seek the get/read position
seekp -> seek the put/write position
```

#### Why Call `clear()`?

Reading to the end of a file sets the stream's EOF and failure flags. Further seeking or reading
may fail until those state flags are reset:

```cpp
file.clear();
file.seekg(offset);
```

`clear()` does not erase the file. It only resets the stream's error-state flags.

#### Appending Records

For an append-only log, explicitly move the write position to the end before writing:

```cpp
file.clear();
file.seekp(0, std::ios::end);

write_string(file, key);
write_string(file, value);

file.flush();
```

Another option is to open with `std::ios::app`:

```cpp
std::ios::in |
std::ios::out |
std::ios::binary |
std::ios::app
```

This normally creates the file if it is missing and forces every write to the end. The tradeoff is
that it cannot overwrite an earlier position, even after calling `seekp()`. Opening without `app`
and explicitly seeking to the end is more flexible if the database may later update headers,
metadata, or an index at specific offsets.

#### Limitations of Editing a File

An `fstream` can overwrite existing bytes:

```cpp
file.seekp(offset);
file.write(new_data, new_data_size);
```

However, it cannot directly insert or remove bytes in the middle while automatically shifting all
the following bytes. If the replacement data has a different size, common approaches include:

- Appending a new record and marking the old record obsolete.
- Rewriting the contents into a new temporary file.
- Using fixed-size records or pages.
- Maintaining offsets and free-space metadata.

For an append-only database, reading with `seekg()` and adding records using `seekp()` is a natural
use of `std::fstream`.

### Reusing Helpers with Different Stream Types

It is fine to use `std::ifstream`, `std::ofstream`, and `std::fstream` in the same project. A helper
should generally accept the most general stream interface that provides the capability it needs,
rather than a particular kind of file stream.

For example, a function that only reads should accept `std::istream&` instead of
`std::ifstream&`:

```cpp
bool read_string(std::istream& stream, std::string& result)
{
    std::uint32_t length{};

    if (!stream.read(
            reinterpret_cast<char*>(&length),
            sizeof(length))) {
        return false;
    }

    result.resize(length);

    if (!stream.read(
            result.data(),
            static_cast<std::streamsize>(length))) {
        return false;
    }

    return true;
}
```

Both `std::ifstream` and `std::fstream` provide the `std::istream` interface, so either one can be
passed to this function:

```cpp
std::ifstream input_file{path, std::ios::binary};
std::fstream database_file{
    path,
    std::ios::in | std::ios::out | std::ios::binary
};

std::string value;

read_string(input_file, value);
read_string(database_file, value);
```

Similarly, a helper that only writes should accept `std::ostream&`:

```cpp
void write_string(
    std::ostream& stream,
    const std::string& value)
{
    const auto length =
        static_cast<std::uint32_t>(value.size());

    stream.write(
        reinterpret_cast<const char*>(&length),
        sizeof(length)
    );

    stream.write(
        value.data(),
        static_cast<std::streamsize>(value.size())
    );
}
```

This function accepts either an `std::ofstream` or an `std::fstream`:

```cpp
write_string(output_file, value);
write_string(database_file, value);
```

The stream hierarchy is approximately:

```text
          std::istream   std::ostream
                 \       /
                  \     /
                 std::iostream
                       ^
                  std::fstream
```

`std::fstream` derives from `std::iostream`, which combines the input and output stream interfaces.
Consequently, the same `std::fstream` object can be passed to a function expecting either an
`std::istream&` or an `std::ostream&`:

```cpp
std::fstream file{
    path,
    std::ios::in |
    std::ios::out |
    std::ios::binary
};

std::string value;

read_string(file, value);   // fstream used as an istream
write_string(file, value);  // fstream used as an ostream
```

A useful general rule is:

- A function that only reads should accept `std::istream&`.
- A function that only writes should accept `std::ostream&`.
- A function that genuinely needs to read and write can accept `std::iostream&`.
- Use `std::ifstream`, `std::ofstream`, or `std::fstream` when opening a particular file.

Using the general interfaces also makes helpers easier to test with `std::stringstream`, without
creating a real file. When one `std::fstream` is used for both operations, remember to manage its
read and write positions with `seekg()` and `seekp()`.

### `std::streamsize`

`std::streamsize` is a signed integer type used by C++ streams to represent a number of characters
or bytes.

For example, `write()` is approximately declared as:

```cpp
std::ostream& write(
    const char* data,
    std::streamsize count
);
```

Therefore, this code:

```cpp
file.write(
    value.data(),
    static_cast<std::streamsize>(value.size())
);
```

means: "Write `value.size()` bytes starting at `value.data()`."

The cast is needed because `value.size()` returns `std::size_t`, an unsigned size type, while
`write()` expects `std::streamsize`, a signed stream-size type. The precise underlying integer type
used for `std::streamsize` depends on the C++ implementation, but it is commonly similar to `long`
or `long long`.

`std::streamsize` describes an amount of data, not a location in a file. Related stream types
include:

- `std::streamsize`: a number of bytes or characters.
- `std::streamoff`: a distance between file positions.
- `std::streampos`: a specific position in a file.

For ordinary strings, the conversion can be written as:

```cpp
const auto count =
    static_cast<std::streamsize>(value.size());

file.write(value.data(), count);
```

For extremely large data, production code should first verify that `value.size()` fits inside
`std::streamsize` before performing the cast.

### `static_cast` and `reinterpret_cast`

C++ casts use this general form:

```cpp
cast_name<TargetType>(expression)
```

They explicitly tell the compiler to convert an expression to another type, or to view it as
another type.

#### `static_cast`

Use `static_cast` for ordinary conversions that the compiler understands:

```cpp
double price = 12.75;
int whole = static_cast<int>(price); // 12
```

In the binary-writing example:

```cpp
file.write(
    value.data(),
    static_cast<std::streamsize>(value.size())
);
```

`value.size()` returns `std::size_t`, while `write()` expects `std::streamsize`. The cast makes
that integer conversion explicit.

Likewise:

```cpp
std::uint32_t length =
    static_cast<std::uint32_t>(value.size());
```

This converts the string length to `std::uint32_t`. Care is required because information can be
lost if the original value is too large for the destination type.

Think of `static_cast` as: "Perform a normal type conversion that the compiler understands."

#### `reinterpret_cast`

`reinterpret_cast` is a lower-level operation. It generally does not convert the underlying data.
Instead, it tells the compiler to view the same memory through a different type.

For example:

```cpp
std::uint32_t length = 42;

file.write(
    reinterpret_cast<const char*>(&length),
    sizeof(length)
);
```

`&length` has the type `std::uint32_t*`, but `std::ostream::write()` expects a `const char*` pointing
to bytes. The cast means: "Take the address of `length` and view that address as a pointer to its
individual bytes."

The integer is not converted into the text `"42"`. Its raw memory bytes are written directly.

Reading uses the same idea in the opposite direction:

```cpp
std::uint32_t length{};

file.read(
    reinterpret_cast<char*>(&length),
    sizeof(length)
);
```

Here, `read()` places bytes from the file into the memory occupied by `length`. C++ specifically
allows an object's raw bytes to be accessed through `char*` or `const char*`.

A `static_cast` cannot be used here because `std::uint32_t*` and `const char*` are unrelated pointer
types. Changing how an address is interpreted requires `reinterpret_cast`.

##### Why `read()` Needs a `char*`

Consider this code:

```cpp
std::uint32_t length{};

if (!file.read(
        reinterpret_cast<char*>(&length),
        sizeof(length))) {
    return false;
}
```

`length` is already a `std::uint32_t`, so `&length` already has the type `std::uint32_t*`. However,
`file.read()` does not accept a `std::uint32_t*`; its declaration is approximately:

```cpp
std::istream& read(
    char* destination,
    std::streamsize byte_count
);
```

The stream works with raw bytes, so it requires a `char*`. This expression:

```cpp
reinterpret_cast<char*>(&length)
```

means: "Treat the address of `length` as the address of its first byte." Then `sizeof(length)` tells
`read()` how many bytes to copy into the memory occupied by `length`, normally four bytes for a
`std::uint32_t`.

Using `reinterpret_cast<std::uint32_t*>(&length)` would not help because the result would still be a
`std::uint32_t*`, while `read()` requires a `char*`.

Reading uses `char*` because `read()` modifies the destination memory. Writing instead uses
`const char*`:

```cpp
reinterpret_cast<const char*>(&length)
```

This is because `write()` only examines the source memory and does not modify it.

Finally, `read()` returns the stream itself. Applying `!` to that result checks the stream state:

```cpp
if (!file.read(...)) {
    return false;
}
```

The condition is entered if all the requested bytes could not be read, such as when the stream
reaches the end of the file or encounters an incomplete record.

In summary:

- Use `static_cast<T>(value)` for ordinary, intentional type conversions.
- Use `reinterpret_cast<T>(value)` for deliberate low-level operations involving memory and pointer
  representations.
- Prefer `static_cast` when possible. Use `reinterpret_cast` carefully because it is easier to
  misuse.
