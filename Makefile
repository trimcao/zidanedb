BUILD_DIR := build
RELEASE_BUILD_DIR := build-release

.PHONY: all configure build test clean superclean format format-check

all: build

configure:
	cmake -S . -B $(BUILD_DIR) -DBUILD_TESTING=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

configure-release:
	cmake -S . -B $(RELEASE_BUILD_DIR) \
		-DCMAKE_BUILD_TYPE=Release \
		-DBUILD_TESTING=OFF

configure-offline:
	cmake -S . -B $(BUILD_DIR) \
		-G Ninja \
		-DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
		-DFETCHCONTENT_FULLY_DISCONNECTED=ON

build: configure
	cmake --build $(BUILD_DIR)

build-offline: configure-offline
	cmake --build $(BUILD_DIR)

build-matrix: configure-release
	cmake --build build-release --target matrix

test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure

clean:
	cmake --build $(BUILD_DIR) --target clean

superclean:
	cmake -E rm -rf $(BUILD_DIR)
	cmake -E rm -rf $(RELEASE_BUILD_DIR)


format:
	git ls-files -z -- '*.cpp' '*.h' '*.hpp' | \
		xargs -0 clang-format -i

format-check:
	git ls-files -z -- '*.cpp' '*.h' '*.hpp' | \
		xargs -0 clang-format --dry-run --Werror