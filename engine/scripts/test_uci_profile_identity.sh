#!/usr/bin/env bash
set -euo pipefail

ENGINE="${ENGINE:-./chess-engine}"
EXPECTED_IDENTITY='info string tuning profile=builtin-default-v1 hash=sha256:55a1ac92352bd018460f115cb5061c76140f1eed453afc8a229ed3fa84145718 schema=1 model=phase-2-typed-model-v1'

if [[ ! -x "$ENGINE" ]]; then
  echo "Engine binary not found or not executable: $ENGINE" >&2
  exit 1
fi

BOOK_FIXTURE="$(mktemp)"
coproc UCI_ENGINE { "$ENGINE" --mode=gui --book="$BOOK_FIXTURE"; }
trap 'kill "$UCI_ENGINE_PID" 2>/dev/null || true; rm -f "$BOOK_FIXTURE"' EXIT

send() {
  printf '%s\n' "$1" >&"${UCI_ENGINE[1]}"
}

wait_for() {
  local pattern="$1"
  local deadline=$((SECONDS + 10))
  while (( SECONDS < deadline )); do
    if IFS= read -r -t 0.1 line <&"${UCI_ENGINE[0]}"; then
      if [[ "$line" =~ $pattern ]]; then
        return 0
      fi
    fi
  done
  echo "Timed out waiting for: $pattern" >&2
  return 1
}

read_handshake() {
  HANDSHAKE_OUTPUT=''
  local deadline=$((SECONDS + 10))
  while (( SECONDS < deadline )); do
    if IFS= read -r -t 0.1 line <&"${UCI_ENGINE[0]}"; then
      HANDSHAKE_OUTPUT+="$line"$'\n'
      if [[ "$line" == 'uciok' ]]; then
        return 0
      fi
    fi
  done
  echo "Timed out waiting for UCI handshake" >&2
  return 1
}

assert_handshake() {
  local output="$1"
  [[ "$(grep -c '^id name BitboardEngine$' <<<"$output")" -eq 1 ]]
  [[ "$(grep -c '^id author Khalil$' <<<"$output")" -eq 1 ]]
  [[ "$(grep -Fxc "$EXPECTED_IDENTITY" <<<"$output")" -eq 1 ]]
  [[ "$(grep -c '^uciok$' <<<"$output")" -eq 1 ]]
  [[ "$(grep -c '^option name ' <<<"$output")" -eq 9 ]]

  local identity_line uciok_line
  identity_line="$(grep -Fn "$EXPECTED_IDENTITY" <<<"$output" | cut -d: -f1)"
  uciok_line="$(grep -n '^uciok$' <<<"$output" | cut -d: -f1)"
  [[ "$identity_line" -lt "$uciok_line" ]]
}

send 'uci'
read_handshake
FIRST_HANDSHAKE="$HANDSHAKE_OUTPUT"
assert_handshake "$FIRST_HANDSHAKE"

send 'isready'
wait_for '^readyok$'
send 'setoption name OwnBook value false'
send 'position startpos'
send 'go depth 1'
wait_for '^bestmove '

send 'ucinewgame'
send 'position startpos'
send 'go infinite'
sleep 0.05
send 'stop'
wait_for '^bestmove '

send 'position startpos moves d2d4 g8f6'
send 'go ponder movetime 100'
send 'ponderhit'
wait_for '^bestmove '

send 'uci'
read_handshake
SECOND_HANDSHAKE="$HANDSHAKE_OUTPUT"
assert_handshake "$SECOND_HANDSHAKE"
[[ "$FIRST_HANDSHAKE" == "$SECOND_HANDSHAKE" ]]

send 'quit'
echo 'UCI compiled-profile identity tests passed'
