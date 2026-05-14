import ctypes
import os
from ctypes import c_int64, POINTER


def main() -> int:
    lib_path = os.environ.get("SPEEDUP_LIB")
    if not lib_path:
        raise RuntimeError("SPEEDUP_LIB must point to a built shared library (.so/.dll/.dylib)")

    if not os.path.exists(lib_path):
        raise FileNotFoundError(f"SPEEDUP_LIB does not exist: {lib_path}")

    lib = ctypes.CDLL(lib_path)
    lib.speedup_binary_search_i64.argtypes = [POINTER(c_int64), c_int64, c_int64]
    lib.speedup_binary_search_i64.restype = c_int64

    arr = (c_int64 * 5)(2, 4, 6, 8, 10)
    found = lib.speedup_binary_search_i64(arr, 6, 5)
    missing = lib.speedup_binary_search_i64(arr, 7, 5)

    assert found == 2, f"expected 2, got {found}"
    assert missing == -1, f"expected -1, got {missing}"
    print("Python smoke test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
