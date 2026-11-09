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
        "posted": connection.execute(
            "SELECT * FROM CHECKINGACCOUNT_V1 ORDER BY TRANSID"
        ).fetchall(),
        "scheduled": connection.execute(
            "SELECT * FROM BILLSDEPOSITS_V1 ORDER BY BDID"
        ).fetchall(),
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

sleep 8
wmctrl -lp > "$windows_file" || true
scrot "$output_dir/mmex-initial.png"

# A real application window must be visible, not merely a running process.
if ! kill -0 "$app_pid" 2>/dev/null || [[ -z "$(xdotool search --onlyvisible --pid "$app_pid" 2>/dev/null || true)" ]]; then
  echo "MMEX stopped or lost all graphical windows." >&2
  exit 1
fi

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
        "posted": connection.execute(
            "SELECT * FROM CHECKINGACCOUNT_V1 ORDER BY TRANSID"
        ).fetchall(),
        "scheduled": connection.execute(
            "SELECT * FROM BILLSDEPOSITS_V1 ORDER BY BDID"
        ).fetchall(),
    }
    assert connection.execute("PRAGMA integrity_check").fetchone() == ("ok",)
assert actual == expected, "MMEX graphical startup changed financial records"
print("MMEX graphical launch preserved all posted and scheduled transactions")
PY
