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