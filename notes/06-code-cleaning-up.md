# Code Cleaning Up

The `Database`/`Index` split is a reasonable foundation. The next priorities are
reliable file I/O and smaller, reusable helpers.

Work through the items below one at a time. The first five address correctness
and error handling; the remaining items improve organization and maintainability.

Checklist updated after reviewing commit `3e3c937` and the current working tree
on 2026-09-16. Original issues are described in the past tense; checked boxes
reflect the current code.
Regression-test additions are tracked separately: a successful temporary probe
does not count as a test added to the repository.

## 1. Check Every Index Read

Originally, `find_entry_offset()` and other paths in
[`src/index.cpp`](../src/index.cpp) ignored read results, allowing truncation to
look like a missing key or leave entry fields uninitialized. Reads are now
checked explicitly or protected by stream exceptions. In particular, the
`read_uint64()` in `Index::erase()` is covered by its `failbit | badbit` exception
mask even though the boolean return value is not used.

The current contract is consistent: `std::nullopt` means "key absent"; an
exception means "could not read the database." `Database::get()` no longer
returns `nullopt` for a failed database read.

- [x] Check every seek and read, or enable stream exceptions where appropriate.
- [x] Distinguish a missing key from an I/O error or invalid record.
- [x] Apply the same error-handling contract throughout `Database` and `Index`.

## 2. Make Successful Construction Guarantee a Usable Index

Originally, [`Index::load()` and `setup()`](../src/index.cpp) printed errors and
returned, allowing construction to finish after failure. They now throw on
failure, and `load()` validates the header values and bucket-table capacity.

Validate the magic, supported version, positive bucket count, and sufficient
file size. Reject zero buckets in the constructor: otherwise,
`hash % num_buckets_` divides by zero.

- [x] Validate the requested bucket count when creating an index.
- [x] Validate the magic, version, bucket count, and file size when loading one.
- [x] Throw when loading or initialization fails, including failed writes and
  flushes during setup.

## 3. Review the Ordering in `Database::erase()`

Originally, [`Database::erase()`](../src/database.cpp) removed the index entry
before appending the deletion record. A failed database write could therefore
leave the key missing from the index without a deletion record.

Establish the same record-first ordering used by `put()`: check existence,
append the deletion record, then update the index. This improves ordinary
failure behavior; making both files recover consistently after interruption is
a separate recovery task.

- [x] Check whether the key exists before changing either file.
- [x] Append and flush the deletion record before updating the index.
- [x] Document the remaining failure cases between the two writes for the
  future recovery design. The comments at the `put()`/`erase()` index updates
  explain the unindexed-put and still-visible-deletion cases.

Keep those short comments next to the ordering-sensitive code. Put the fuller
design in the existing [API Design and Recovery note](07-api-design-and-recovery.md),
under its currently empty `Recovery` heading. That discussion should cover
partial data records, partial index updates, restart/replay rules, and durability
limits: flushing a C++ stream is not an atomic transaction or a power-loss
durability guarantee. The basic documentation checkbox is complete; implementing
recovery is a separate project, not a requirement for finishing this checklist.

## 4. Handle Statistics for an Empty Index

Originally, [`Index::stats()`](../src/index.cpp) divided by `non_empty_buckets`
even when it was zero. It now leaves the average at zero for an empty index and
only divides when there are occupied buckets.

Define the empty average as zero. Also document that this average measures
chain length among occupied buckets, whereas load factor includes every bucket.

- [x] Return an average chain length of zero when there are no occupied buckets.
- [x] Document the meaning of `avg_chain_length` in
  [`IndexStats`](../include/zidanedb/index_stats.h).
- [x] Consider accumulating the entry count as an integer, then converting it
  for the final average calculation.

## 5. Make the Database/Index Filename Relationship Unambiguous

The old constructors replaced the database extension with `.zidx`, causing
filename collisions. The current [`Database` constructor](../src/database.cpp)
requires `.zdb` and appends `.idx`, so `data.zdb` belongs with `data.zdb.idx`.

An existing database with no index is now rejected. An index with live entries
but no database is also rejected, fixing the stale-offset problem reproduced in
the previous review. A logically empty index without a database is allowed,
including after erasing every key, since no live offsets need a data file.

- [x] Choose and document the filename convention.
- [x] Prevent the database and index from using the same filename.
- [x] Prevent distinct supported database names from accidentally sharing an
  index file.
- [x] Define behavior for a missing companion file. Missing companions are
  rejected except for a logically empty index without a database file.

