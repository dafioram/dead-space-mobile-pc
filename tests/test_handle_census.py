"""Exercise only a synthetic Python process and its deliberately owned handles."""
import argparse
import csv
import ctypes as c
from ctypes import wintypes as w
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--helper", required=True)
    args = parser.parse_args()
    kernel = c.WinDLL("kernel32", use_last_error=True)
    kernel.GetCurrentProcess.restype = w.HANDLE
    kernel.GetProcessTimes.argtypes = [w.HANDLE, c.POINTER(w.FILETIME), c.POINTER(w.FILETIME), c.POINTER(w.FILETIME), c.POINTER(w.FILETIME)]
    kernel.CreateEventW.argtypes = [c.c_void_p, w.BOOL, w.BOOL, w.LPCWSTR]
    kernel.CreateEventW.restype = w.HANDLE
    kernel.CreateSemaphoreW.argtypes = [c.c_void_p, w.LONG, w.LONG, w.LPCWSTR]
    kernel.CreateSemaphoreW.restype = w.HANDLE
    kernel.CloseHandle.argtypes = [w.HANDLE]
    times = [w.FILETIME() for _ in range(4)]
    assert kernel.GetProcessTimes(kernel.GetCurrentProcess(), *(c.byref(t) for t in times))
    started = times[0].dwHighDateTime << 32 | times[0].dwLowDateTime
    with tempfile.TemporaryDirectory(prefix="deadspace-handle-test-") as temporary:
        output = Path(temporary) / "types-测试 & fixture.csv"
        command = [args.helper, "--pid", str(os.getpid()), "--started", str(started), "--out", str(output)]

        def capture():
            result = subprocess.run(command, capture_output=True, timeout=15)
            assert result.returncode == 0, (result.returncode, result.stdout, result.stderr, output.read_text() if output.exists() else "")
            rows = list(csv.DictReader(output.open(encoding="utf-8")))
            latest = rows[-1]["utc"]
            snapshot = [row for row in rows if row["utc"] == latest]
            assert all(row["valid"] == "1" for row in snapshot), snapshot
            assert sum(int(row["count"]) for row in snapshot) == int(snapshot[0]["total_handles"])
            return {row["type_name"]: int(row["count"]) for row in snapshot}

        before = capture()
        handles = [kernel.CreateEventW(None, False, False, None) for _ in range(8)]
        handles += [kernel.CreateSemaphoreW(None, 0, 1, None) for _ in range(3)]
        assert all(handles)
        try:
            during = capture()
            assert during.get("Event", 0) >= before.get("Event", 0) + 8, (before, during)
            assert during.get("Semaphore", 0) >= before.get("Semaphore", 0) + 3, (before, during)
        finally:
            for handle in handles:
                kernel.CloseHandle(handle)
        after = capture()
        assert after.get("Event", 0) <= during["Event"] - 8, (during, after)
        assert after.get("Semaphore", 0) <= during["Semaphore"] - 3, (during, after)
        bad = subprocess.run([args.helper, "--pid", str(os.getpid()), "--started", str(started + 1),
                              "--out", str(Path(temporary) / "wrong-pid-generation.csv")], capture_output=True, timeout=15)
        assert bad.returncode == 1
        row = list(csv.DictReader((Path(temporary) / "wrong-pid-generation.csv").open()))[-1]
        assert row["valid"] == "0" and row["error"] != "0"
        invalid = subprocess.run([args.helper, "--pid", "0", "--started", "1", "--out", str(output)], timeout=15)
        assert invalid.returncode == 2
    print("process-local handle types, create/close deltas, Unicode CSV and PID-generation refusal passed; no game executed")


if __name__ == "__main__":
    main()
