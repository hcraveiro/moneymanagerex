#!/usr/bin/env bash
# Run MMEX with a synthetic v22 database inside an isolated X11 virtual display.
set -euo pipefail

if [[ $# -ne 3 ]]; then
  echo "Usage: $0 <mmex-executable> <synthetic-v22.mmb> <output-directory>" >&2
  exit 2
fi

binary="$1"
source_database="$2"
output_dir="$3"
mkdir -p "$output_dir/home" "$output_dir/config" "$output_dir/data"
cp "$source_database" "$output_dir/working-copy-v22.mmb"

export HOME="$output_dir/home"
export XDG_CONFIG_HOME="$output_dir/config"
export XDG_DATA_HOME="$output_dir/data"
export GDK_BACKEND=x11
export NO_AT_BRIDGE=1
export WEBKIT_DISABLE_COMPOSITING_MODE=1

# Start a lightweight X11 window manager to support window activation and
# keyboard focus, which Xvfb alone does not provide.
openbox --sm-disable > "$output_dir/window-manager.log" 2>&1 &
manager_pid=$!
sleep 2

database="$output_dir/working-copy-v22.mmb"
log_file="$output_dir/mmex.log"
windows_file="$output_dir/windows.txt"
snapshot_file="$output_dir/ledger-before.json"

python3 - "$database" "$snapshot_file" <<'PY'
import json
import sqlite3
import sys

with sqlite3.connect(sys.argv[1]) as connection:
    data = {
        "posted": [
            list(record) for record in connection.execute(
                "SELECT * FROM CHECKINGACCOUNT_V1 ORDER BY TRANSID"
            )
        ],
        "scheduled": [
            list(record) for record in connection.execute(
                "SELECT * FROM BILLSDEPOSITS_V1 ORDER BY BDID"
            )
        ],
    }
with open(sys.argv[2], "w", encoding="utf-8") as output:
    json.dump(data, output, ensure_ascii=False)
PY

"$binary" -s "$database" > "$log_file" 2>&1 &
app_pid=$!
cleanup() {
  kill "$app_pid" 2>/dev/null || true
  wait "$app_pid" 2>/dev/null || true
}
trap cleanup EXIT

found_window=""
for attempt in $(seq 1 70); do
  if ! kill -0 "$app_pid" 2>/dev/null; then
    echo "MMEX exited before showing a graphical window." >&2
    cat "$log_file" >&2
    exit 1
  fi
  found_window="$(xdotool search --onlyvisible --pid "$app_pid" 2>/dev/null | head -n 1 || true)"
  if [[ -n "$found_window" ]]; then
    break
  fi
  sleep 1
done

wmctrl -lp > "$windows_file" || true
if [[ -z "$found_window" ]]; then
  echo "MMEX produced no visible window after startup." >&2
  cat "$windows_file" >&2
  cat "$log_file" >&2
  exit 1
fi

window_title="$(xdotool getwindowname "$found_window" 2>/dev/null || true)"
printf 'MMEX window id: %s\nWindow title: %s\n' "$found_window" "$window_title" \
  | tee "$output_dir/window-summary.txt"

# The first installation displays a privacy dialog. Explicitly opt out of
# anonymous statistics before acknowledging the dialog in our test profile.
if [[ "$window_title" == "License agreement" ]]; then
  scrot "$output_dir/mmex-initial.png"
  xdotool windowactivate --sync "$found_window"
  xdotool mousemove --window "$found_window" 77 533 click 1
  sleep 1
  xdotool mousemove --window "$found_window" 205 573 click 1
  sleep 2
  # If window decorations or GTK's focus policy prevent the mouse click,
  # send the default button's keyboard activation.
  if xdotool search --onlyvisible --name "^License agreement$" >/dev/null 2>&1; then
    xdotool windowactivate --sync "$found_window" || true
    xdotool key --clearmodifiers Tab Return
  fi
  sleep 5
fi

sleep 8
wmctrl -lp > "$windows_file" || true
scrot "$output_dir/mmex-after-startup.png"
for window in $(xdotool search --onlyvisible --pid "$app_pid" 2>/dev/null || true); do
  printf 'Visible window: %s %s\n' "$window" \
    "$(xdotool getwindowname "$window" 2>/dev/null || true)" \
    >> "$output_dir/window-summary.txt"
done
if grep -q 'Visible window: .*License agreement' "$output_dir/window-summary.txt"; then
  echo "MMEX is still showing the first-run privacy dialog." >&2
  exit 1
fi

# A real application window must be visible, not merely a running process.
if ! kill -0 "$app_pid" 2>/dev/null || [[ -z "$(xdotool search --onlyvisible --pid "$app_pid" 2>/dev/null || true)" ]]; then
  echo "MMEX stopped or lost all graphical windows." >&2
  exit 1
fi

main_window="$(xdotool search --onlyvisible --pid "$app_pid" 2>/dev/null | tail -n 1 || true)"
main_title="$(xdotool getwindowname "$main_window" 2>/dev/null || true)"
if [[ "$main_title" != *"Money Manager Ex"* ]]; then
  echo "Main MMEX window not found: $main_title" >&2
  exit 1
fi
xdotool windowactivate --sync "$main_window"

# Read-only visual checks of the actual Desktop navigation views.
xdotool mousemove 111 171 click 1
sleep 3
scrot "$output_dir/mmex-scheduled-transactions.png"
xdotool mousemove 66 327 click 1
sleep 2
scrot "$output_dir/mmex-reports-navigation.png"

cleanup
trap - EXIT

python3 - "$database" "$snapshot_file" <<'PY'
import json
import sqlite3
import sys

with open(sys.argv[2], encoding="utf-8") as source:
    expected = json.load(source)
with sqlite3.connect(sys.argv[1]) as connection:
    actual = {
        "posted": [
            list(record) for record in connection.execute(
                "SELECT * FROM CHECKINGACCOUNT_V1 ORDER BY TRANSID"
            )
        ],
        "scheduled": [
            list(record) for record in connection.execute(
                "SELECT * FROM BILLSDEPOSITS_V1 ORDER BY BDID"
            )
        ],
    }
    assert connection.execute("PRAGMA integrity_check").fetchone() == ("ok",)
if actual != expected:
    changes = {}
    for name, table, pk in (
        ("posted", "CHECKINGACCOUNT_V1", "TRANSID"),
        ("scheduled", "BILLSDEPOSITS_V1", "BDID"),
    ):
        if actual[name] == expected[name]:
            continue
        columns = [
            record[1] for record in
            connection.execute(f"PRAGMA table_info({table})")
        ]
        before = {row[0]: row for row in expected[name]}
        after = {row[0]: row for row in actual[name]}
        rows = {}
        for row_id in sorted(set(before) | set(after)):
            previous = before.get(row_id)
            current = after.get(row_id)
            if previous == current:
                continue
            rows[str(row_id)] = {
                column: {"before": (previous or [None] * len(columns))[index],
                         "after": (current or [None] * len(columns))[index]}
                for index, column in enumerate(columns)
                if previous is None or current is None
                or previous[index] != current[index]
            }
        changes[name] = rows
    diff_path = str(sys.argv[2]).replace("ledger-before.json", "ledger-diff.json")
    with open(diff_path, "w", encoding="utf-8") as output:
        json.dump(changes, output, indent=2, ensure_ascii=False)
    raise AssertionError(f"MMEX changed financial records: {changes}")
print("MMEX graphical launch preserved all posted and scheduled transactions")
PY
