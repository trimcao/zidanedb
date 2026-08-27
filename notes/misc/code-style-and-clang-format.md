# Code Style and `clang-format`

For this project, start with `clang-format`. It handles layout and whitespace automatically.
`clang-tidy` and `cppcheck` solve different problems, such as finding possible bugs, unsafe patterns,
and maintainability issues. They are not required just to make the code style consistent.

## Install `clang-format`

On macOS with Homebrew:

```sh
brew install clang-format
```

This is the installation command from the
[Homebrew `clang-format` formula](https://formulae.brew.sh/formula/clang-format).

Verify the installation:

```sh
clang-format --version
```

## Add `.clang-format`

Create a `.clang-format` file at the repository root:

```yaml
BasedOnStyle: LLVM
Language: Cpp

IndentWidth: 4
ContinuationIndentWidth: 4
UseTab: Never
ColumnLimit: 100

BreakBeforeBraces: Attach

AllowShortIfStatementsOnASingleLine: Never
AllowShortLoopsOnASingleLine: false

DerivePointerAlignment: false
PointerAlignment: Left
ReferenceAlignment: Left

SortIncludes: CaseSensitive
```

This is a reasonable starting point for the style generally used in ZidaneDB:

```cpp
bool Database::erase(const std::string& key)
{
    if (!data_.contains(key)) {
        return false;
    }

    return true;
}
```

There is no single industry-standard C++ style. The important part is selecting a reasonable
baseline and applying it consistently. Clang-format supports baselines such as LLVM, Google,
Mozilla, WebKit, Microsoft, and GNU. A `.clang-format` file can override the details that matter to
the project. See the
[LLVM style-options documentation](https://clang.llvm.org/docs/ClangFormatStyleOptions.html).

## Check Formatting Without Changing Files

This checks all tracked C++ files without modifying them:

```sh
git ls-files -z -- '*.cpp' '*.h' '*.hpp' |
    xargs -0 clang-format --dry-run --Werror
```

If a file's formatting differs from `.clang-format`, the command reports errors and exits
unsuccessfully.

## Apply Formatting

```sh
git ls-files -z -- '*.cpp' '*.h' '*.hpp' |
    xargs -0 clang-format -i
```

Then inspect the changes:

```sh
git diff
```

The first formatting pass should ideally be made in its own commit. That prevents a large
formatting diff from being mixed with changes to database behavior.

Using `git ls-files` also avoids accidentally formatting downloaded dependencies under
`build/_deps`.

## Add Makefile Commands

Add these targets to the `Makefile`:

```make
.PHONY: format format-check

format:
	git ls-files -z -- '*.cpp' '*.h' '*.hpp' | \
		xargs -0 clang-format -i

format-check:
	git ls-files -z -- '*.cpp' '*.h' '*.hpp' | \
		xargs -0 clang-format --dry-run --Werror
```

The normal workflow can then be:

```sh
make format
make build
make test
```

To check formatting without changing anything:

```sh
make format-check
```

## Optional VS Code Setup

When using Microsoft's C/C++ extension, select it as the C++ formatter and enable formatting when
saving:

```json
"[cpp]": {
    "editor.defaultFormatter": "ms-vscode.cpptools",
    "editor.formatOnSave": true
}
```

VS Code will use the `.clang-format` file. Clang-format searches for this file by walking upward
from the source file toward the repository root. See the
[LLVM clang-format documentation](https://clang.llvm.org/docs/ClangFormat.html).

## Formatting Versus Static Analysis

The tools have different responsibilities:

- `clang-format` automatically applies consistent whitespace and layout.
- Compiler warnings report problems found while compiling the program.
- `clang-tidy` checks for possible bugs, modernization opportunities, and maintainability issues.
- `cppcheck` is another static-analysis tool that can find some problems independently of the
  compiler.

Establish formatting first. Compiler warnings and semantic linting can then be added separately, so
it remains clear which tool is responsible for each kind of feedback.
