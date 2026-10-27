# CPP setup

TODO: usage with SpeedUp C ABI.

## Bucketization (header-only C++17)

```cmake
add_subdirectory(SpeedUp)
target_link_libraries(my_app PRIVATE speedup::bucketization)
```

```cpp
#include "speedup/bucketization.hpp"
namespace bz = speedup::bucketization;

bz::BucketIndex<FileItem> files;
files.add_sort_key(bz::SortKey<FileItem>::of("name", &FileItem::name));
files.add_sort_key(bz::SortKey<FileItem>::of("size", &FileItem::size));
files.add_all(items);
files.sort_by("size", bz::Direction::descending);
std::vector<FileItem> visible = files.first_page();
```

More: `docs/bucketization/README.md` and `examples/bucketization/file_browser.cpp`.
