# C++ Code Organization

Notes and examples for organizing C++ code. Add new topics as the project grows.

## Sharing Helper Functions Across Files

Put function declarations in a header and their implementations in a `.cpp` file.
These can remain ordinary functions; no class is needed.

For example, `write_string()` and `read_string()` are used by both `database.cpp`
and `index.cpp`. They can share one implementation through `src/binary_io.h` and
`src/binary_io.cpp`.

### Declare the Functions in a Header

In `src/binary_io.h`:

```cpp
#ifndef ZIDANEDB_BINARY_IO_H
#define ZIDANEDB_BINARY_IO_H

#include <iosfwd>
#include <string>

namespace zidanedb::binary_io {

void write_string(std::ostream& stream, const std::string& s);
bool read_string(std::istream& stream, std::string& result);

} // namespace zidanedb::binary_io

#endif
```

`<iosfwd>` provides the stream declarations needed for these function signatures.
The include guard prevents the header from being processed more than once within
the same translation unit.

### Define the Functions in a Source File

In `src/binary_io.cpp`:

```cpp
#include "binary_io.h"

#include <cstdint>
#include <istream>
#include <ostream>

namespace zidanedb::binary_io {

void write_string(std::ostream& stream, const std::string& s) {
    const auto length = static_cast<std::uint32_t>(s.size());

    stream.write(reinterpret_cast<const char*>(&length), sizeof(length));
    stream.write(s.data(), static_cast<std::streamsize>(s.size()));
}

// Move the existing read_string() implementation here too.

} // namespace zidanedb::binary_io
```

The example above shows the implementation of `write_string()`. The declaration
of `read_string()` in the header also needs a matching implementation in this
source file before it can be called.

### Use the Shared Functions

In both `database.cpp` and `index.cpp`, include the header and call the functions:

```cpp
#include "binary_io.h"

// Inside namespace zidanedb:
binary_io::write_string(file, key);

if (!binary_io::read_string(file, value)) {
    // Handle the failed read.
}
```

Remove the duplicated implementations from those files. Include `binary_io.h`,
not `binary_io.cpp`; the build system compiles the implementation separately.

### Use a Named Namespace for Shared Helpers

Helpers defined inside `namespace { ... }`, an anonymous namespace, are local to
their `.cpp` file. Use the named namespace `zidanedb::binary_io` for the shared
declarations and definitions, as shown above.

Functions used only within one `.cpp` file can stay in its anonymous namespace.
For example, the index's hash function can remain local to `index.cpp` if no
other file needs it.

### Add the Source File to CMake

Add `src/binary_io.cpp` to the existing `add_library(zidanedb ...)` source list:

```cmake
add_library(zidanedb
    src/database.cpp
    src/index.cpp
    src/binary_io.cpp
    # Keep the other existing entries.
)
```

The header makes the function declarations available to callers. Compiling and
linking `binary_io.cpp` provides their implementations.

### Keep Internal Helpers in `src/`

`read_uint32()`, `write_uint32()`, and the other binary I/O helpers can go into
the same pair of files. Keep them in `src/` because they are internal
implementation details. Headers intended for library users belong in
`include/zidanedb/`.

An existing `src/utils.h` could serve this purpose too; `binary_io.h` makes its
contents clearer.
