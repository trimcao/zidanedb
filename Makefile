BUILD_DIR := build

.PHONY: all configure build test clean superclean

all: build

configure:
	cmake -S . -B $(BUILD_DIR) -DBUILD_TESTING=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

configure-release:
	cmake -S . -B build-release \
		-DCMAKE_BUILD_TYPE=Release \
		-DBUILD_TESTING=OFF

build: configure
	cmake --build $(BUILD_DIR)

build-matrix: configure-release
	cmake --build build-release --target matrix

test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure

clean:
	cmake --build $(BUILD_DIR) --target clean

superclean:
	cmake -E rm -rf $(BUILD_DIR)
