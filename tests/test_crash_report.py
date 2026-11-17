"""Check crash evidence using an asset-free synthetic process, not the game."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="deadspace-crash-test-") as temporary:
        for mode in ("abort", "exception"):
            directory = Path(temporary) / ("reports-测试 & " + mode)
            directory.mkdir()
            result = subprocess.run([args.probe, str(directory), mode], timeout=30,
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            assert result.returncode != 0
            assert (directory / "crash.dmp").read_bytes()[:4] == b"MDMP"
            log = (directory / "probe.log").read_text(encoding="utf-8")
            assert "CRASH:" in log and "minidump written" in log, log
            assert (directory / "stderr.log").exists()
    print("synthetic abort/exception dumps and Unicode per-run destinations passed; no game executed")


if __name__ == "__main__":
    main()
