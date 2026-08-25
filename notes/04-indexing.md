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

## Binary Mode for Files