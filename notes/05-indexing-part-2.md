# Indexing (Part 2)

## Recap
Here is a quick summary of my ZidaneDB implementation so far:
- Currently ZidaneDB persists both the database and the index.
- During startup, the index is loaded into memory. This means the bigger
the database, the longer the load time. We basically have the same problem
as before.
- GET operation: get the db offset from the index (stored in memory),
and seek the db file.
- PUT operation: write to the db file, and edit the index file in-place.
Because now we also need to maintain the index file, `put` is a pretty
expensive operation.
- ERASE operation: very similar to the PUT op, we need to update both
the db file and the index file.


Let's talk about the current pain points:
- Startup time (load time) depends on the size of the database.
If we have a big database, startup time will take a long time.
- Put time also depends on the size of the database now, because
we need to edit the index file in-place so we need to scan the index
file to find the given key.
- It means it's impossible to work with 1 million keys at the moment.
And that's my next objective.

Thoughts on the `checkpoint` approach:
- Previously, we discussed and noticed a `checkpoint` approach, where
we read the db file at startup and build some kind of index.
- The idea is we can save the location of the last db file read, and
continue from there to save time.
- However, at startup we still need to load the whole index file (with
the current design), and we persist the index anyway, so probably
this approach does not help.

Thoughts on the design:
- Loading the whole index file does not sound right. I probably need
a different idea here.
- I can avoid loading the index file to memory. Index file gives me
the value offset on the db, so it only takes O(n) where n is equal to
number of keys. This is better than reading the db file directly, because
need to read the whole db file to make sure the value we get is the latest.
In other words, it's better than having only the db file, but still,
it's O(n).
- Editing the index file in-place also does not scale. But I also
cannot keep writing to the index file (like a log). Currently, this mechanism
makes it more expensive for PUT op, but allows startup time to be faster
in case there are a lot of overwrites in the db. Basically, PUT op is O(n)
at the moment.
- At startup: O(n) time, O(n) space. GET: O(1). PUT: O(n). ERASE: O(n).

Current matrix `perf-basic` results:
```
tri@fedora:~/tri/zidanedb$ ./build-release/matrix perf-basic -n 10000
Database file: "/tmp/matrix-performance.zdb"
Pairs:        10000
Put time:     1.18819 seconds
Put rate:     8416.18 ops/second
Load time:    0.000524127 seconds
Verify time:  0.0106359 seconds
Get rate:     940214 ops/second
File size:    257780 bytes

tri@fedora:~/tri/zidanedb$ ./build-release/matrix perf-basic -n 100000
Database file: "/tmp/matrix-performance.zdb"
Pairs:        100000
Put time:     116.935 seconds
Put rate:     855.176 ops/second
Load time:    0.00815008 seconds
Verify time:  0.107059 seconds
Get rate:     934064 ops/second
File size:    2777780 bytes
```

## Persistent Hash Index
What to do:
- Suggested by ChatGPT: create an abstraction for Index.
This should make the code easier to understand.
- I will implement persistent hash table index.

Persistent hash index design:
- The hash table will have 1,000,000 buckets.
(Remember my current objective is only 1,000,000 to 10,000,000 keys).
- There will be a header that shows bucket_count.
- Use some hash function and produce a hash value for a given key.
Use that hash value for PUT and GET ops. That should make it fast.
- No hash map (in memory) is needed, except for caching. But for now, we don't need
any cache. We can implement that later.
- How about collisions?

Handling collisions in the persistent hash index:
- One simple design is bucket chaining. But how to implement it?
I think I need go through the format of the hash index file.
- Suppose we have 1,000,000 buckets. Even if we have no collision, it's still
unclear how we can access the buckets cleanly, unless we have a fixed length
for each key, but that does not sound right.

The index file format:
- We start with a number of buckets, for now, it's 1,000,000.
- Each bucket has fixed size. Each bucket is just: bucket index -> index file offset.
- Note: the bucket does not store db offset, it's just index offset.
The index offset points to the index data: key -> db offset.
- Index offset = 0 means there's no key for that bucket.
- Before the buckets, the index file should have a header, with some fields
like: magic, version, bucket_count. `magic` is the field to show this file
is a ZidaneDB index file. `version` means the version of the index file format.
- The index data live after the fixed buckets.
- Each index entry has the format: [key-length][db-offset][next-entry][key-bytes].
- [next-entry] is the field that points to the next entry with the same hash
number. This is for handling collision, and we are using a simple bucket chaining.
- With new PUT: compute the hash value, check the bucket, go through the collision
chain if needed, create and write a new index entry, (update the index offset of
the bucket if it does not have any key yet).
- I need to decide whether to insert at the head or insert at the tail of the
collision chain (a linked list). Inserting at the head is ok.
- With overwrite PUT: same as before, but we just find the index entry we need,
then update the db-offset of that entry.
- With DELETE: to make it simple, remove the index entry for that key and update
the bucket index offset accordingly. This will be like a linked list deletion.
Garbage will be left behind, but we can do compaction later.
- Important: hash function must be stable. I need to choose a deterministic hash
algorithm.