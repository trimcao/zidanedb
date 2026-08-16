# First Implementation of my Storage Engine

## Motivation
Implement a very, very basic key-value store. Make sure the database lib code works, the `zidane` cli and `matrix` cli work. Learn to add some unit tests.

## Lessons

### 1. Why does `std::filesystem` not exist on my setup?

`std::filesystem` does exist on this setup. It was added to the C++ standard library in C++17 and is provided by the `<filesystem>` header.

The problem is the **C++ language mode** used when the header is compiled. On this machine, Apple Clang 17 produces this result:

```sh
c++ -fsyntax-only -x c++ include/zidanedb/database.h
```

```text
error: no member named 'filesystem' in namespace 'std'
```

The compiler's default mode is older than C++17. Both of these commands successfully compile the header:

```sh
c++ -std=c++17 -fsyntax-only -x c++ include/zidanedb/database.h
c++ -std=c++20 -fsyntax-only -x c++ include/zidanedb/database.h
```

The project's CMake configuration already requests C++20 correctly:

```cmake
target_compile_features(zidanedb
    PUBLIC
        cxx_std_20
)
```

CMake will add the compiler's standard-selection flag when one is needed. Because `database.h` is part of the library's public interface, `PUBLIC` is appropriate: targets that consume the header also inherit the C++20 requirement.

Therefore, build the project through CMake:

```sh
cmake -S . -B build
cmake --build build
```

If an editor still underlines `std::filesystem`, its code analyzer is probably not using the CMake configuration and may still be parsing as an older version of C++. Configure that editor or language server to use the project's CMake compile settings.

