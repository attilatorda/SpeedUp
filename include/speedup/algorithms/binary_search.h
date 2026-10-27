#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* All functions take an array sorted in ascending order. */

/* Index of an element equal to key, or -1. When key occurs more than once,
 * the first occurrence. */
int64_t speedup_binary_search_i64(const int64_t* array, int64_t key, int64_t size);

/* Textbook branching search with early exit; the baseline the others are
 * tested and measured against. Any matching index, or -1. */
int64_t speedup_binary_search_i64_ref(const int64_t* array, int64_t key, int64_t size);

/* First index i with array[i] >= key; size when every element is smaller. */
int64_t speedup_lower_bound_i64(const int64_t* array, int64_t key, int64_t size);

/* Many keys at once: results[i] is what speedup_binary_search_i64 (or
 * speedup_lower_bound_i64) returns for keys[i]. On large arrays this is
 * several times faster than calling the single-key function in a loop,
 * because the searches overlap their cache misses. */
void speedup_binary_search_batch_i64(const int64_t* array, int64_t size,
                                     const int64_t* keys, int64_t count, int64_t* results);
void speedup_lower_bound_batch_i64(const int64_t* array, int64_t size,
                                   const int64_t* keys, int64_t count, int64_t* results);

#ifdef __cplusplus
}
#endif
