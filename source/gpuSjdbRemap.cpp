#include "gpuSjdbRemap.h"
// Makefile / default CPU builds need no CUDA headers or linker dependencies.
GpuSjdbRemapResult gpuSjdbRemap(const GpuSjdbRemapRequest& request) {
    return {GpuSjdbRemapResult::Unavailable,
            validGpuSjdbRemap(request) ? "CUDA backend not compiled" : "unsupported request"};
}