`Index::empty()` now reads the bucket heads instead of comparing file sizes.
The erase-all regression verifies that it returns true even though deleted
entries still occupy space in the file. Another regression verifies that such
an index can reopen without its old database and accept new writes safely.

- [x] Clarify `Index::empty()` and test the erase-all case. Emptiness means all
  bucket heads are zero, independently of the file's size.
- [x] Make `Index::empty()` report a missing index file as an error instead of
  returning true. The early `!exists(path_)` branch was removed during review;
  the checked file open now throws, consistently with `find()` and `stats()`.
  The missing-file regression passes.

## 6. Open the Index Once per Operation and Reuse the Stream

Originally, [`find_entry_offset()`](../src/index.cpp) opened its own file while
its caller also opened one. The caller now passes an existing stream to the
helper.

Lookup opens its `fstream` with `std::ios::in | std::ios::binary`; statistics
uses `ifstream` and is `const`. Neither requests write access. Read-only-index
lookup and statistics also passed the temporary review probe.

- [x] Pass an already-open stream into the entry lookup helper.
- [x] Use input-only access for lookup and statistics.
- [x] Mark `Index::stats()` as `const` in its declaration and definition.

## 7. Centralize Binary-Format Details

Header-size calculations and full entry-reading sequences were duplicated in
[`src/index.cpp`](../src/index.cpp). The const `header_size()` and `read_entry()`
helpers now centralize these details, including checked entry decoding used by
chain lookup and statistics.

In [`src/utils.cpp`](../src/utils.cpp), `string_size()` now returns `uint64_t`.
`write_string()` checks length before converting to the 32-bit prefix; its
`uint32_t max_length` cannot exceed that prefix's capacity. `read_string()`
rejects an oversized stored length before resizing the result. `Database::put()`
also validates key/value limits before opening or modifying either file.

- [x] Centralize the header-size calculation.
- [x] Centralize entry decoding and its error checks.
- [x] Use a sufficiently wide return type for the serialized string size.
- [x] Reject strings whose lengths cannot fit the on-disk length prefix.
- [x] Validate stored lengths before resizing a string to read its contents.

## 8. Remove Small Sources of Duplication and Clutter

- [x] Remove duplication between [`Database` constructors](../src/database.cpp)
  and use initializer lists. A single constructor with a default argument
  achieves this; delegation is no longer necessary.
- [x] Include `"utils.h"` in [`src/utils.cpp`](../src/utils.cpp), so the compiler
  checks definitions against declarations.
- [x] Include `<string_view>` explicitly in [`src/utils.h`](../src/utils.h).
- [x] Reassess the previously unused `IndexEntry` and
  `index_size_before_entries()` in [`src/index.h`](../src/index.h). Keep the
  actively used `IndexEntry`; the size helper's declaration and definition have
  now been removed because the bucket-scanning `empty()` no longer needs it.
- [x] Remove the unused `WorkloadOptions` in
  [`apps/matrix/matrix.h`](../apps/matrix/matrix.h).
- [x] Remove the unused `std::string k` local in `Index::stats()`.
- [x] Remove commented-out maps and historical implementation comments. Keep
  comments explaining the file format and invariants. Teaching comments and code
  examples are preserved in [C++ syntax notes](misc/cpp-code-syntax.md).
- [x] Use `const std::string&` for the read-only `Database::put()` and
  `Index::set()` string arguments.

## Tests to Add Alongside the Cleanup

The [database tests](../tests/database_test.cpp) and
[index tests](../tests/index_test.cpp) now cover all the cleanup cases below,
alongside persistence, collisions, deletion positions, and empty values:

- [x] Statistics for a new, empty database.
- [x] Statistics after deleting every key.
- [x] Rejection of zero buckets: invalid `Index`/`Database` constructor arguments
  create no files, and a zero count stored in an otherwise complete header is
  rejected on reopening.
- [x] Invalid magic or unsupported index version. The tests change one magic
  byte without changing its length, and exercise both version zero and a newer
  unsupported version.
- [x] Truncated index headers, bucket tables, and entries. See the `[truncation]`
  cases in [`index_test.cpp`](../tests/index_test.cpp), including a table
  truncated after successful construction.
- [x] Missing companion files: missing index, missing data with a populated
  index, and a logically empty index before the first write or after erase-all.
- [x] Filename collisions or invalid names, according to the chosen convention.
- [x] Reading an index without write permission. Reopening, lookup, statistics,
  and `empty()` work for empty and populated read-only indexes. The test first
  verifies that write access is actually denied and restores permissions on
  scope exit. It skips if the current user/filesystem bypasses permission bits.

