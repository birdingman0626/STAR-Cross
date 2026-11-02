#ifndef STAR_GPU_SYNC_MODE_H
#define STAR_GPU_SYNC_MODE_H
// How host threads wait for the device, set before the CUDA context exists.
// Default: blocking (waiting threads sleep and leave the cores to the mapping workers).
// STAR_GPU_SYNC=spin|yield|auto selects the other CUDA schedules for measurements.
#include <cuda_runtime.h>
#include <cstdlib>
#include <cstring>

inline unsigned gpuSyncScheduleFlag() {
    const char* v=std::getenv("STAR_GPU_SYNC");
    if (v && !std::strcmp(v,"spin"))  return cudaDeviceScheduleSpin;
    if (v && !std::strcmp(v,"yield")) return cudaDeviceScheduleYield;
    if (v && !std::strcmp(v,"auto"))  return cudaDeviceScheduleAuto;
    return cudaDeviceScheduleBlockingSync;
}
#endif
