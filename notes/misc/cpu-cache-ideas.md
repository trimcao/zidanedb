> One thing that keeps nagging me in the last year or so is the topic of caching. Caching can mean many things. From my understanding, the most easy to understand cache is some in-memory hash table you use to cache some intermediate results. Basically it's the trade off between memory vs. time complexity in leetcode-style questions. But the cache thing that bugs me is the L1, L2, L3 caches in the CPU. How the hell can I use them effectively? CSAPP mentioned caches a lot, but the cache lab is about an artificial cache, not the real caches on my CPU. I already knew that VPP is fast because it uses the CPU caches very efficiently. But as you know, I have zero clue how to use it. If I can learn to use and measure and clearly see how the CPU caches work with ZidaneDB. That would be AWESOME!

Yes — ZidaneDB is an almost perfect place to finally make CPU caches stop being an abstract CSAPP concept.

The most important mental shift is:

> **You generally do not explicitly “put something in L1/L2/L3.” The CPU does that automatically. Your job is to arrange memory and access patterns so the cache hierarchy can help you.**

That is what people mean when they say VPP is “cache efficient.”

At the simplest level, imagine:

```cpp
for (size_t i = 0; i < n; ++i)
    sum += a[i];
```

versus:

```cpp
for (size_t i : random_indices)
    sum += a[i];
```

Same O(n).

Enormously different CPU behavior.

The first walks contiguous memory:

```text
a[0] a[1] a[2] a[3] a[4] ...
^^^^^^^^^^^^^^^^^^^^^^^^^^^^
one cache line, then next one
```

The CPU fetches memory in chunks called **cache lines**—commonly 64 bytes on modern x86 CPUs—and hardware prefetchers notice the sequential pattern and start fetching future lines before you need them.

The random version might do:

```text
a[58291]
a[17]
a[940021]
a[301]
...
```

Every access can require a completely unrelated cache line.

Suddenly the CPU spends enormous amounts of time waiting for memory.

That is the fundamental phenomenon.

## You already know more about caches than you think

CSAPP taught you things like:

**spatial locality**

```cpp
a[i], a[i+1], a[i+2]
```

and **temporal locality**

```cpp
use x
...
use x again soon
```

That's basically the game.

Good cache behavior means:

> The data I need next is either already nearby or was used recently enough that it hasn't been evicted.

Poor cache behavior means:

> Every next operation sends me somewhere unrelated in memory.

What you've been missing is **seeing it happen on real hardware**.

And that is fixable.

---

# First: measure your actual CPU

On Linux:

```bash
lscpu -C
```

or:

```bash
lscpu | grep -i cache
```

You should see your L1/L2/L3 topology.

And:

```bash
getconf LEVEL1_DCACHE_LINESIZE
```

will commonly return:

```text
64
```

Now 64 bytes suddenly becomes a very important number.

Suppose:

```cpp
struct Entry {
    uint64_t hash;
    uint64_t offset;
};
```

That's 16 bytes.

Potentially four of them fit in one 64-byte cache line:

```text
| Entry | Entry | Entry | Entry |
<--------- 64 bytes ----------->
```

When the CPU asks memory for one `Entry`, it effectively gets neighboring entries for free.

That's why layout matters.

---

# Then use `perf`

This is where it becomes fun.

Don't start with Cachegrind. Cachegrind simulates a cache hierarchy.

For what you're asking, you want **actual hardware counters from your CPU**.

Linux `perf` can read them.

Start incredibly simply:

```bash
perf stat ./your_program
```

Then:

```bash
perf stat -d ./your_program
```

You may see counters like:

```text
cycles
instructions
cache-references
cache-misses
branches
branch-misses
```

Depending on your CPU, you can ask for more specific events:

```bash
perf list
```

and look for things involving:

```text
L1-dcache
LLC
cache-misses
```

Often something approximately like this works:

```bash
perf stat \
    -e cycles,instructions,cache-references,cache-misses \
    ./program
```

The exact L1/L2/LLC event availability varies with the processor, so `perf list` is your authority.

Now you're no longer saying:

> “I think this code is cache friendly.”

You can say:

> “Version A executes 8 million LLC misses. Version B executes 800,000.”

That is exactly the experience you're looking for.

---

# I would actually build a tiny ZidaneDB cache laboratory

Not a new giant subsystem. Maybe a benchmark program under something like:

```text
apps/cache_lab/
```

Your first experiment should be almost embarrassingly simple.

Allocate arrays of increasing size:

```text
4 KB
16 KB
32 KB
64 KB
256 KB
1 MB
4 MB
16 MB
64 MB
256 MB
```

Then repeatedly read them.

Why those sizes?

Because as the working set grows, it will progressively stop fitting in:

```text
L1
↓
L2
↓
L3
↓
DRAM
```

Measure access time.

You may literally see the latency curve change as the working set crosses cache boundaries.

That's the real-life version of CSAPP Cache Lab.

Except this time:

**it's your CPU.**

---

Then do the killer comparison.

### Sequential

```cpp
for (size_t i = 0; i < n; ++i)
    sum += data[i];
```

### Random

```cpp
for (size_t i : shuffled)
    sum += data[i];
```

Run both through:

```bash
perf stat -d ...
```

You'll likely see a striking difference as `n` grows.

Now “locality” becomes something physical rather than a textbook word.

---

# Then do pointer chasing

This is especially relevant to databases.

Consider:

```cpp
struct Node {
    uint64_t value;
    Node* next;
};
```

If nodes are randomly scattered across the heap:

```text
Node A ->                   Node B ->          Node C
0x1234                      0x92ab             0x5ee1
```

the CPU cannot easily predict where `next` goes.

