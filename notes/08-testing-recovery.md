# Testing Recovery V1

Recovery V1 has been implemented for ZidaneDB. Now it's time to make sure recovery actually works.

## Plan
We will use Matrix to test recovery of ZidaneDB. Why using Matrix and not just adding unit tests
on ZidaneDB? Because we want to actually crash ZidaneDB process.

The first wave of testing will use `failpoints` or in other words, deterministic crash points.
Later, we will try to do `partial-write injection` and `VM hard reset`.

## Deterministic Crash Points

Basic idea: at the crash point, `zidane` process would raise `SIGSTOP`, then Matrix would call
`SIGKILL` to kill the process.

We will define a `failpoint()` helper function that runs based on an environment variable, such
as `ZIDANEDB_FAILPOINT`.

Crash 1: Before DB write
Crash 2: DB record completely written, index not updated
Crash 3: Index update partially/fully happens, but `index_clean` is false
Crash 4: After `indexed_up_to_offset` changes
Crash 5: During clean shutdown

## Partial-Write Injection

## fsync Experiments

## VM Hard Reset

