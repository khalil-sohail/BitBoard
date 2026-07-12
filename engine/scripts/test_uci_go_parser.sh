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

assert_no_bestmove_briefly() {
  local deadline=$((SECONDS + 1))
  while (( SECONDS < deadline )); do
    if IFS= read -r -t 0.05 line <&"${UCI_ENGINE[0]}"; then
      printf '%s\n' "$line"
      if [[ "$line" =~ ^bestmove\  ]]; then
        echo "Invalid go command started a search" >&2
        return 1
      fi
    fi
  done
}

assert_invalid_go() {
  send "$1"
  wait_for '^info string error invalid go command: ' >/dev/null
  assert_no_bestmove_briefly
  send "isready"
  wait_for '^readyok$' >/dev/null
}

send "uci"
wait_for '^uciok$' >/dev/null
send "isready"
wait_for '^readyok$' >/dev/null
send "setoption name OwnBook value false"

assert_invalid_go "go movetime -1"
assert_invalid_go "go movetime abc"
assert_invalid_go "go movetime 3.5"
assert_invalid_go "go movetime 1e3"
assert_invalid_go "go movetime 999999999999999999999"
assert_invalid_go "go depth 0"
assert_invalid_go "go depth -1"
assert_invalid_go "go depth abc"
assert_invalid_go "go wtime -1 btime 1000"
assert_invalid_go "go wtime 1000"
assert_invalid_go "go movetime 1000 depth 8"
assert_invalid_go "go infinite movetime 1000"
assert_invalid_go "go wtime 1000 btime 1000 depth 8"
assert_invalid_go "go wtime 1000 btime 1000 winc 1"
assert_invalid_go "go movetime"

send "position startpos moves d2d4 g8f6"
send "go movetime 25"
wait_for '^bestmove ' >/dev/null

send "go infinite"
sleep 0.05
send "stop"
wait_for '^bestmove ' >/dev/null

send "go ponder movetime 100"
send "ponderhit"
wait_for '^bestmove ' >/dev/null

send "quit"
