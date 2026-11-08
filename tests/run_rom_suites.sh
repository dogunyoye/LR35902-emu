#!/usr/bin/env bash
# Run the Mooneye, blargg, dmg-acid2, and Mealybug Tearoom test ROMs through rom_test_runner.
#
# Usage: tests/run_rom_suites.sh [-o OUTPUT_DIR] [--mooneye DIR] [--blargg DIR] [--acid2 DIR] [--cgb-acid2 DIR]
#                                [--mealybug DIR]
#   --mooneye DIR   extracted mooneye-test-suite release (contains acceptance/)
#   --blargg DIR    gb-test-roms checkout (contains cpu_instrs/)
#   --acid2 DIR     dmg-acid2 checkout with dmg-acid2.gb built or downloaded into it
#   --cgb-acid2 DIR cgb-acid2 checkout with cgb-acid2.gbc built or downloaded into it
#   --mealybug DIR  mealybug-tearoom-tests checkout (its ROM zip is unpacked into OUTPUT_DIR)
# Writes one log per ROM and results.tsv to OUTPUT_DIR (default: build/rom-results).
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
RUNNER=${RUNNER:-$ROOT/build/rom_test_runner}
out=$ROOT/build/rom-results
mooneye= blargg= acid2= cgb_acid2= mealybug=
while [ $# -gt 0 ]; do
  case $1 in
    -o) out=$2; shift 2 ;;
    --mooneye) mooneye=$2; shift 2 ;;
    --blargg) blargg=$2; shift 2 ;;
    --acid2) acid2=$2; shift 2 ;;
    --cgb-acid2) cgb_acid2=$2; shift 2 ;;
    --mealybug) mealybug=$2; shift 2 ;;
    *) echo "Unknown argument: $1" >&2; exit 2 ;;
  esac
done
mkdir -p "$out"

# Mooneye encodes the target model in the file name.
model_for() {
  case "$1" in
    *sgb2*) echo sgb2 ;;
    *sgb*|*-S.gb) echo sgb ;;
    *mgb*) echo mgb ;;
    *dmg0*) echo dmg0 ;;
    *cgb0*) echo cgb0 ;;
    *cgb*|*-C.gb) echo cgb ;;
    *-A.gb) echo agb ;;
    *) echo dmgABC ;;
  esac
}

run_one() {
  suite=$1 rom=$2 model=$3 cycles=$4
  log="$out/$suite-$(basename "$rom" .gb | tr ' ,()' '____').log"
  "$RUNNER" "$rom" "$cycles" "$model" >"$log" 2>&1
  case $? in
    0) result=PASS ;;
    1) result=FAIL ;;
    *) result=NORESULT ;;
  esac
  printf '%s\t%s\t%s\n' "$result" "$suite" "${rom#"$5"/}"
}
export -f run_one model_for
export RUNNER out

{
  if [ -n "$mooneye" ]; then
    find "$mooneye" -name '*.gb' ! -path '*/utils/*' ! -path '*/manual-only/*' -print0 |
      xargs -0 -P 8 -I{} bash -c 'run_one mooneye "$1" "$(model_for "$(basename "$1")")" 50000000 "$2"' _ {} "$mooneye"
  fi
  if [ -n "$blargg" ]; then
    # The cgb_sound and interrupt_time ROMs only run on the Game Boy Color, so
    # the runner picks the CGB for them. The standalone oam_bug 7-timing_effect
    # ROM writes its output to cartridge RAM without a bound and overflows into
    # its own code; the combined oam_bug.gb runs the same test and checks that
    # output by CRC.
    find "$blargg" -name '*.gb' ! -path '*/oam_bug/rom_singles/7-timing_effect.gb' -print0 |
      xargs -0 -P 8 -I{} bash -c 'run_one blargg "$1" auto 1000000000 "$2"' _ {} "$blargg"
    # The CPU, timing, and HALT tests also run in CGB mode on a CGB.
    find "$blargg" -name '*.gb' \( -path '*/cpu_instrs/*' -o -path '*/instr_timing/*' -o -path '*/mem_timing*' \
      -o -name 'halt_bug.gb' \) -print0 |
      xargs -0 -P 8 -I{} bash -c 'run_one blargg-cgb "$1" cgb 1000000000 "$2"' _ {} "$blargg"
  fi
  if [ -n "$acid2" ]; then
    if "$RUNNER" --screenshot "$out/dmg-acid2.ppm" "$acid2/dmg-acid2.gb" 20000000 >"$out/dmg-acid2.log" 2>&1 &&
       python3 "$ROOT/tests/compare_screenshot.py" "$out/dmg-acid2.ppm" "$acid2/img/reference-dmg.png" >>"$out/dmg-acid2.log" 2>&1; then
      printf 'PASS\tacid2\tdmg-acid2.gb\n'
    else
      printf 'FAIL\tacid2\tdmg-acid2.gb\n'
    fi
  fi
  if [ -n "$cgb_acid2" ]; then
    if "$RUNNER" --screenshot "$out/cgb-acid2.ppm" "$cgb_acid2/cgb-acid2.gbc" 20000000 >"$out/cgb-acid2.log" 2>&1 &&
       python3 "$ROOT/tests/compare_screenshot.py" --rgb "$out/cgb-acid2.ppm" "$cgb_acid2/img/reference.png" \
         >>"$out/cgb-acid2.log" 2>&1; then
      printf 'PASS\tcgb-acid2\tcgb-acid2.gbc\n'
    else
      printf 'FAIL\tcgb-acid2\tcgb-acid2.gbc\n'
    fi
  fi
  if [ -n "$mealybug" ]; then
    # Screenshot tests compared against the DMG reference images.
    unzip -o -q "$mealybug/mealybug-tearoom-tests.zip" -d "$out/mealybug-roms"
    for rom in "$out"/mealybug-roms/*.gb; do
      name=$(basename "$rom" .gb)
      reference="$mealybug/expected/DMG-blob/$name.png"
      [ -f "$reference" ] || continue
      if "$RUNNER" --screenshot "$out/mealybug-$name.ppm" "$rom" 20000000 >"$out/mealybug-$name.log" 2>&1 &&
         python3 "$ROOT/tests/compare_screenshot.py" "$out/mealybug-$name.ppm" "$reference" >>"$out/mealybug-$name.log" 2>&1; then
        printf 'PASS\tmealybug\t%s.gb\n' "$name"
      else
        printf 'FAIL\tmealybug\t%s.gb\n' "$name"
      fi
    done
  fi
} | sort -k1,1 -k2,2 -k3,3 >"$out/results.tsv"

cut -f1 "$out/results.tsv" | sort | uniq -c
echo
grep -v '^PASS' "$out/results.tsv"
exit 0
