# Code Cleaning Up

The `Database`/`Index` split is a reasonable foundation. The next priorities are
reliable file I/O and smaller, reusable helpers.

Work through the items below one at a time. The first five address correctness
and error handling; the remaining items improve organization and maintainability.

Checklist updated after reviewing commit `96d4926` on 2026-09-16. Original issues
are described in the past tense; checked boxes reflect the current code.
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

An existing database with no index is now rejected. An index with entry bytes
but no database is also rejected, fixing the stale-offset problem reproduced in
the previous review. A pristine index without a database is allowed, since the
database file is created lazily on the first successful write.

- [x] Choose and document the filename convention.
- [x] Prevent the database and index from using the same filename.
- [x] Prevent distinct supported database names from accidentally sharing an
  index file.
- [x] Define behavior for a missing companion file. Missing companions are
  rejected except for a pristine index before the first database write.

One related contract needs clarification: `Index::empty()` currently compares
file size with the header-plus-buckets size. Since erasing entries does not
shrink the file, `put("key", "value")` followed by `erase("key")` leaves zero
occupied buckets but `empty()` still returns false. Consequently, a missing
database is rejected even when its index has no live keys. This is conservative,
but "pristine file" and "logically empty index" are different concepts.

- [ ] Clarify `Index::empty()` and test the erase-all case. Either inspect bucket
  heads to implement logical emptiness, or rename the helper to express the
  pristine-file check and explicitly retain the stricter missing-data policy.

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
- [ ] Include `<string_view>` explicitly in [`src/utils.h`](../src/utils.h).
- [x] Reassess the previously unused `IndexEntry` and
  `index_size_before_entries()` in [`src/index.h`](../src/index.h). Both are now
  used, so keep them. Revisit the size helper only if changing `empty()` makes
  it unused again.
- [ ] Remove the unused `WorkloadOptions` in
  [`apps/matrix/matrix.h`](../apps/matrix/matrix.h) if it is not needed yet.
- [ ] Remove the unused `std::string k` local in `Index::stats()`.
- [x] Remove commented-out maps and historical implementation comments. Keep
  comments explaining the file format and invariants. Teaching comments and code
  examples are preserved in [C++ syntax notes](misc/cpp-code-syntax.md).
- [x] Use `const std::string&` for the read-only `Database::put()` and
  `Index::set()` string arguments.

## Tests to Add Alongside the Cleanup

The [existing tests](../tests/database_test.cpp) cover persistence, collisions,
deletion positions, and empty values. Add focused tests as each behavior is
clarified or corrected:

- [x] Statistics for a new, empty database.
- [x] Statistics after deleting every key.
- [ ] Rejection of zero buckets.
- [ ] Invalid magic or unsupported index version.
- [ ] Truncated index headers, bucket tables, and entries.
- [x] Missing companion files: missing index, missing data with a populated
  index, and a pristine index before the first write.
- [x] Filename collisions or invalid names, according to the chosen convention.
- [ ] Reading an index without write permission.

The existing assertion that the index file size is greater than zero is now
weak: construction alone writes the bucket table. Consider checking file
growth when testing that a new entry was appended; keep the reopen-and-read
checks as well.

- [ ] Strengthen the index-entry persistence test's file-size assertion.

Record the index size after constructing `Database` but before `put()`, then
assert that it grows after inserting a new key. Keep the existing reopen/read
checks. The current `file_size(...) > 0` would pass for an index with only its
header and empty bucket table.

The string-limit and serialization regression tests in
[`database_test.cpp`](../tests/database_test.cpp) and
[`utils_test.cpp`](../tests/utils_test.cpp) are already present and passing.

## Review Verification

### Current Review — 2026-09-16

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