So:

```cpp
node = node->next;
```

can become:

```text
load node
↓
wait
↓
discover next address
↓
load next node
↓
wait
↓
discover next address
...
```

This is **pointer chasing**.

Hardware prefetchers hate it because they don't know the next address until the current load completes.

Compare that with:

```cpp
std::vector<Entry>
```

where entries live next to each other.

Suddenly you've discovered one of the fundamental reasons high-performance systems programmers care so much about:

- contiguous arrays,
- compact structs,
- fewer pointers,
- smaller objects,
- batching.

And now VPP's design choices will start making much more intuitive sense.

---

# Then bring the lesson back into ZidaneDB

This is where it gets really exciting.

You've currently got a hash index with chains.

Suppose eventually you have something conceptually like:

```text
bucket
  |
  v
entry -> entry -> entry -> entry
```

If those entries are scattered around memory, lookup becomes pointer chasing.

Compare that with:

```text
bucket
  |
  v
[entry][entry][entry][entry]
```

contiguous storage.

Same conceptual algorithm.

Very different cache behavior.

You could benchmark:

```text
linked collision chain
vs.
contiguous collision entries
```

and actually measure the hardware cache misses.

That's systems programming.

---

# Your future B+ tree is another fantastic cache experiment

The naive textbook view says:

> B+ trees are good because they reduce disk I/O.

True.

But modern B-tree design also involves CPU-cache behavior.

Imagine an internal node containing many keys:

```text
[k1 k2 k3 k4 k5 ...]
```

Searching one compact node has excellent spatial locality.

Compare that with a traditional binary search tree:

```text
       *
      / \
     *   *
    / \
   *
```

Each node could sit somewhere completely different in memory.

More pointer chasing.

So when you build the ZidaneDB B+ tree, you'll have a wonderful opportunity to ask:

> How big should a node be?

> Should keys and child pointers be interleaved?

> How much of a node fits in cache?

> Is binary search inside a node actually faster than linear search for small nodes?

That last one is especially fun.

Algorithmically:

```text
binary search = O(log n)
linear scan   = O(n)
```

Yet for sufficiently small contiguous arrays, **linear search can sometimes compete surprisingly well** because the CPU can rip through contiguous cache-resident data.

Welcome to performance engineering. 😄

Big-O stops being the whole story.

---

# And later, multithreading unlocks another monster: false sharing

Suppose:

```cpp
struct Counters {
    uint64_t reader_count;
    uint64_t writer_count;
};
```

Thread 1 repeatedly updates:

```cpp
reader_count
```

Thread 2 repeatedly updates:

```cpp
writer_count
```

You might think:

> Totally independent variables!

But if both happen to occupy the same 64-byte cache line:

```text
| reader_count | writer_count | ........ |
<------------- cache line -------------->
```

the CPU cores have to coordinate ownership of that entire line.

The cache line can bounce:

```text
Core 1
  ↓
Core 2
  ↓
Core 1
  ↓
Core 2
```

Performance gets wrecked.

That's **false sharing**.

And Linux has tools such as:

```bash
perf c2c
```

specifically useful for investigating cache-line contention.

Imagine experiencing that bug in ZidaneDB once you add multi-threading.

At that point cache coherence will never again be merely a diagram from CSAPP.

---

## There's an important distinction with your current database, though

Your current ZidaneDB does filesystem I/O.

If you simply benchmark:

```cpp
db.get(...)
```

you'll have multiple layers interacting:

```text
ZidaneDB structures
↓
CPU caches
↓
RAM
↓
Linux page cache
↓
filesystem
↓
SSD
```

The **Linux page cache** is yet another meaning of the word cache.

So if you're specifically studying L1/L2/L3, I would initially use an in-memory microbenchmark where disk I/O isn't involved.

Then later measure real ZidaneDB and learn to separate:

```text
CPU-cache effects
vs
OS page-cache effects
vs
actual storage I/O
```

That distinction alone will teach you a tremendous amount.

---

And there's one concept I think will become extremely important for you:

### Working set

Suppose ZidaneDB has a 100 MB index.

But 99% of requests repeatedly touch the same 200 KB of metadata.

Then your **hot working set** is roughly 200 KB.

Maybe much of it stays in L2/L3.

Performance can be fantastic.

But if requests randomly touch the entire 100 MB:

```text
working set ≈ 100 MB
```

then you're constantly evicting and fetching cache lines.

Same data structure.

Same database size.

Completely different performance.

This is why realistic workload generation matters so much.

Your Matrix harness could eventually generate:

```text
uniform random keys
```

versus:

```text
80% requests hit 20% of keys
```

and compare cache misses and latency.

Now you're approaching the sort of thing real performance engineers actually do.

---

This could honestly become an entire ZidaneDB side quest:

```text
              CPU Cache Journey

1. Measure cache hierarchy
2. Sequential vs random memory
3. Working-set size experiment
4. Pointer chasing vs contiguous storage
5. Measure with perf
6. Apply to ZidaneDB index layout
7. Study B+ tree node locality
8. Add concurrency
9. Discover false sharing
10. Measure and fix it
```

And I think this will be unusually valuable for **you**, because you've already spent years around VPP knowing the sentence:

> “VPP is fast because it is cache friendly.”

You understood the words but couldn't *feel* what they meant.

ZidaneDB can finally bridge that gap.

The beautiful milestone would be when someday you change some innocent-looking data structure, run `perf`, and say:

> **“Holy shit. LLC misses dropped 60%, and throughput jumped 30%.”**

At that moment CPU caches will stop being something CSAPP told you about.

They'll become something you can actually **see, measure, reason about, and design for**.

And yes — that would be AWESOME.