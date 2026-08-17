BUILD_DIR := build

.PHONY: all configure build test clean superclean

all: build

configure:
	cmake -S . -B $(BUILD_DIR) -DBUILD_TESTING=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

build: configure
	cmake --build $(BUILD_DIR)

test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure

clean:
	cmake --build $(BUILD_DIR) --target clean

superclean:
	cmake -E rm -rf $(BUILD_DIR)
