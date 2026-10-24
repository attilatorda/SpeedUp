# SpeedUp Bucketization for Java

Pure-Java implementation of the `Bucketization` interface: fast re-sorting of
a collection that can be ordered in several ways (name, size, date, ...).
Java 17+, no runtime dependencies.

Design, guarantees and the cross-language specification:
[docs/bucketization/README.md](../../../docs/bucketization/README.md).

## Build and test

```bash
cd bindings/java/bucketization
mvn test        # unit, randomized and cross-language golden tests
mvn package     # target/speedup-bucketization-0.1.0-SNAPSHOT.jar
```

## Usage

```java
import io.github.attilatorda.speedup.bucketization.*;

record FileItem(String name, long size, long modified) {}

BucketIndex<FileItem> files = new BucketIndex<>(BucketPlan.defaults().withPageSize(visibleRows));
files.addSortKey(SortKey.ofCollated("name", FileItem::name, Collator.getInstance()));
files.addSortKey(SortKey.ofLong("size", FileItem::size));
files.addSortKey(SortKey.ofLong("modified", FileItem::modified));
files.addAll(listing);                         // every item picks a bucket per sort key

files.sortBy("modified", Direction.DESCENDING); // O(n) regrouping, no comparisons
List<FileItem> rows = files.range(0, visibleRows); // sorts only the bucket(s) on screen
```

| Class | Role |
|-------|------|
| `Bucketization<T>` | the interface |
| `BucketIndex<T>` | reference implementation |
| `SortKey<T>` | a sort criterion: `ofLong`, `ofDouble`, `ofString`, `ofStringIgnoreCase`, `ofCollated`, `ofKey`, `ofComparator`; `withGroups` keeps folders first in both directions |
| `Keys` | order-preserving 64-bit key encodings for custom keys |
| `BucketPlan` | target bucket sizes: `defaults`, `firstPage`, `geometric`, `uniform`, `ofSizes` |
| `BucketLayout<T>` | split points and expected bucket sizes (the distribution model) |
| `BucketStats` | skew, empty buckets, first-page cost |

Elements are kept by reference. After changing an element in a way that
affects its order, call `update(element)` or `updateIf(filter, mutator)`. The
index is not thread-safe.

## Demo and benchmark

```bash
mvn -q test-compile
java -cp target/classes:target/test-classes \
    io.github.attilatorda.speedup.bucketization.examples.FileBrowserDemo /some/directory 20
java -Xmx4g -cp target/classes:target/test-classes \
    io.github.attilatorda.speedup.bucketization.examples.BucketizationBenchmark 1000000
```
