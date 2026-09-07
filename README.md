## How to build

Just use `make`:

```
make build
```

CLI11 and Catch2 are checked into `vendor/`, so configuration and compilation
do not require network access, including after deleting the build directory.

To build fresh:

```
make clean          # clean first
make superclean     # or clean everything
make build          # build again
```
