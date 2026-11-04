#!/usr/bin/env python3
"""Record bounded, headless original-app probes after an implementation change."""
import argparse
import datetime
import hashlib
import json
import pathlib
import platform
import subprocess
import sys


def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("fork", nargs="+", type=pathlib.Path)
    parser.add_argument("--steps", type=int, default=1_000_000)
    parser.add_argument("--indexed", action="store_true")
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()
    if not 1 <= args.steps <= 100_000_000:
        parser.error("steps must be 1..100000000")
    root = pathlib.Path(__file__).resolve().parents[1]
    binary = root / "build/ospi-runtime"
    if not binary.is_file():
        parser.error("build the runtime first: BUILDFLEET_LOCAL=1 make runtime")
    forks = [p.resolve(strict=True) for p in args.fork]
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    output = args.output or root / "build/checkpoints" / stamp
    output.mkdir(parents=True, exist_ok=False)
    files = subprocess.check_output(
        ["git", "ls-files", "-z", "-c", "-o", "--exclude-standard"], cwd=root
    ).decode().split("\0")
    manifest = {name: sha(root / name) for name in sorted(set(files))
                if name and (root / name).is_file() and not (root / name).is_symlink()}
    encoded = json.dumps(manifest, sort_keys=True).encode()
    (output / "source-manifest.json").write_bytes(encoded)
    report = {"utc": stamp, "host": platform.node(), "platform": platform.platform(),
              "binary_sha256": sha(binary), "source_manifest_sha256": hashlib.sha256(encoded).hexdigest(),
              "steps": args.steps, "indexed": args.indexed,
              "acceptance": "Gameplay, sound, saves and acceleration require separate evidence.",
              "targets": []}
    for index, fork in enumerate(forks, 1):
        command = [str(binary), *( ["--indexed"] if args.indexed else []), "--steps", str(args.steps), str(fork)]
        log = output / f"target-{index}.log"
        with log.open("wb") as stream:
            try:
                result = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT, timeout=30)
                code = result.returncode
            except subprocess.TimeoutExpired:
                code = "timeout"
        row = {"fork": str(fork), "fork_sha256": sha(fork), "command": command,
               "exit": code, "log": log.name, "log_sha256": sha(log)}
        report["targets"].append(row)
        print(f"{fork.name}: exit={code}; {log}")
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Checkpoint: {output}")
    return 0 if all(t["exit"] == 0 for t in report["targets"]) else 1


if __name__ == "__main__":
    sys.exit(main())
