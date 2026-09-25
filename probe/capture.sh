#!/usr/bin/env bash
set -euo pipefail
label=$1
rows=${2:-30}
root=$(cd "$(dirname "$0")/.." && pwd)
main=/home/diejor/projects/networked
capture=$main/extension/thirdparty/tracy/capture/build-release/tracy-capture
out=$root/tmp/spawn
mkdir -p "$out"
cd "$root"
PLAYGROUND_ROWS=$rows PROBE_WARMUP_MS=5000 PROBE_LINGER_MS=${PROBE_LINGER_MS:-0} timeout 120 \
    godot --headless --path . --script res://probe/run.gd \
    > "$out/$label.log" 2>&1 &
before=$(ss -ltn | grep -oE '127.0.0.1:80[89][0-9]' | sort)
port=
for _ in $(seq 40); do
    sleep 0.1
    now=$(ss -ltn | grep -oE '127.0.0.1:80[89][0-9]' | sort)
    port=$(comm -13 <(echo "$before") <(echo "$now") | head -1 | cut -d: -f2)
    [ -n "$port" ] && break
done
timeout 100 "$capture" -f -o "$out/$label.tracy" -a 127.0.0.1 -p "$port" \
    > "$out/$label.capture.log" 2>&1
wait
grep PROBE "$out/$label.log"
"$main/tmp/playground-trace/inspect" "$out/$label.tracy" "$out/$label.json"
python3 "$main/.agents/design/evidence/playground-spawn/stages.py" \
    "$out/$label.json" | tee "$out/$label.stages.txt"
