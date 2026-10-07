# Compatibility entry points. CMake owns source lists, dependencies and flags.
# Run from source/: make STAR, make STARlong, or make gdb.
CMAKE ?= cmake
BUILD_DIR ?= ../build/make
BUILD_TYPE ?= Release
CXX_STANDARD ?= 17
CMAKE_ARGS ?=
JOBS ?= 4
LONG_READS ?= OFF
POSIX_SHARED_MEM ?= OFF
OUTPUT ?= STAR

.PHONY: all STAR STARlong gdb gdb-long STARforMac POSIXSHARED clean CLEAN help
all: STAR

STAR STARlong gdb gdb-long STARforMac POSIXSHARED:
	$(CMAKE) -S . -B "$(BUILD_DIR)" -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DSTAR_CXX_STANDARD=$(CXX_STANDARD) -DSTAR_LONG_READS=$(LONG_READS) -DSTAR_POSIX_SHARED_MEM=$(POSIX_SHARED_MEM) $(CMAKE_ARGS)
	$(CMAKE) --build "$(BUILD_DIR)" --config $(BUILD_TYPE) --target STAR --parallel $(JOBS)
	$(CMAKE) -E copy "$(BUILD_DIR)/STAR" "$(OUTPUT)"

STARlong gdb-long: LONG_READS=ON
STARlong gdb-long: OUTPUT=STARlong
gdb gdb-long: BUILD_TYPE=Debug
POSIXSHARED: POSIX_SHARED_MEM=ON

# Full static linking was never qualified for the CMake dependency graph.
.PHONY: STARstatic STARlongStatic STARforMacStatic STARlongForMacStatic
STARstatic STARlongStatic STARforMacStatic STARlongForMacStatic:
	@echo "Full static builds require a separately qualified toolchain; use CMake and docs/BUILD.md."
	@exit 2

clean CLEAN:
	$(CMAKE) --build "$(BUILD_DIR)" --target clean

help:
	@echo "make STAR [JOBS=4] [CXX_STANDARD=17] [CMAKE_ARGS='-DSTAR_USE_AVX2=ON']"
	@echo "make STARlong; make gdb; canonical entry: cmake -S source -B build"
