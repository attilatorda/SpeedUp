#include "speedup/backend/dispatch.h"
static uint32_t g_threads = 0;
static speedup_cache_hint_t g_cache = {32768,262144,8388608,64};
static speedup_backend_pref_t g_pref = SPEEDUP_BACKEND_AUTO;
void speedup_set_threads_hint(uint32_t n){g_threads=n;}
void speedup_set_cache_hint(speedup_cache_hint_t hint){g_cache=hint;}
void speedup_set_backend_preference(speedup_backend_pref_t pref){g_pref=pref;}
