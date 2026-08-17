BUILD_DIR := build

.PHONY: all configure build test clean superclean

all: build

configure:
	cmake -S . -B $(BUILD_DIR) -DBUILD_TESTING=ON

build: configure
	cmake --build $(BUILD_DIR)

test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure

clean:
	cmake --build build --target clean

superclean:
	cmake -E rm -rf build
