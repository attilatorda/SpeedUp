#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef enum speedup_backend_pref_t {
    SPEEDUP_BACKEND_AUTO = 0,
    SPEEDUP_BACKEND_FORCE_CPU = 1,
    SPEEDUP_BACKEND_FORCE_CUDA = 2,
    SPEEDUP_BACKEND_FORCE_OPENCL = 3
} speedup_backend_pref_t;

typedef struct speedup_cache_hint_t {
    uint32_t l1_bytes;
    uint32_t l2_bytes;
    uint32_t l3_bytes;
    uint32_t cacheline_bytes;
} speedup_cache_hint_t;

void speedup_set_threads_hint(uint32_t n);
void speedup_set_cache_hint(speedup_cache_hint_t hint);
void speedup_set_backend_preference(speedup_backend_pref_t pref);

uint32_t speedup_get_threads_hint(void);
speedup_cache_hint_t speedup_get_cache_hint(void);
speedup_backend_pref_t speedup_get_backend_preference(void);

/* 1 when an OpenCL GPU was found (the OpenCL loader is opened on the first
 * call), and that device's name, or "". */
int speedup_opencl_available(void);
const char* speedup_opencl_device_name(void);

#ifdef __cplusplus
}
#endif
