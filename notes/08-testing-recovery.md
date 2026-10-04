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

## Appendix: Making clangd Understand CMake Compile Definitions

CMake can define the crash worker's complete path for the `matrix` target:

```cmake
target_compile_definitions(matrix
    PRIVATE
        ZIDANEDB_CRASH_WORKER_PATH="$<TARGET_FILE:zidane_crash_worker>"
)
```

The real compiler then receives a definition similar to:

```text
-DZIDANEDB_CRASH_WORKER_PATH=\"/path/to/zidanedb/build/zidane_crash_worker\"
```

This is why the following C++ code can compile even when VS Code marks the name as unknown:

```cpp
const std::filesystem::path child_bin{ZIDANEDB_CRASH_WORKER_PATH};
```

`ZIDANEDB_CRASH_WORKER_PATH` is a compiler definition created by CMake, not a C++ variable
declared in the source. The editor's language server must read the same compile command as the
real compiler to understand it.

ZidaneDB uses the VS Code clangd extension. Its compilation database is generated at
`build/compile_commands.json`, but clangd does not normally search a sibling `build/` directory
when analyzing files under `apps/`. Configure it by creating `.vscode/settings.json`:

```json
{
    "clangd.arguments": [
        "--compile-commands-dir=${workspaceFolder}/build"
    ]
}
```

Then run **clangd: Restart language server** from the VS Code command palette. If the diagnostic
remains, run **Developer: Reload Window**.

Configure CMake with compilation database generation enabled:

```bash
cmake -S . -B build \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DBUILD_TESTING=ON
```

Reconfigure CMake after changing target definitions so that `compile_commands.json` stays current.
Because the crash-test source is only included when `BUILD_TESTING` is enabled, the editor must use
a build configured with `BUILD_TESTING=ON`.

An alternative is to place a symbolic link in the repository root:

```bash
ln -s build/compile_commands.json compile_commands.json
```

Clangd searches the project root automatically. The explicit `--compile-commands-dir` setting is
clearer, however, because it states which build directory the editor should use.

Do not add a fake fallback definition directly to the C++ source:

```cpp
#define ZIDANEDB_CRASH_WORKER_PATH ...
```

That would hide the editor configuration problem and could make the editor and the real build use
different worker paths.
