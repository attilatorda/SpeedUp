# JAVA setup

TODO: usage with SpeedUp C ABI.

## Bucketization (pure Java)

```bash
cd bindings/java/bucketization
mvn test
mvn package   # target/speedup-bucketization-0.1.0-SNAPSHOT.jar
```

```java
BucketIndex<FileItem> files = new BucketIndex<>();
files.addSortKey(SortKey.ofString("name", FileItem::name));
files.addSortKey(SortKey.ofLong("size", FileItem::size));
files.addAll(items);
files.sortBy("size", Direction.DESCENDING);
List<FileItem> visible = files.firstPage();
```

More: `bindings/java/bucketization/README.md` and `docs/bucketization/README.md`.
