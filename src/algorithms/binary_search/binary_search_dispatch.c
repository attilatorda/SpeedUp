#include "speedup/algorithms/binary_search.h"
#include "speedup/backend/dispatch.h"

int speedup_cuda_available(void);
int speedup_opencl_available(void);
int speedup_cuda_binary_search_stub(void);
int speedup_opencl_binary_search_stub(void);

int64_t speedup_binary_search_i64(const int64_t* array, int64_t key, int64_t size) {
    speedup_backend_pref_t pref = speedup_get_backend_preference();

    if (pref == SPEEDUP_BACKEND_FORCE_CUDA) {
        if (speedup_cuda_available() && speedup_cuda_binary_search_stub() == 0) {
            return -1;
        }
        return speedup_binary_search_i64_ref(array, key, size);
    }

    if (pref == SPEEDUP_BACKEND_FORCE_OPENCL) {
        if (speedup_opencl_available() && speedup_opencl_binary_search_stub() == 0) {
            return -1;
        }
        return speedup_binary_search_i64_ref(array, key, size);
    }

    if (pref == SPEEDUP_BACKEND_FORCE_CPU) {
        return speedup_binary_search_i64_ref(array, key, size);
    }

    if (speedup_cuda_available() && speedup_cuda_binary_search_stub() == 0) {
        return -1;
    }
    if (speedup_opencl_available() && speedup_opencl_binary_search_stub() == 0) {
        return -1;
    }

    return speedup_binary_search_i64_ref(array, key, size);
}