The C++ standards committee adopted the filesystem library into C++17 in [P0218R1](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2016/p0218r1.html). The [CMake target commands guide](https://cmake.org/cmake/help/latest/guide/tutorial/In-Depth%20CMake%20Target%20Commands.html) explains how `target_compile_features()` selects a target's minimum language standard.

### 2. What is `[[nodiscard]]`?

`[[nodiscard]]` is a standard C++ attribute that tells the compiler that ignoring a result is probably a mistake.

For example:

```cpp
[[nodiscard]]
std::optional<std::string> get(const std::string& key) const;
```

This call ignores the returned value:

```cpp
database.get("player");
```

The compiler will normally issue a warning because calling `get()` without examining its result does nothing useful.

This call uses the result:

```cpp
auto player = database.get("player");
```

`[[nodiscard]]` does not change how the function runs, and ignoring the result is not automatically a compilation error. It gives the compiler information that it can use to diagnose a likely bug.

It is especially useful for:

- A lookup result that says whether a value exists.
- A status or error result that the caller should inspect.
- A value whose entire purpose is to be consumed by the caller.

It is less useful on a function returning `void`, since there is no result to discard.

### 3. How does `std::optional` work?

`std::optional<T>` represents either:

- One value of type `T`.
- No value.

Include it with:

```cpp
#include <optional>
```

For this database:

```cpp
std::optional<std::string> get(const std::string& key) const;
```

the two possible states are:

```cpp
return it->second;       // contains a string
return std::nullopt;     // contains no string
```

A possible implementation is:

```cpp
std::optional<std::string> Database::get(const std::string& key) const
{
    auto it = data_.find(key);

    if (it == data_.end()) {
        return std::nullopt;
    }

    return it->second;
}
```

The caller can check it like this:

```cpp
auto player = database.get("player");

if (player.has_value()) {
    std::cout << player.value() << '\n';
} else {
    std::cout << "key not found\n";
}
```

A common shorter form is:

```cpp
if (player) {
    std::cout << *player << '\n';
}
```

The important operations are:

| Expression | Meaning |
|---|---|
| `player.has_value()` | Returns whether a value is present. |
| `if (player)` | Another way to test whether a value is present. |
| `*player` | Accesses the value; only use after confirming it exists. |
| `player.value()` | Accesses the value and throws `std::bad_optional_access` if absent. |
| `player.value_or("unknown")` | Returns the value or the supplied fallback. |
| `std::nullopt` | Represents the no-value state. |

This solves an ambiguity in a plain `std::string` return value. Without `optional`, an empty string could mean either:

- The key was found and its value was `""`.
- The key was not found.

With `optional`, those states are distinct:

```cpp
return std::string{};   // found, and the stored value is empty
return std::nullopt;    // not found
```

Conceptually, an `optional` contains its value plus knowledge of whether that value is present. It is not a pointer, and an `optional<std::string>` returned by value does not refer back to the map entry. The C++ standards committee's original [`optional` proposal](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2013/n3527.html) describes it as a type that can answer whether it contains a `T` and provide that value when present.

### 4. What does `const` at the end of `get()` mean?

Consider:

```cpp
std::optional<std::string> get(const std::string& key) const;
```

The final `const` applies to the `Database` object on which the member function is called. It promises that `get()` will not modify that object's ordinary data members.

Inside a non-static member function, C++ supplies an implicit pointer named `this`. A useful mental model is:

```cpp
// C++ member function
value = database.get(key);

// Rough C-like mental model
value = database_get(&database, key);
```

For a trailing-`const` member function, the implicit object parameter behaves roughly like a pointer to const:

```cpp
database_get(const Database* database, const std::string& key);
```

Therefore, this is not allowed inside `get() const`:

```cpp
data_[key] = "changed";
```

Reading from `data_` is allowed:

```cpp
auto it = data_.find(key);
```

The trailing `const` also allows the function to be called through a const database object:

```cpp
const Database database{"store.zdb"};
auto player = database.get("player");
```

A non-const member function such as `put()` cannot normally be called on that object.

The `const` must appear in both the declaration and definition:

```cpp
// database.h
std::optional<std::string> get(const std::string& key) const;

// database.cpp
std::optional<std::string> Database::get(const std::string& key) const
{
    // ...
}
```

The two occurrences of `const` in this signature have separate jobs:

```cpp
get(const std::string& key) const
    ^                         ^
    |                         |
    do not modify key         do not modify this Database
```

### 5. Is `std::string& key` the C++ equivalent of `std::string* key` in C?

They are related forms of indirection, but they are not the same language construct.

First, `std::string` itself is a C++ standard-library type and does not exist in C. The exact pointer spelling below is therefore also C++, although its pointer behavior resembles C:

```cpp
std::string* key;          // pointer to a string
std::string& key_ref = s;  // reference to a string
```

A C++ reference acts as another name, or alias, for an existing object:

```cpp
void uppercase(std::string& text)
{
    // Modifications to text affect the caller's string.
}

std::string name = "zidane";
uppercase(name);
```

A pointer requires explicit address-taking and dereferencing:

```cpp
void uppercase(std::string* text)
{
    if (text == nullptr) {
        return;
    }

    // Access with *text or text->...
}

std::string name = "zidane";
uppercase(&name);
```

The major differences are:

| Reference: `T&` | Pointer: `T*` |
|---|---|
| Must refer to an existing object when created. | May be `nullptr`. |
| Cannot be reseated to refer to a different object. | Can be assigned another address. |
| Used with ordinary object syntax: `key.size()`. | Requires pointer syntax: `key->size()` or `(*key).size()`. |
| Caller passes `key`. | Caller passes `&key`. |
| Usually expresses a required, non-owning object. | Can express an optional object, array traversal, or other pointer semantics. |

These three parameter declarations also mean different things:

| Declaration | Meaning |
|---|---|
| `std::string key` | Make a new parameter object by copying or moving the argument. |
| `std::string& key` | Borrow the caller's string and allow it to be modified. |
| `const std::string& key` | Borrow the caller's string without copying and do not modify it. |

For `get()`, the database only needs to inspect the key, so this communicates the intent well:

```cpp
get(const std::string& key)
```

Read it from right to left as: **`key` is a reference to a const `std::string`**. This spelling means the same thing:

```cpp
get(std::string const& key)
```

Passing `put()` parameters by value can still be reasonable because the database needs to store them:

```cpp
void put(std::string key, std::string value);
```

The implementation can then move those parameter objects into the map rather than copying them again:

```cpp
data_.insert_or_assign(std::move(key), std::move(value));
```

That requires `<utility>` for `std::move`. The important principle is not to change every parameter mechanically to a reference. Choose based on intent:

- Borrow and read: `const T&`.
- Borrow and modify the caller's object: `T&`.
- Accept no object as a valid possibility: often `T*` or a more specific nullable type.
- Take or create your own value: often `T` by value.

The C++ standards committee paper [N3538](https://www.open-std.org/JTC1/SC22/WG21/docs/papers/2013/n3538.html) summarizes const-reference parameters as combining pass-by-value-like syntax with indirect access that avoids copying.

### 6. How do I fix `std::filesystem` errors in VS Code?

The project itself is configured correctly. CMake's generated flags for `zidanedb` contain:

```text
-std=gnu++20
```

The editor error comes from clangd, the C++ language server installed in VS Code. clangd needs to know the same include paths, language standard, definitions, and other flags that CMake uses to compile each source file.

The standard way to provide that information is a **compilation database** named `compile_commands.json`.

#### Step 1: Generate the compilation database

Configure the existing build directory with:

```sh
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

This creates:

```text
build/compile_commands.json
```

The file records the exact compiler command for every C++ translation unit. In this project, that includes the required `-std=gnu++20` flag. CMake documents the option in [`CMAKE_EXPORT_COMPILE_COMMANDS`](https://cmake.org/cmake/help/latest/variable/CMAKE_EXPORT_COMPILE_COMMANDS.html).

#### Step 2: Tell VS Code's clangd where the file is

Create `.vscode/settings.json` if it does not already exist:

```json
{
    "clangd.arguments": [
        "--compile-commands-dir=${workspaceFolder}/build"
    ]
}
```

This tells clangd to read `build/compile_commands.json` instead of guessing how the project is compiled.

The [official clangd VS Code extension documentation](https://github.com/clangd/vscode-clangd) recommends generating `compile_commands.json` with CMake so clangd receives the project's real compile flags.

#### Step 3: Restart clangd

After saving the setting and configuring CMake:

1. Open the VS Code command palette with `Cmd+Shift+P`.
2. Run **clangd: Restart language server**.
3. If that command is not present, run **Developer: Reload Window**.

The false `std::filesystem` error should then disappear.

#### How to verify the fix

Open VS Code's **Output** panel and select **clangd**. Its log should say that it loaded a compilation database from the project's `build` directory. The inferred compile command should contain a C++20 flag such as:

```text
-std=gnu++20
```

If the real CMake build succeeds while VS Code still shows a red underline, the underline remains an editor-analysis problem rather than a compiler error.

Avoid fixing this by adding only a hard-coded `-std=c++20` fallback flag. A compilation database is better because it also communicates include directories and target-specific options, and it stays synchronized with the CMake project.
