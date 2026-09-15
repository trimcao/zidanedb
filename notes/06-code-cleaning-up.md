# Code Cleaning Up

The `Database`/`Index` split is a reasonable foundation. The next priorities are
reliable file I/O and smaller, reusable helpers.

Work through the items below one at a time. The first five address correctness
and error handling; the remaining items improve organization and maintainability.

## 1. Check Every Index Read

In [`src/index.cpp`](../src/index.cpp), `find_entry_offset()` and other read paths
ignore several `read_uint64()` and `read_string()` results. A truncated file can
therefore look like a missing key, or leave entry fields uninitialized or stale.
The surrounding `catch` does not handle ordinary stream failures unless stream
exceptions are enabled.

Choose a consistent contract: `std::nullopt` means "key absent"; an exception
means "could not read the database." Currently,
[`Database::get()`](../src/database.cpp) also returns `nullopt` for file errors,
making these cases indistinguishable.

- [ ] Check every seek and read, or enable stream exceptions where appropriate.
- [ ] Distinguish a missing key from an I/O error or invalid record.
- [ ] Apply the same error-handling contract throughout `Database` and `Index`.

## 2. Make Successful Construction Guarantee a Usable Index

[`Index::load()` and `setup()`](../src/index.cpp) print errors and return, allowing
construction to finish after failure. `load()` also reads the magic and version
without checking their values.

Validate the magic, supported version, positive bucket count, and sufficient
file size. Reject zero buckets in the constructor: otherwise,
`hash % num_buckets_` divides by zero.

- [ ] Validate the requested bucket count when creating an index.
- [ ] Validate the magic, version, bucket count, and file size when loading one.
- [ ] Throw when loading or initialization fails, including failed writes and
  flushes during setup.

## 3. Review the Ordering in `Database::erase()`

[`Database::erase()`](../src/database.cpp) removes the index entry before appending
the deletion record. If the database write fails, the function throws but the
key has already disappeared from the index.

Establish the same record-first ordering used by `put()`: check existence,
append the deletion record, then update the index. This improves ordinary
failure behavior; making both files recover consistently after interruption is
a separate recovery task.

- [ ] Check whether the key exists before changing either file.
- [ ] Append and flush the deletion record before updating the index.
- [ ] Document the remaining failure cases between the two writes for the
  future recovery design.

## 4. Handle Statistics for an Empty Index

[`Index::stats()`](../src/index.cpp) divides by `non_empty_buckets`, which is zero
for a new database or after deleting every key. The floating-point result can
be NaN.

Define the empty average as zero. Also document that this average measures
chain length among occupied buckets, whereas load factor includes every bucket.

- [ ] Return an average chain length of zero when there are no occupied buckets.
- [ ] Document the meaning of `avg_chain_length` in
  [`IndexStats`](../include/zidanedb/index_stats.h).
- [ ] Consider accumulating the entry count as an integer, then converting it
  for the final average calculation.

## 5. Make the Database/Index Filename Relationship Unambiguous

The [`Database` constructors](../src/database.cpp) replace the database's
extension with `.zidx`. Consequently, `data.zdb` and `data.backup` share
`data.zidx`; passing `data.zidx` makes the database and index paths identical.

Either enforce a database filename convention or derive an index filename that
remains distinct. Also decide what opening an existing database with a missing
index means: currently it creates an empty index, making previous records
inaccessible through `get()`.

- [ ] Choose and document the filename convention.
- [ ] Prevent the database and index from using the same file.
- [ ] Prevent distinct supported database names from accidentally sharing an
  index file.
- [ ] Define behavior for a missing companion file. Until recovery exists,
  reporting an error is a possible policy.

## 6. Open the Index Once per Operation and Reuse the Stream

[`find_entry_offset()`](../src/index.cpp) opens the file, then its caller opens
it again. Have the caller open the stream and pass it into the helper.

Lookup and statistics currently request write access despite only reading.
Use an input stream for those operations and make `Index::stats()` `const`.
This also lets reads work when the index file is read-only.

- [ ] Pass an already-open stream into the entry lookup helper.
- [ ] Use input-only access for lookup and statistics.
- [ ] Mark `Index::stats()` as `const` in its declaration and definition.

## 7. Centralize Binary-Format Details

Header-size calculations and entry-reading sequences repeat in
[`src/index.cpp`](../src/index.cpp). Small helpers such as `header_size()` and
`read_entry()` would give the code one place to maintain the layout and check
errors.

In [`src/utils.cpp`](../src/utils.cpp), `string_size()` narrows the serialized
size to `uint32_t`. Use a sufficiently wide size type, and separately validate
that strings fit the 32-bit length prefix before writing. Validate lengths
before allocating memory when reading.

- [ ] Centralize the header-size calculation.
- [ ] Centralize entry decoding and its error checks.
- [ ] Use a sufficiently wide return type for the serialized string size.
- [ ] Reject strings whose lengths cannot fit the on-disk length prefix.
- [ ] Validate stored lengths before resizing a string to read its contents.

## 8. Remove Small Sources of Duplication and Clutter

- [ ] Delegate one [`Database` constructor](../src/database.cpp) to the other,
  and use initializer lists.
- [ ] Include `"utils.h"` in [`src/utils.cpp`](../src/utils.cpp), so the compiler
  checks definitions against declarations.
- [ ] Include `<string_view>` explicitly in [`src/utils.h`](../src/utils.h).
- [ ] Remove the unused `IndexEntry` and `get_start_entry_offset()` in
  [`src/index.h`](../src/index.h) if they are not needed yet.
- [ ] Remove the unused `WorkloadOptions` in
  [`apps/matrix/matrix.h`](../apps/matrix/matrix.h) if it is not needed yet.
- [ ] Remove commented-out maps and historical implementation comments. Keep
  comments explaining the file format and invariants.
- [ ] Consider `const std::string&` for `put()`/`set()` arguments that are only
  read; they currently copy strings without retaining ownership.

## Tests to Add Alongside the Cleanup

The [existing tests](../tests/database_test.cpp) cover persistence, collisions,
deletion positions, and empty values. Add focused tests as each behavior is
clarified or corrected:

- [ ] Statistics for a new, empty database.
- [ ] Statistics after deleting every key.
- [ ] Rejection of zero buckets.
- [ ] Invalid magic or unsupported index version.
- [ ] Truncated index headers, bucket tables, and entries.
- [ ] Missing companion files.
- [ ] Filename collisions or invalid names, according to the chosen convention.
- [ ] Reading an index without write permission.

The existing assertion that the index file size is greater than zero is now
weak: construction alone writes the bucket table. Consider checking file
growth when testing that a new entry was appended; keep the reopen-and-read
checks as well.

- [ ] Strengthen the index-entry persistence test's file-size assertion.

## Review Verification

The review included a syntax-only compiler check on the core sources. It passed,
with conversion warnings in statistics and `string_size()`. Tests and workloads
that create database files were not run during the review. These notes describe
suggested work; they do not indicate that the cleanup has been implemented.
