# Load Time Improvement

Few things to think about from the last note:
- Define the PUT and DELETE APIs. What does this mean: `PUT("player", "")`?
- Is `PUT` supposed to represent a state, or an event?
- Compaction: how do I reclaim obsolete records? How and when to trigger
compaction?

Here is what we currently have in terms of performance:
```
./build/matrix perf-basic --pairs 10000000
Pairs:        10000000
Put time:     222.476 seconds
Put rate:     44948.7 ops/second
Load time:    11.8816 seconds
Verify time:  3.94841 seconds
Get rate:     2.53267e+06 ops/second
File size:    324884890 bytes
```

Some observations:
- Put rate is consistent with different number of pairs.
- When the data size is big, e.g. with 10 million entries, load time becomes
a big bottleneck. When the database is that big, each cli takes 12 seconds.
Not acceptable.
- File size is something we need to look at as well. 324 MB for 10 million entries
might be too much.
- The `get` rate does not tell the whole story because we just get from the
`unordered_map`. Anyway, the load time is what we should focus on.

## New Strategy

The currently strategy is: whenever we open the db file, we read it from
beginning to end, and load all data into an `unordered_map`.
This map functions like a cache, and it helps reading the data very fast
(after loading is done).

The tentative new strategy is:
- On each cli call, do not load the data to a `map` at all.
Just remember the given db file. Maybe check if it exists, if not,
create a new file.
- For each `put`, just append the next entry to the db file.
- For each `delete`, same thing, just append.
- The file size now will be even worse than previously. Let's assume
compaction will help us.
- For each `get`, now the operation is completely new.
Maybe we read the db file line by line and find the given key.
Maybe going from end of the file to the beginning is a small
optimization. Anyway, `get` will be the main focus for our next step.


One question though: how does a normal storage engine interact with users?
Do we need to optimize for the use case of separate cli requests?
Or should the database always run as a background service?