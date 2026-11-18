"""Print the CTest filter for one duration-balanced ThreadSanitizer shard."""

import argparse
import json
import re
import subprocess
import sys


# CTestCostData.txt from the general TSan job in PR validation run 44368428319.
# Tests under one second and newly registered tests get a one-second estimate.
ESTIMATED_SECONDS = {
    "audio-worker": 10.28,
    "extended-horizon": 8.74,
    "extended-replay-engine": 43.82,
    "extended-track": 7.42,
    "ghost-racing-line": 3.06,
    "host-regressions": 78.30,
    "html-track-3d-resources": 1.82,
    "html-track-editor": 2.87,
    "html-track-validation": 3.40,
    "html-track-validation-native": 7.17,
    "hypervision": 17.81,
    "mcmurtry-assets": 1.31,
    "mcmurtry-hypervision": 22.20,
    "menu-background": 3.25,
    "regression-timeouts": 2.08,
    "render-workers": 10.20,
    "replay-widescreen": 5.79,
    "sdl3-audio": 2.16,
    "sdl3-hires": 11.88,
    "sdl3-input": 17.95,
    "sdl3-music": 2.83,
    "sdl3-touch": 2.13,
    "shape3d-hires": 33.09,
    "skybox-assets": 1.30,
    "skybox-hires": 25.80,
    "track-preview-skybox": 2.26,
    "unit-bank-replay": 44.31,
    "unit-fixed-work-budget": 1.07,
    "unit-human-independent-grid-normal-prepared-threaded": 52.36,
    "unit-human-independent-grid-owoot-prepared-threaded": 116.85,
    "unit-human-independent-near-prepared-sync": 4.22,
    "unit-human-independent-near-prepared-threaded": 4.64,
    "unit-learning-persistence": 33.26,
    "unit-race-ansx": 1.58,
    "unit-race-branches-no-stalls": 2.13,
    "unit-race-checkpoints": 1.03,
    "unit-race-default": 2.16,
    "unit-race-default-no-stalls": 6.40,
    "unit-race-glacier-owoot-sync": 287.55,
    "unit-race-glacier-owoot-worker": 313.49,
    "unit-race-glacier-sync": 56.14,
    "unit-race-glacier-worker": 137.05,
    "unit-race-helens-manual": 34.62,
    "unit-race-joes-manual": 4.04,
    "unit-race-keukenhof-no-stalls": 29.59,
    "unit-race-lm02": 1.66,
    "unit-race-manual": 1.59,
    "unit-race-native-grid": 406.31,
    "unit-race-ninhbinh-no-stalls": 31.98,
    "unit-race-owoot": 1.11,
    "unit-race-prepared": 6.78,
    "unit-race-prepared-worker": 1.37,
    "unit-race-skids-manual-faster": 6.03,
    "unit-race-skids-short-launch-sync": 5.84,
    "unit-race-skids-short-launch-threaded": 9.67,
    "video-mode-replay": 69.23,
    "widescreen": 5.33,
    "widescreen-cursor": 2.50,
}


def balance(tests, shard_count):
    shards = [[] for _ in range(shard_count)]
    totals = [0.0] * shard_count
    weighted = sorted((-ESTIMATED_SECONDS.get(test, 1.0), test) for test in tests)
    for negative_seconds, test in weighted:
        shard = min(range(shard_count), key=totals.__getitem__)
        shards[shard].append(test)
        totals[shard] -= negative_seconds
    return shards, totals


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_directory")
    parser.add_argument("shard", type=int, help="One-based shard number")
    parser.add_argument("shards", type=int, help="Number of shards")
    parser.add_argument("include_regex")
    parser.add_argument("exclude_regex")
    args = parser.parse_args()
    if not 1 <= args.shard <= args.shards:
        parser.error("shard must be between 1 and the number of shards")

    listing = subprocess.check_output(
        ["ctest", "--test-dir", args.build_directory, "--show-only=json-v1",
         "--tests-regex", args.include_regex, "--exclude-regex", args.exclude_regex],
        text=True,
    )
    tests = {test["name"] for test in json.loads(listing)["tests"]}
    if len(tests) < args.shards:
        parser.error(f"cannot split {len(tests)} selected tests into {args.shards} nonempty shards")

    shards, totals = balance(tests, args.shards)
    selected = shards[args.shard - 1]
    seconds = totals[args.shard - 1]
    print(f"Estimated test time for ThreadSanitizer shard {args.shard}/{args.shards} "
          f"is {seconds:.0f} seconds", file=sys.stderr)
    print("^(" + "|".join(re.escape(name) for name in selected) + ")$")


if __name__ == "__main__":
    main()
