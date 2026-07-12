#!/usr/bin/env bash
set -euo pipefail

ENGINE="${ENGINE:-./chess-engine}"

if [[ ! -x "$ENGINE" ]]; then
  echo "Engine binary not found or not executable: $ENGINE" >&2
  exit 1
fi

coproc UCI_ENGINE { "$ENGINE" --mode=gui; }
trap 'kill "$UCI_ENGINE_PID" 2>/dev/null || true' EXIT

send() {
  printf '%s\n' "$1" >&"${UCI_ENGINE[1]}"
}

wait_for() {
  local pattern="$1"
  local deadline=$((SECONDS + 8))
  while (( SECONDS < deadline )); do
    if IFS= read -r -t 0.1 line <&"${UCI_ENGINE[0]}"; then
      printf '%s\n' "$line"
      if [[ "$line" =~ $pattern ]]; then
        return 0
      fi
    fi
  done
  echo "Timed out waiting for: $pattern" >&2
  return 1
}

send "uci"
wait_for '^uciok$' >/dev/null
send "isready"
wait_for '^readyok$' >/dev/null

send "position startpos moves d2d4 g8f6"
send "go ponder movetime 1000"
send "ponderhit"

output="$(wait_for '^bestmove ')"
printf '%s\n' "$output"

send "isready"
wait_for '^readyok$' >/dev/null
send "quit"
