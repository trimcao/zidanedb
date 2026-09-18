# Vendored dependencies

ZidaneDB keeps its build dependencies in this directory so a fresh build can
be configured and compiled without network access.

| Dependency | Version | Commit |
| --- | --- | --- |
| [CLI11](https://github.com/CLIUtils/CLI11) | 2.6.2 | `37bb6edc5317e99af72ef48405e65d9ca5218861` |
| [Catch2](https://github.com/catchorg/Catch2) | 3.15.0 | `6ee0826dcae55ed1e06b2c5701981221e979e1e6` |
| [crc32c](https://github.com/google/crc32c/) | 1.1.2 | `` |

The source trees are archives of the corresponding upstream Git revisions;
they do not contain nested `.git` directories. Their upstream license files
are included in their respective directories.

The normal `make build` and `make test` commands use these copies directly.
No separate dependency download or offline option is required.
