#!/usr/bin/env bash
# Exercise Virtual edit / disable / reactivate using a disposable MMEX database.
set -euo pipefail
if [[ $# -ne 3 ]]; then
  echo "Usage: $0 <mmex-binary> <synthetic-v22.mmb> <output-directory>" >&2
  exit 2
fi

binary="$1"
source_database="$2"
output_dir="$3"
mkdir -p "$output_dir"/{home,config,data}
database="$output_dir/edited-copy-v22.mmb"
cp "$source_database" "$database"

export HOME="$output_dir/home"
export XDG_CONFIG_HOME="$output_dir/config"
export XDG_DATA_HOME="$output_dir/data"
export GDK_BACKEND=x11
export NO_AT_BRIDGE=1
export WEBKIT_DISABLE_COMPOSITING_MODE=1

openbox --sm-disable > "$output_dir/window-manager.log" 2>&1 &
manager_pid=$!
"$binary" -s "$database" > "$output_dir/mmex-edit.log" 2>&1 &
app_pid=$!
cleanup() {
  kill "$app_pid" "$manager_pid" 2>/dev/null || true
  wait "$app_pid" 2>/dev/null || true
}
trap cleanup EXIT

window=""
for attempt in $(seq 1 65); do
  if ! kill -0 "$app_pid" 2>/dev/null; then
    echo "MMEX exited before the editor test." >&2
    exit 1
  fi
  window="$(xdotool search --onlyvisible --pid "$app_pid" 2>/dev/null | head -n 1 || true)"
  if [[ -n "$window" ]]; then break; fi
  sleep 1
done
if [[ -z "$window" ]]; then
  echo "No MMEX GUI appeared." >&2
  exit 1
fi

if [[ "$(xdotool getwindowname "$window")" == "License agreement" ]]; then
  xdotool windowactivate --sync "$window"
  xdotool mousemove --window "$window" 77 533 click 1
  sleep 1
  xdotool mousemove --window "$window" 205 573 click 1
  sleep 2
  if xdotool search --onlyvisible --name "^License agreement$" >/dev/null 2>&1; then
    xdotool windowactivate --sync "$window" || true
    xdotool key --clearmodifiers Tab Return
  fi
fi
sleep 7

main_window="$(xdotool search --onlyvisible --pid "$app_pid" 2>/dev/null | tail -n 1 || true)"
if [[ -z "$main_window" ]] || [[ "$(xdotool getwindowname "$main_window")" != *"Money Manager Ex"* ]]; then
  echo "Main MMEX window did not appear." >&2
  exit 1
fi
xdotool windowactivate --sync "$main_window"
xdotool mousemove 106 172 click 1
sleep 3

open_editor() {
  xdotool mousemove 283 171 click 1
  sleep 1
  xdotool mousemove 338 795 click 1
  sleep 2
  if ! xdotool search --onlyvisible --name "^Edit Scheduled Transaction$" >/dev/null 2>&1; then
    echo "The scheduled transaction edit dialog did not appear." >&2
    scrot "$output_dir/edit-dialog-missing.png"
    exit 1
  fi
}

verify_database() {
  local stage="$1"
  python3 - "$database" "$source_database" "$stage" <<'PY'
import sqlite3
import sys
from datetime import date

working, original, stage = sys.argv[1:]
with sqlite3.connect(working) as current, sqlite3.connect(original) as initial:
    assert current.execute("PRAGMA integrity_check").fetchone() == ("ok",)
    posted = current.execute(
        "SELECT * FROM CHECKINGACCOUNT_V1 ORDER BY TRANSID"
    ).fetchall()
    posted_original = initial.execute(
        "SELECT * FROM CHECKINGACCOUNT_V1 ORDER BY TRANSID"
    ).fetchall()
    assert posted == posted_original, "GUI Virtual operation posted a real transaction"

    rows = current.execute(
        "SELECT * FROM BILLSDEPOSITS_V1 WHERE BDID != 920 ORDER BY BDID"
    ).fetchall()
    original_rows = initial.execute(
        "SELECT * FROM BILLSDEPOSITS_V1 WHERE BDID != 920 ORDER BY BDID"
    ).fetchall()
    assert rows == original_rows, "Another recurring schedule changed unexpectedly"

    target = current.execute(
        "SELECT TRANSAMOUNT, ISVIRTUAL, VIRTUALFREQUENCY, VIRTUALSTARTDATE, "
        "NUMOCCURRENCES, NEXTOCCURRENCEDATE, REPEATS "
        "FROM BILLSDEPOSITS_V1 WHERE BDID=920"
    ).fetchone()
    assert target is not None
    amount, is_virtual, frequency, started, payments_left, due, repeats = target
    assert abs(float(amount) - 35.0) < 0.000001, (stage, target)
    assert frequency == "Weekly", (stage, target)
    assert payments_left == 5, (stage, target)
    assert due == "2026-11-30", (stage, target)
    assert repeats == 3, (stage, target)
    expected = {
        "amount_edit": (1, "2026-11-20"),
        "disabled": (0, None),
        "reactivated": (1, date.today().isoformat()),
    }[stage]
    assert (is_virtual, started) == expected, (stage, target, expected)
    print(f"GUI {stage}: {target}; real financial transactions unchanged")
PY
}

open_editor
scrot "$output_dir/edit-before.png"
xdotool mousemove 846 330 click 1
xdotool key --clearmodifiers ctrl+a
xdotool type --clearmodifiers "35.00"
xdotool mousemove 670 755 click 1
sleep 2
scrot "$output_dir/edit-amount-saved.png"
verify_database amount_edit

open_editor
xdotool mousemove 343 297 click 1
sleep 1
scrot "$output_dir/virtual-unchecked.png"
xdotool mousemove 670 755 click 1
sleep 2
verify_database disabled

open_editor
xdotool mousemove 343 297 click 1
sleep 1
scrot "$output_dir/virtual-reactivated.png"
xdotool mousemove 670 755 click 1
sleep 2
verify_database reactivated

cleanup
trap - EXIT
