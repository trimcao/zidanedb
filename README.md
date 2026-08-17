## How to build
```
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON   # configure
cmake --build build   # compile and link
```

To build fresh
```
cmake --build build --target clean  # clean compiled outputs
cmake --build build --clean-first   # or clean and immeditely rebuild
```