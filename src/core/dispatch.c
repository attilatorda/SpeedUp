#include "speedup/backend/dispatch.h"
static uint32_t g_threads = 0;
static speedup_cache_hint_t g_cache = {32768,262144,8388608,64};
static speedup_backend_pref_t g_pref = SPEEDUP_BACKEND_AUTO;
/* Arrays of up to this many 8-byte elements fit the L2 hint; the binary
 * search reads it on every call, so it is kept precomputed. */
int64_t speedup_internal_l2_elements = 262144 / 8;
void speedup_set_threads_hint(uint32_t n){g_threads=n;}
void speedup_set_cache_hint(speedup_cache_hint_t hint){g_cache=hint; speedup_internal_l2_elements=(int64_t)hint.l2_bytes/8;}
void speedup_set_backend_preference(speedup_backend_pref_t pref){g_pref=pref;}
uint32_t speedup_get_threads_hint(void){return g_threads;}
speedup_cache_hint_t speedup_get_cache_hint(void){return g_cache;}
speedup_backend_pref_t speedup_get_backend_preference(void){return g_pref;}
