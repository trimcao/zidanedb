# Code Style and `clang-format`

For this project, start with `clang-format`. It handles layout and whitespace automatically.
`clang-tidy` and `cppcheck` solve different problems, such as finding possible bugs, unsafe patterns,
and maintainability issues. They are not required just to make the code style consistent.

## Install `clang-format` and `clangd`

The command-line `clang-format` program is used by the Makefile targets. The `clangd` language
server provides formatting, diagnostics, completion, and navigation inside VS Code.

### macOS

Install LLVM with Homebrew:

```sh
brew install llvm
```

Homebrew installs LLVM as keg-only. Add its programs to the shell path so VS Code can find them:

```sh
echo 'export PATH="$(brew --prefix llvm)/bin:$PATH"' >> ~/.zshrc
source ~/.zshrc
```

See the [Homebrew LLVM formula](https://formulae.brew.sh/formula/llvm) and the
[clangd installation guide](https://clangd.llvm.org/installation).

### Fedora Linux

On Fedora, both programs are provided by `clang-tools-extra`:

```sh
sudo dnf install clang-tools-extra
```

### Debian and Ubuntu Linux

On Debian-based distributions:

```sh
sudo apt install clang-format clangd
```

On either macOS or Linux, verify that both programs are available:

```sh
clang-format --version
clangd --version
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
bool Database::erase(const std::string& key) {
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
git ls-files -z -- '*.cpp' '*.h' '*.hpp' ':(exclude)vendor/**' |
    xargs -0 clang-format --dry-run --Werror
```

If a file's formatting differs from `.clang-format`, the command reports errors and exits
unsuccessfully.

## Apply Formatting

```sh
git ls-files -z -- '*.cpp' '*.h' '*.hpp' ':(exclude)vendor/**' |
    xargs -0 clang-format -i
```

Then inspect the changes:

```sh
git diff
```

The first formatting pass should ideally be made in its own commit. That prevents a large
formatting diff from being mixed with changes to database behavior.

Using `git ls-files` avoids build outputs and untracked files. The explicit exclusion prevents
clang-format from rewriting the checked-in third-party sources under `vendor/`.

## Add Makefile Commands

Add these targets to the `Makefile`:

```make
.PHONY: format format-check

format:
	git ls-files -z -- '*.cpp' '*.h' '*.hpp' ':(exclude)vendor/**' | \
		xargs -0 clang-format -i

format-check:
	git ls-files -z -- '*.cpp' '*.h' '*.hpp' ':(exclude)vendor/**' | \
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

## VS Code Format on Save

The same VS Code setup works on macOS and Linux. Install the official clangd extension from the
Extensions panel, or from a terminal:

```sh
code --install-extension llvm-vs-code-extensions.vscode-clangd
```

If Microsoft's C/C++ extension is installed, disable it for this workspace to avoid running two C++
language servers at the same time.

Open the Command Palette with `Cmd+Shift+P` on macOS or `Ctrl+Shift+P` on Linux. Select
**Preferences: Open User Settings (JSON)** and add:

```json
"[cpp]": {
    "editor.defaultFormatter": "llvm-vs-code-extensions.vscode-clangd",
    "editor.formatOnSave": true
},
"[c]": {
    "editor.defaultFormatter": "llvm-vs-code-extensions.vscode-clangd",
    "editor.formatOnSave": true
}
```

Open a C++ file and save it to test the setup. If VS Code asks which formatter to use, run
**Format Document With...**, select **Configure Default Formatter**, and choose **clangd**.

clangd uses the repository's `.clang-format` file. See the
[clangd formatting documentation](https://clangd.llvm.org/features#formatting) and the
[VS Code format-on-save
documentation](https://code.visualstudio.com/docs/editing/codebasics#_formatting).

For accurate diagnostics and code completion, generate the CMake compilation database:

```sh
make configure
```

This creates `build/compile_commands.json`. clangd searches the project's `build/` directory for
this file automatically. The compilation database is not required for formatting alone.

## Formatting Versus Static Analysis

The tools have different responsibilities:

- `clang-format` automatically applies consistent whitespace and layout.
- Compiler warnings report problems found while compiling the program.
- `clang-tidy` checks for possible bugs, modernization opportunities, and maintainability issues.
- `cppcheck` is another static-analysis tool that can find some problems independently of the
  compiler.

Establish formatting first. Compiler warnings and semantic linting can then be added separately, so
it remains clear which tool is responsible for each kind of feedback.
