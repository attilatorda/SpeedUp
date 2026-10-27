#include "speedup/algorithms/binary_search.h"
#include "speedup/backend/dispatch.h"

int64_t speedup_binary_search_i64_cpu(const int64_t* array, int64_t key, int64_t size);

/* The CUDA and OpenCL backends have no binary search yet, so every
 * preference, forced or not, ends on the CPU path. A GPU backend joins here
 * once it searches; until then a forced GPU preference must not change the
 * result. */
int64_t speedup_binary_search_i64(const int64_t* array, int64_t key, int64_t size) {
    return speedup_binary_search_i64_cpu(array, key, size);
}
