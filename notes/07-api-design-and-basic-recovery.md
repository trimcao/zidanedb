# API Design and Recovery

## API Design (Basic)
The basic question: what do PUT, GET, and DELETE actually mean?

PUT scenarios:
- PUT with empty value
- PUT overwrite

GET scenarios:
- GET a key with an empty value
- GET a non-existent key

DELETE scenarios:
- DELETE a non-existent key

What I want right now:
- PUT with empty value should be allowed.
- DELETE a non-existent key should notify the users, maybe an error is ok.

Current problems:
- Currently, the db file will use empty value as an indicator of a deleted
key. What should I do?
- If I want to do PUT with empty value, how to handle empty value
in the `zidane` command line?

Solutions:
- Having a type of db entry will help us differentiate between a deleted
key and an existing key with an empty value.
- CLI11 should handle empty value just fine. Just need to test it
thoroughly.

## Recovery

### Basic recovery path (summary)

```
Can I read the fixed-size header?
        ↓ no
incomplete tail

Are lengths sane and within remaining file size?
        ↓ no
corrupt/incomplete record

Can I read the entire payload + checksum?
        ↓ no
incomplete tail

Does checksum match?
        ↓ no
corrupted/torn record

Yes
        ↓
accept record
```

```
record checksum
    ↓
log scanner
    ↓
recover incomplete tail
    ↓
rebuild index
    ↓
deterministic crash points
    ↓
partial-write injection
    ↓
fsync experiments
    ↓
VM hard-reset experiments
```

### Source of truth
The `.zdb` data log is authoritative and the `.idx` file is derived state.
If the index disagrees with the log after a crash, recovery trusts the log.

### Every data record is self-describing
A valid record should contain the following information:
- Operation type (PUT/DELETE)
- Key length
- Key bytes
- Value length
- Value bytes
- Checksum

Goal: given an offset, can ZidaneDB tell whether there is one complete valid record there.

### Write a sequential log scanner
Write something like `scan_records()` that starts at byte 0 and walks the `.zdb` file.
It should return every valid record until EOF.

### Scanner should tolerate an incomplete tail
If the DB file has an incomplete tail, do not treat that as random corruption,
ignore or truncate the incomplete tail.

### Rebuild the index entirely from the data log
Can reconstruct the hash index from the data log.
Question: Do I need to replay all of the `PUT` and `DELETE` to reconstruct?

### Idea of checkpoint
We don't need to rebuild the persistent index file from scratch.
The basic idea is
```
existing persistent index
        +
replay tail of data log
        =
current index
```

Add `last_applied_log_offset` to the index file header
```
magic
version
bucket_count
last_applied_log_offset
```

If `last_applied_log_offset == 0 (or missing)`, of course we will build
the index file from scratch.

This idea works well because a big concern for us right now is an
incomplete tail. So mostly we just need to check the last portion
of the db file.

How about the index file is corrupted itself, not the db file?
The simple approach is, after an unclean crash, rebuild the index file
from the scratch.

Add another marker in the index file header to indicate if the index file
shuts down cleanly or not.
```
magic
version
bucket_count
last_applied_log_offset
clean_shutdown
```

### Use checksums and distinguish truncation from corruption


### Build crash injection into Matrix


### Hardware crash simulated with VMs (virtual machines)

