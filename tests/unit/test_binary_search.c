#include <assert.h>
#include <stdint.h>
#include "speedup/algorithms/binary_search.h"
int main(void){int64_t a[5]={2,4,6,8,10};assert(speedup_binary_search_i64(a,6,5)==2);assert(speedup_binary_search_i64(a,7,5)==-1);return 0;}
