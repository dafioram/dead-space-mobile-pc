"""Run the shipped watcher against an asset-free fixture, never the game."""
import argparse
import csv
import json
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", required=True)
    parser.add_argument("--census-helper", required=True)
    parser.add_argument("--kit-template", type=Path)
    args = parser.parse_args()
    source = args.kit_template or Path(__file__).resolve().parents[1] / "tools/playthrough"
    with tempfile.TemporaryDirectory(prefix="deadspace-playthrough-test-") as temporary:
        root = Path(temporary)
        kit = root / "kit-测试 & % portable"
        shutil.copytree(source, kit)
        exe = kit / "deadspace.exe"
        shutil.copy2(args.probe, exe)
        shutil.copy2(args.census_helper, kit / "handle-census.exe")
        command = ["powershell.exe", "-NoLogo", "-NoProfile", "-ExecutionPolicy", "Bypass",
                   "-File", str(kit / "Watch-Playthrough.ps1")]
        # Missing inputs must fail before creating a profile or starting a child.
        result = subprocess.run(command, capture_output=True, timeout=30)
        assert result.returncode == 2, result.stdout + result.stderr
        assert not (kit / "userdata").exists()
        assert not (kit / "reports").exists()
        game = root / "data-测试 & % ' fixture"
        (game / "assets").mkdir(parents=True)
        (game / "libDeadSpace.so").write_bytes(b"synthetic, never loaded")
        (kit / "Playthrough-Paths.json").write_text(json.dumps({"GameDir": str(game) + "\\"}), encoding="utf-8")
        (game / "hold-ms.txt").write_text("5500")
        result = subprocess.run(command, capture_output=True, timeout=40)
        assert result.returncode == 0, result.stdout + result.stderr
        runs = list((kit / "reports").iterdir())
        assert len(runs) == 1
        first = runs[0]
        metadata = json.loads((first / "session.json").read_text(encoding="utf-8-sig"))
        assert metadata["state"] == "exited" and metadata["exit_code"] == 0, metadata
        assert metadata["watcher_errors"] == 0
        assert metadata["handle_census_attempts"] >= 1 and metadata["handle_census_failures"] == 0, metadata
        handles = list(csv.DictReader((first / "handle-types.csv").open(encoding="utf-8")))
        assert handles and all(row["valid"] == "1" for row in handles), handles
        rows = list(csv.DictReader((first / "process-memory.csv").open(encoding="utf-8")))
        assert len(rows) >= 2 and int(rows[0]["private_bytes"]) > 0, rows
        console = (first / "console.log").read_text(encoding="utf-8")
        assert str(kit / "userdata") in console and "synthetic-no-game-executed" in console, console
        assert str(first) in console and "--no-save" not in console
        assert "--perf-interval\n10" in console
        # A new run must keep previous evidence and persistent fixture progress.
        evidence = (first / "console.log").read_bytes()
        (game / "exit-code.txt").write_text("17")
        (game / "hold-ms.txt").write_text("0")
        result = subprocess.run(command, capture_output=True, timeout=30)
        assert result.returncode == 17, result.stdout + result.stderr
        assert (first / "console.log").read_bytes() == evidence
        runs = list((kit / "reports").iterdir())
        assert len(runs) == 2
        second = next(path for path in runs if path != first)
        metadata = json.loads((second / "session.json").read_text(encoding="utf-8-sig"))
        assert metadata["exit_code"] == 17 and metadata["exit_hex"] == "0x00000011", metadata
        assert (kit / "userdata/synthetic-progress.txt").read_text().splitlines() == ["fixture-run"] * 2
    print("watcher Unicode/quoting, samples, persistent profile, new reports and failure exit passed; no game executed")


if __name__ == "__main__":
    main()
