# Understanding CMake, Make, and Ninja

ZidaneDB currently has three build-related layers, plus two different kinds of `Makefile`:

```text
C++ source files + CMakeLists.txt
                |
                | cmake -S . -B build
                v
       CMake chooses a generator
                |
        +-------+--------+
        |                |
        v                v
 build/Makefile     build/build.ninja
        |                |
      make             ninja
        +-------+--------+
                |
                v
      compiler and linker
                |
                v
       zidane, matrix, tests
```

## What Ninja Is

Ninja is a build executor, similar to Make.

Both tools answer questions such as:

- Which `.cpp` files changed?
- Which object files need recompilation?
- In what order should targets be built?
- Which operations can run in parallel?
- Does the executable need to be relinked?

Ninja is deliberately small and optimized for fast incremental builds. It is normally given
generated `build.ninja` files by a higher-level system such as CMake, rather than having people
write them manually. See the [official Ninja overview](https://ninja-build.org/).

Ninja is not:

- A compiler.
- A package manager.
- A replacement for `CMakeLists.txt`.
- Required for CMake.
- Particularly important for a project as small as ZidaneDB.

For ZidaneDB, ordinary Make is completely reasonable.

## What CMake Does

CMake reads `CMakeLists.txt` and generates instructions for another build tool.

This command:

```sh
cmake -S . -B build
```

means:

- `-S .`: the source directory is the current directory.
- `-B build`: place the generated build files in `build/`.

On this machine, CMake currently chooses:

```text
Unix Makefiles
```

This choice is stored in `build/CMakeCache.txt`:

```text
CMAKE_GENERATOR:INTERNAL=Unix Makefiles
```

CMake consequently generates:

```text
build/Makefile
```

Then:

```sh
cmake --build build
```

asks CMake to invoke Make using that generated Makefile.

## The Two Makefiles

The repository has a hand-written Makefile:

```text
zidanedb/Makefile
```

It provides convenient commands:

```sh
make build
make test
make clean
make superclean
```

CMake generates another Makefile:

```text
zidanedb/build/Makefile
```

The generated file should not be edited manually.

When this is run from the repository root:

```sh
make build
```

the root Makefile approximately performs:

```sh
cmake -S . -B build
cmake --build build
```

Then `cmake --build` invokes the generated build system:

```text
root Makefile
    |
    +-- runs CMake
    |
    +-- CMake invokes build/Makefile
```

## Why Switching to Ninja Fails

The normal configuration uses the default generator:

```make
cmake -S . -B build
```

That produces Unix Makefiles on this setup.

The offline configuration currently says:

```make
cmake -S . -B build -G Ninja
```

That asks CMake to place Ninja files in the same `build/` directory. CMake does not allow one build
directory to switch generators. Exactly one generator belongs to each build tree. See the
[CMake generator documentation](https://cmake.org/cmake/help/latest/manual/cmake-generators.7.html).

That causes this error:

```text
generator Ninja
does not match the generator used previously: Unix Makefiles
```

Ninja is also not currently installed on this machine. Even with an empty build directory,
`-G Ninja` would not work until Ninja was installed.

## Recommendation for ZidaneDB

For now, consistently use Unix Makefiles. Ninja will not provide a meaningful benefit for a project
of this size.

Change the offline configuration conceptually from:

```make
configure-offline:
	cmake -S . -B $(BUILD_DIR) \
		-G Ninja \
		-DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
		-DFETCHCONTENT_FULLY_DISCONNECTED=ON
```

to:

```make
configure-offline:
	cmake -S . -B $(BUILD_DIR) \
		-DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
		-DFETCHCONTENT_FULLY_DISCONNECTED=ON
```

The important change is removing:

```make
-G Ninja
```

Both normal and offline configurations then use the same generator and build directory.

Offline mode only works after dependencies have already been downloaded into `build/_deps`. Running
`make superclean` deletes those downloaded dependencies, so a new offline configuration may not
work afterward.

## Normal Development Workflow

Most of the time, simply use:

```sh
make build
```

There is no need to clean first. Build systems are designed to perform incremental builds:

```text
Edit database.cpp
        |
        v
make build
        |
        +-- recompile database.cpp
        +-- skip unchanged source files
        +-- relink affected executables
```

Run tests with:

```sh
make test
```

The `test` target already builds before running the tests. Therefore, the everyday development cycle
can be:

```sh
# Edit code.
make test

# Edit code again.
make test
```

Use `make build` when compilation is needed without running tests.

## When to Clean

### Ordinary Incremental Rebuild

Use this almost always:

```sh
make build
```

### Recompile Everything While Keeping the CMake Configuration

```sh
make clean
make build
```

`make clean` removes compiled outputs but preserves:

- CMake's cache.
- The selected generator.
- Downloaded dependencies.
- The overall build-directory configuration.

### Completely Recreate the Build Directory

```sh
make superclean
make build
```

This is useful when:

- The CMake cache is badly confused.
- The generator is deliberately being changed.
- Build files appear irreparably stale.
- CMake reports a generator mismatch.

However, `superclean` also removes downloaded FetchContent dependencies, so the next build may need
internet access.

## Using Ninja Later

If Ninja is eventually desired, use a separate build directory:

```sh
cmake -S . -B build-ninja -G Ninja
cmake --build build-ninja
```

Keep a Make-based build separate:

```sh
cmake -S . -B build-make -G "Unix Makefiles"
cmake --build build-make
```

The build trees will not conflict:

```text
build-make/
    Makefile
    CMakeCache.txt

build-ninja/
    build.ninja
    CMakeCache.txt
```

CMake's official tutorial explains that changing generators requires deleting the old build tree or
using another directory. See
[CMake: Before You Begin](https://cmake.org/cmake/help/latest/guide/tutorial/Before%20You%20Begin.html).

For ZidaneDB right now, the simplest predictable workflow is one `build/` directory, one Unix
Makefiles generator, and these commands:

```sh
make build
make test
```
