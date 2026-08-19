# The First Problem: Forgetfulness

## Motivation

We have our stupidly simple kv store. It runs, it can put keys, get keys, delete keys. Great. But when `zidane` stops running, he does not remember anything. Databases must remember stuffs. `zidane` should remember his headbutt against `matrix`. Forgetfulness is not good for `zidane`.


## My Thoughts on a Solution

Let's start with a very naive implementation, and go from there.
But the first question is, what is the format of the db file?
Sadly, I am already stumped here. Problems are:
- If keys and values can be multi-line, then it will be complicated.
- If I choose to do: <key>=<value>, then what happens if either key or value has
the character `=`?

I guess, I will implement a very rough draft first. And there will be many problems
that I don't even see right now. But it's ok.
My current assumptions are:
- Keys and values are one-line only. No new-line character allowed.
- The format of the database file will be roughly:
```
<key-0>
<value-0>
<key-1>
<value-1>
...
```
- I mean, new-line character is probably ok as long as we can save all the special
characters into the same line. For example, new-line will be stored as `\n` in the string.
So a string like this `line1\nline2\line3` can be 3 lines long, but on the db file, we
can still process it as one line. That's the idea, but I don't know exactly how
to do it.
- Follow up the point above, is that one of the reasons we want to use binary format?
Maybe not binary (for now), but `base64` definitely sounds promising.
- For now, let's just follow this plan, then I can deal with the problems later.
Most likely I will need to change the file format anyway :).


Next question: when do I open the db file?
- Naive: just open the file whenever I do `put` or `delete`.


One note from Codex:
- A safer persistence design will eventually write to a temporary file first and replace the original only after the complete write succeeds.

## Challenges from Matrix


## What's Next


## Appendix

### Understanding the `TemporaryDatabaseFile` test helper

`TemporaryDatabaseFile` is a small test helper. Its job is to:

1. Choose a path in the operating system's temporary directory.
2. Ensure no old database exists at that path.
3. Let the test use the path.
4. Delete the test database when the test finishes.

This is an example of RAII: the constructor prepares a resource, and the destructor cleans it up.

#### Understanding the constructor

```cpp
explicit TemporaryDatabaseFile(const std::string& filename)
    : path_{
        std::filesystem::temp_directory_path() /
        filename
    }
{
    remove();
}
```

The constructor contains several separate C++ concepts.

##### `explicit`

```cpp
explicit TemporaryDatabaseFile(...)
```

This prevents C++ from automatically converting a string into a `TemporaryDatabaseFile`.

Direct construction is allowed:

```cpp
TemporaryDatabaseFile file{"test.zdb"};
```

But an accidental implicit conversion is prevented:

```cpp
std::string filename{"test.zdb"};

// Not allowed because the constructor is explicit:
TemporaryDatabaseFile file = filename;
```

It makes the creation of this special test object intentional.

##### `const std::string& filename`

```cpp
const std::string& filename
```

- `&` avoids copying the filename.
- `const` promises not to modify the caller's string.

##### The member initializer list

```cpp
: path_{
    std::filesystem::temp_directory_path() /
    filename
}
```

The colon begins the constructor's member initializer list. It constructs `path_` before the constructor body runs.

Suppose the temporary directory is:

```text
/tmp
```

and `filename` is:

```text
zidanedb-test.zdb
```

This expression:

```cpp
std::filesystem::temp_directory_path() / filename
```

produces something like:

```text
/tmp/zidanedb-test.zdb
```

For filesystem paths, `/` is overloaded to mean "join these path components." It is not mathematical division.

Conceptually, the constructor does this:

```text
construct path_ as "/tmp/zidanedb-test.zdb"
run the constructor body
```

##### Why call `remove()` in the constructor?

```cpp
{
    remove();
}
```

A previous test run may have left a database file behind—for example, if the process was interrupted. If the old file remains, the new test might load old data and produce misleading results.

Calling `remove()` ensures every test begins with no database file:

```text
Construct helper
    ↓
Delete old test file, if one exists
    ↓
Test starts with a clean path
```

Calling `remove()` when the file does not exist is harmless in this helper because it uses the non-throwing `std::error_code` overload.

The destructor calls `remove()` again for a different reason:

```cpp
~TemporaryDatabaseFile()
{
    remove();
}
```

This second call cleans up the database created by the current test:

```text
Constructor remove() -> clean before test
Destructor remove()  -> clean after test
```

#### Understanding `path()`

```cpp
const std::filesystem::path& path() const
```

There are two separate uses of `const`.

##### First `const`: the returned path is read-only

```cpp
const std::filesystem::path&
```

This returns a reference to `path_` without copying it, but callers cannot modify it through that reference:

```cpp
const auto& database_path = file.path();
```

This is not allowed:

```cpp
file.path() = "something-else.zdb";
```

That protection matters because the helper must remember the correct path to remove later.

The returned reference remains valid only while the `TemporaryDatabaseFile` object still exists.

##### Last `const`: the function does not modify the helper

```cpp
path() const
       ^^^^^
```

The trailing `const` promises that calling `path()` does not modify the `TemporaryDatabaseFile` object:

```cpp
const TemporaryDatabaseFile file{"test.zdb"};

file.path(); // Allowed because path() is a const member function.
```

Inside `path()`, modifying ordinary members would produce a compiler error:

```cpp
const std::filesystem::path& path() const
{
    // Not allowed in a const member function:
    path_ = "another-file.zdb";

    return path_;
}
```

The complete declaration therefore means:

```cpp
const std::filesystem::path& path() const
//    ^ return a read-only reference
//                                   ^ do not modify this object
```

The implementation is simply:

```cpp
const std::filesystem::path& path() const
{
    return path_;
}
```

In plain English:

> Give the caller access to my path without copying it, but do not let the caller change it, and do not change this helper while doing so.
