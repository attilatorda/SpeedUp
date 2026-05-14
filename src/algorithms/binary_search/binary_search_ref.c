#include "speedup/algorithms/binary_search.h"
int64_t speedup_binary_search_i64_ref(const int64_t* array, int64_t key, int64_t size){
 int64_t left=0,right=size-1;
 while(left<=right){int64_t mid=left+(right-left)/2; if(array[mid]==key) return mid; if(array[mid]<key) left=mid+1; else right=mid-1;}
 return -1;
}
