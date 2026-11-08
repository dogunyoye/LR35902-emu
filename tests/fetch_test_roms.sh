#!/usr/bin/env bash
# Download the test ROM suites that run_rom_suites.sh runs, at pinned versions.
#
# Usage: tests/fetch_test_roms.sh [DIR]
#   DIR  where to put them (default: build/test-roms); then run
#        tests/run_rom_suites.sh --roms DIR
# Suites already downloaded are skipped. Requires curl and tar with xz support.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
dir=${1:-$ROOT/build/test-roms}
mkdir -p "$dir"

MOONEYE=mts-20260714-0944-31510e1
BLARGG_COMMIT=c240dd7d700e5c0b00a7bbba52b53e4ee67b5f15
DMG_ACID2_COMMIT=8a98ce731f96dde032ffb22ec36dc985d78fdb18
CGB_ACID2_COMMIT=04c6ca40cf75b6a93513fe596de4ab797efaff97
MEALYBUG_COMMIT=70e88fb90b59d19dfbb9c3ac36c64105202bb1f4

# fetch NAME URL [EXTRA_FILE_URL]: unpacks the archive at URL into DIR/NAME,
# dropping its top-level folder, and adds EXTRA_FILE_URL's file beside it.
fetch() {
  local name=$1 url=$2 extra=${3:-}
  if [ -f "$dir/$name/.complete" ]; then
    echo "$name: already downloaded"
    return
  fi
  echo "$name: downloading"
  # Unpack beside the target and move it into place, so an interrupted
  # download is not mistaken for a finished one.
  local partial="$dir/$name.partial"
  rm -rf "$partial" "$dir/$name"
  mkdir -p "$partial"
  curl -sSfL "$url" | tar -x --strip-components=1 -C "$partial" -f - $([[ $url == *.xz ]] && echo -J || echo -z)
  if [ -n "$extra" ]; then
    curl -sSfL -o "$partial/$(basename "$extra")" "$extra"
  fi
  touch "$partial/.complete"
  mv "$partial" "$dir/$name"
}

fetch mooneye "https://gekkio.fi/files/mooneye-test-suite/$MOONEYE/$MOONEYE.tar.xz"
fetch blargg "https://github.com/retrio/gb-test-roms/archive/$BLARGG_COMMIT.tar.gz"
fetch dmg-acid2 "https://github.com/mattcurrie/dmg-acid2/archive/$DMG_ACID2_COMMIT.tar.gz" \
  https://github.com/mattcurrie/dmg-acid2/releases/download/v1.0/dmg-acid2.gb
fetch cgb-acid2 "https://github.com/mattcurrie/cgb-acid2/archive/$CGB_ACID2_COMMIT.tar.gz" \
  https://github.com/mattcurrie/cgb-acid2/releases/download/v1.1/cgb-acid2.gbc
fetch mealybug "https://github.com/mattcurrie/mealybug-tearoom-tests/archive/$MEALYBUG_COMMIT.tar.gz"

echo
echo "Run them with: tests/run_rom_suites.sh --roms $dir"