The old assertion that the index file size was greater than zero was too weak:
construction alone writes the bucket table. The persistence test now verifies
actual growth while retaining the reopen-and-read checks.

- [x] Strengthen the index-entry persistence test's file-size assertion.

The test records index size after constructing `Database` but before `put()`,
then requires a larger file after inserting a new key. An index containing only
its header and empty bucket table can no longer satisfy this assertion.

The invalid-header tests use a read/write `fstream` to overwrite one existing
field, then verify that the file size is unchanged. Unlike the truncation tests,
these inputs still contain all their bytes; the stored metadata itself is wrong.

The string-limit and serialization regression tests in
[`database_test.cpp`](../tests/database_test.cpp) and
[`utils_test.cpp`](../tests/utils_test.cpp) are already present and passing.

The truncation tests first create a valid temporary index, then call
`std::filesystem::resize_file(path, bytes_to_keep)` with a smaller size. This
preserves the prefix and removes the tail without changing stored lengths or
bucket offsets. Each `SECTION` starts from a fresh valid file. Header and table
truncation is rejected while reopening; entry truncation is detected when
lookup, statistics, set, or erase attempts to read that entry.

See the [truncated-input appendix](#appendix-truncated-input-tests) for examples
and commands to run individual cases.

## Review Verification

### Remaining Cleanup Tests — 2026-09-16

- Added five test cases for zero requested/stored bucket counts, invalid
  magic/version, and read-only index access; strengthened the existing
  index-entry persistence test's growth assertion.
- Built `database_tests` and ran CTest in an isolated temporary directory:
  **47/47 tests passed**. The permission test ran as a non-root user and was not
  skipped; both its empty and populated sections passed.
- The focused `[validation],[permissions],[persistence]` run passed all
  **44 assertions in 6 test cases**.
- Formatting and `git diff --check` passed. All items in the cleanup-test
  section are complete. Production code was not changed by the assistant.

### Logical Emptiness and Truncation Follow-up — 2026-09-16

- Verified the bucket-scanning `Index::empty()` implementation and completed
  section 8, including the removal of the now-unused size helper during review.
- Added four truncation test cases (86 assertions across their sections):
  incomplete headers, incomplete bucket tables, a table shortened after
  construction, and incomplete entry fields/payloads. All pass.
- Added empty-index tests for first/last bucket occupancy, erase-all with
  retained file bytes, a missing index file, and reopening after erase-all with
  a missing data file. All pass after the missing-file guard was removed during
  review.
- Full suite result: **42/42 tests pass**. The assistant changed only tests,
  test build wiring, and this checklist; production fixes were made by the user.

### Earlier Full Checklist Review — 2026-09-16

- Built all targets and ran CTest in a fresh temporary directory: **34/34 tests
  passed**, including the previously failing missing-database regression.
- Temporary probes confirmed rejection of zero requested/stored bucket counts,
  invalid magic/version, truncated headers/bucket tables, and truncated entries
  during lookup/statistics. These are diagnostic probes, not committed tests;
  the corresponding test checkboxes remain open.
- A temporary read-only-index probe confirmed both lookup and statistics work.
  The process was non-root, and opening the same file for writing was verified
  to fail. A permanent permission regression test is still missing.
- Reproduced the distinction between physical and logical emptiness: after
  erasing the last key, statistics report zero occupied buckets while
  `Index::empty()` reports false. The missing-data constructor rejects this
  state under the current conservative policy.
- Extra-warning syntax checks passed, with conversion warnings still present
  around stream offsets, magic-string lengths, the statistics division, and
  hash-byte conversion. These are additional hardening opportunities, not
  failures of the regular build.
- Only this checklist was edited in the repository during this review; source
  and regression-test changes are left to the user.

### Earlier Review — 2026-09-15

The follow-up review on 2026-09-15 confirmed the four fixes requested in the
previous review:

- `setup()` enables stream exceptions before writing and flushing.
- `load()` checks bucket capacity using division, avoiding multiplication
  overflow. Failed header reads are rejected before this check.
- `constants.h` includes `<cstdint>` and has an include guard.
- `Database::erase()` checks existence through the index without reading the
  entire stored value.

Verification performed:

- Rebuilt `database_tests` and ran CTest: all 15 existing tests passed. Test
  database files were isolated in a fresh temporary directory.
- The core sources passed syntax-only compilation with additional warnings
  enabled. Conversion warnings remain in statistics and `string_size()`.
- A standalone compiler check including `constants.h` twice passed.
- No source or test files were edited during the review. The test additions
  listed above remain unchecked because they have not been added to the suite;
  failure-injection tests and workloads were not run.

## Appendix: Truncated Input Tests

The tests in [`index_test.cpp`](../tests/index_test.cpp) create malformed inputs
by starting with a valid temporary index and removing bytes from its end.
Only dedicated temporary test files should be shortened this way, never real
database or index files.

### Creating a Truncated Input

The basic pattern is:

1. Create a valid index using the normal `Index` constructor and, when needed,
   `set()`.
2. Shorten the file while preserving the bytes at its beginning.
3. Verify that reopening or reading the incomplete data throws an exception.

For example, inside a test using the temporary-file helper:

```cpp
const auto complete_size = std::filesystem::file_size(file.path());
REQUIRE(complete_size > 0);

// Remove the final byte from this temporary test file.
std::filesystem::resize_file(file.path(), complete_size - 1);
```

`resize_file(path, n)` sets the file's size to `n` bytes. When `n` is smaller
than its current size, it keeps the first `n` bytes and discards the tail. It
does **not** mean "remove `n` bytes." The tests check that their target size is
smaller so they truncate rather than extend the file.

The important detail is that stored lengths, bucket counts, and offsets remain
unchanged. For a serialized key, removing the final byte produces this mismatch:

```text
Before: length = 3, bytes = "key"
After:  length = 3, bytes = "ke"
```

The reader still expects three bytes, but only two remain. The length prefix
is binary, not the text `"3"`; this illustration shows its decoded value.
The index read should report an error, not treat the damaged entry as a missing
key or accept a partial key.

### Choosing Where to Cut

The tests cover three parts of the index format:

- **Headers:** cut inside the magic-length prefix, magic bytes, version, or
  bucket count. An additional case removes the entire header.
- **Bucket tables:** preserve a complete header declaring three buckets, but
  remove some of the promised bucket bytes.
- **Entries:** preserve the header and bucket pointer, but cut inside an
  entry's database offset, next-entry offset, key-length prefix, or key bytes.
  Another case removes the whole entry while retaining its bucket pointer.

For a header field, calculate where the field ends and keep one byte less:

```cpp
const std::uintmax_t magic_end =
    sizeof(std::uint32_t) + zidanedb::INDEX_MAGIC.size();
const std::uintmax_t version_end = magic_end + sizeof(std::uint32_t);

// Retain the complete magic string and only part of the version field.
const auto bytes_to_keep = version_end - 1;
std::filesystem::resize_file(file.path(), bytes_to_keep);
```

This removes the final byte of the version **and everything after it**, including
the bucket count and bucket table. All earlier fields stay valid, isolating the
first incomplete field the reader encounters.

Each `SECTION` reruns the test from its beginning, creating a fresh valid file.
The cuts are independent; they are not applied successively to one damaged file.
The test helper handles filesystem cleanup, not Catch2 itself.

### When Should the Error Be Detected?

Header and bucket-table truncation is rejected while reopening:

```cpp
CHECK_THROWS_AS((zidanedb::Index{file.path(), 2}), std::runtime_error);
```

The constructor validates the header and bucket-table capacity, but does not
traverse every entry. Consequently, an index with a truncated entry can open;
the error must be detected when an operation reads that entry:

```cpp
zidanedb::Index reopened{file.path(), 1};
CHECK_THROWS_AS(reopened.find(key), std::runtime_error);
CHECK_THROWS_AS(reopened.stats(), std::runtime_error);
CHECK_THROWS_AS(reopened.set(key, 99), std::runtime_error);
CHECK_THROWS_AS(reopened.erase(key), std::runtime_error);
```

A separate test creates an `Index` successfully and then shortens its bucket
table. That verifies that `empty()`, lookup, and statistics check their own
reads instead of relying only on constructor validation.

### Running and Inspecting the Tests

From the project root, rebuild after changing test code:

```bash
cmake --build build --target database_tests
```

Run all truncation tests:

```bash
./build/database_tests "[truncation]"
```

Run one particular cut and display successful assertions:

```bash
./build/database_tests "Index operations reject truncated entries" \
  --section "key bytes are incomplete" -s
```

The entry test uses `CAPTURE(entry_start, bytes_to_keep)` to attach those values
to its assertions. They appear on failures by default; `-s` also displays them
with passing assertions, making it easier to see exactly where the file was cut.
