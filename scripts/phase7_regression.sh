#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Phase 7 regression harness (baseline vs candidate)

Usage:
  ./scripts/phase7_regression.sh \
    --baseline-bin ./engine-old \
    --candidate-bin ./chess-engine \
    [--suite bench/phase7_suite.txt] \
    [--default-depth 8] \
    [--movetime-ms 20000] \
    [--engine-args "--mode=gui --book=__NO_BOOK__"] \
    [--csv bench/phase7_results.csv]

Suite file format (pipe-delimited):
  name|position command|depth|expected

Examples:
  mate_in_1|position startpos moves e2e4 e7e5 f1c4 b8c6 d1h5 g8f6|3|h5f7
  horizon_case|position startpos moves e2e4 e7e5 g1f3 b8c6 f1c4 g8f6 f3g5 d7d5 e4d5 f6d5|3|!c4d5

Expected field rules:
  - empty: no tactical assertion
  - move (e.g., h5f7): bestmove must match exactly
  - !move (e.g., !c4d5): bestmove must NOT equal move
EOF
}

trim() {
    local s="$1"
    s="${s#"${s%%[![:space:]]*}"}"
    s="${s%"${s##*[![:space:]]}"}"
    printf '%s' "$s"
}

require_engine_bin() {
    local bin="$1"
    if [[ "$bin" == */* ]]; then
        if [[ ! -x "$bin" ]]; then
            echo "error: engine binary not executable: $bin" >&2
            exit 1
        fi
    else
        if ! command -v "$bin" >/dev/null 2>&1; then
            echo "error: engine binary not found in PATH: $bin" >&2
            exit 1
        fi
    fi
}

pct_change() {
    local base="$1"
    local cand="$2"
    awk -v b="$base" -v c="$cand" 'BEGIN { if (b == 0) { print "n/a"; } else { printf "%.2f%%", ((c - b) * 100.0) / b; } }'
}

BASELINE_BIN=""
CANDIDATE_BIN=""
SUITE_FILE="bench/phase7_suite.txt"
DEFAULT_DEPTH=8
MOVETIME_MS=20000
ENGINE_ARGS="--mode=gui --book=__NO_BOOK__"
CSV_OUT=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --baseline-bin)
            BASELINE_BIN="$2"
            shift 2
            ;;
        --candidate-bin)
            CANDIDATE_BIN="$2"
            shift 2
            ;;
        --suite)
            SUITE_FILE="$2"
            shift 2
            ;;
        --default-depth)
            DEFAULT_DEPTH="$2"
            shift 2
            ;;
        --movetime-ms)
            MOVETIME_MS="$2"
            shift 2
            ;;
        --engine-args)
            ENGINE_ARGS="$2"
            shift 2
            ;;
        --csv)
            CSV_OUT="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "error: unknown argument: $1" >&2
            usage
            exit 1
            ;;
    esac
done

if [[ -z "$BASELINE_BIN" || -z "$CANDIDATE_BIN" ]]; then
    echo "error: both --baseline-bin and --candidate-bin are required." >&2
    usage
    exit 1
fi

if [[ ! -f "$SUITE_FILE" ]]; then
    echo "error: suite file not found: $SUITE_FILE" >&2
    exit 1
fi

require_engine_bin "$BASELINE_BIN"
require_engine_bin "$CANDIDATE_BIN"

read -r -a ENGINE_ARGS_ARR <<< "$ENGINE_ARGS"

if [[ -z "$CSV_OUT" ]]; then
    timestamp=$(date +%Y%m%d_%H%M%S)
    CSV_OUT="bench/phase7_results_${timestamp}.csv"
fi

mkdir -p "$(dirname "$CSV_OUT")"

printf '%s\n' "case,depth,expected,baseline_bestmove,candidate_bestmove,baseline_pass,candidate_pass,baseline_elapsed_ms,candidate_elapsed_ms,baseline_nodes,candidate_nodes,baseline_qnodes,candidate_qnodes,baseline_delta,candidate_delta,baseline_depth_reached,candidate_depth_reached,baseline_engine_elapsed_ms,candidate_engine_elapsed_ms" > "$CSV_OUT"

run_case() {
    local engine_bin="$1"
    local position_cmd="$2"
    local depth="$3"
    local expected="$4"

    local start_ms end_ms elapsed_ms output
    start_ms=$(date +%s%3N)
    output=$(
        {
            printf 'uci\n'
            printf 'isready\n'
            printf '%s\n' "$position_cmd"
            printf 'go depth %s movetime %s\n' "$depth" "$MOVETIME_MS"
            printf 'quit\n'
        } | "$engine_bin" "${ENGINE_ARGS_ARR[@]}" 2>/dev/null
    )
    end_ms=$(date +%s%3N)
    elapsed_ms=$((end_ms - start_ms))

    local bestmove max_depth nodes qnodes delta engine_elapsed
    bestmove=$(printf '%s\n' "$output" | awk '/^bestmove /{print $2; exit}')
    if [[ -z "$bestmove" ]]; then
        bestmove="0000"
    fi

    max_depth=$(printf '%s\n' "$output" | awk '/^info depth /{d=$3} END{if(d=="") d=0; print d}')

    nodes=$(printf '%s\n' "$output" | sed -n 's/.*nodes:[[:space:]]*\([0-9][0-9]*\).*/\1/p' | tail -n 1)
    qnodes=$(printf '%s\n' "$output" | sed -n 's/.*qNodes:[[:space:]]*\([0-9][0-9]*\).*/\1/p' | tail -n 1)
    delta=$(printf '%s\n' "$output" | sed -n 's/.*deltaSkips:[[:space:]]*\([0-9][0-9]*\).*/\1/p' | tail -n 1)
    engine_elapsed=$(printf '%s\n' "$output" | sed -n 's/.*elapsedMs:[[:space:]]*\([0-9][0-9]*\).*/\1/p' | tail -n 1)

    [[ -z "$nodes" ]] && nodes=0
    [[ -z "$qnodes" ]] && qnodes=0
    [[ -z "$delta" ]] && delta=0
    [[ -z "$engine_elapsed" ]] && engine_elapsed=0

    local pass="NA"
    if [[ -n "$expected" ]]; then
        if [[ "$expected" == !* ]]; then
            local forbidden="${expected:1}"
            if [[ "$bestmove" != "$forbidden" ]]; then
                pass="PASS"
            else
                pass="FAIL"
            fi
        else
            if [[ "$bestmove" == "$expected" ]]; then
                pass="PASS"
            else
                pass="FAIL"
            fi
        fi
    fi

    printf '%s|%s|%s|%s|%s|%s|%s' "$bestmove" "$pass" "$elapsed_ms" "$nodes" "$qnodes" "$delta" "$max_depth"
    printf '|%s\n' "$engine_elapsed"
}

case_count=0
expected_count=0

base_pass=0
cand_pass=0

base_elapsed_sum=0
cand_elapsed_sum=0

base_nodes_sum=0
cand_nodes_sum=0
node_fallback_cases=0

base_qnodes_sum=0
cand_qnodes_sum=0

base_delta_sum=0
cand_delta_sum=0

printf '%-24s %-5s %-10s | %-44s | %-44s\n' "Case" "Depth" "Expected" "Baseline" "Candidate"
printf '%-24s %-5s %-10s | %-44s | %-44s\n' "------------------------" "-----" "----------" "--------------------------------------------" "--------------------------------------------"

while IFS='|' read -r raw_name raw_position raw_depth raw_expected; do
    line="$(trim "$raw_name")"
    if [[ -z "$line" || "$line" == \#* ]]; then
        continue
    fi

    case_name="$line"
    position_cmd="$(trim "$raw_position")"
    depth_raw="$(trim "${raw_depth:-}")"
    expected="$(trim "${raw_expected:-}")"

    if [[ -z "$position_cmd" || "$position_cmd" != position* ]]; then
        echo "error: invalid suite line for '$case_name' (position command must start with 'position')." >&2
        exit 1
    fi

    depth="$DEFAULT_DEPTH"
    if [[ -n "$depth_raw" ]]; then
        depth="$depth_raw"
    fi

    base_result="$(run_case "$BASELINE_BIN" "$position_cmd" "$depth" "$expected")"
    cand_result="$(run_case "$CANDIDATE_BIN" "$position_cmd" "$depth" "$expected")"

    IFS='|' read -r base_best base_result_flag base_elapsed base_nodes base_qnodes base_delta base_depth_reached base_engine_elapsed <<< "$base_result"
    IFS='|' read -r cand_best cand_result_flag cand_elapsed cand_nodes cand_qnodes cand_delta cand_depth_reached cand_engine_elapsed <<< "$cand_result"

    case_count=$((case_count + 1))

    base_elapsed_sum=$((base_elapsed_sum + base_elapsed))
    cand_elapsed_sum=$((cand_elapsed_sum + cand_elapsed))

    effective_base_nodes="$base_nodes"
    effective_cand_nodes="$cand_nodes"
    if (( base_nodes == 0 || cand_nodes == 0 )); then
        effective_base_nodes="$base_qnodes"
        effective_cand_nodes="$cand_qnodes"
        node_fallback_cases=$((node_fallback_cases + 1))
    fi

    base_nodes_sum=$((base_nodes_sum + effective_base_nodes))
    cand_nodes_sum=$((cand_nodes_sum + effective_cand_nodes))

    base_qnodes_sum=$((base_qnodes_sum + base_qnodes))
    cand_qnodes_sum=$((cand_qnodes_sum + cand_qnodes))

    base_delta_sum=$((base_delta_sum + base_delta))
    cand_delta_sum=$((cand_delta_sum + cand_delta))

    if [[ -n "$expected" ]]; then
        expected_count=$((expected_count + 1))
        if [[ "$base_result_flag" == "PASS" ]]; then
            base_pass=$((base_pass + 1))
        fi
        if [[ "$cand_result_flag" == "PASS" ]]; then
            cand_pass=$((cand_pass + 1))
        fi
    fi

    printf '%-24s %-5s %-10s | bm=%-6s p=%-4s t=%6sms n=%8s q=%8s | bm=%-6s p=%-4s t=%6sms n=%8s q=%8s\n' \
        "$case_name" "$depth" "${expected:--}" \
        "$base_best" "$base_result_flag" "$base_elapsed" "$base_nodes" "$base_qnodes" \
        "$cand_best" "$cand_result_flag" "$cand_elapsed" "$cand_nodes" "$cand_qnodes"

    printf '%s\n' "$case_name,$depth,$expected,$base_best,$cand_best,$base_result_flag,$cand_result_flag,$base_elapsed,$cand_elapsed,$base_nodes,$cand_nodes,$base_qnodes,$cand_qnodes,$base_delta,$cand_delta,$base_depth_reached,$cand_depth_reached,$base_engine_elapsed,$cand_engine_elapsed" >> "$CSV_OUT"
done < "$SUITE_FILE"

if (( case_count == 0 )); then
    echo "error: no runnable cases found in $SUITE_FILE" >&2
    exit 1
fi

base_elapsed_avg=$((base_elapsed_sum / case_count))
cand_elapsed_avg=$((cand_elapsed_sum / case_count))

node_growth="$(pct_change "$base_nodes_sum" "$cand_nodes_sum")"
qnode_growth="$(pct_change "$base_qnodes_sum" "$cand_qnodes_sum")"
time_growth="$(pct_change "$base_elapsed_sum" "$cand_elapsed_sum")"

node_growth_raw=$(awk -v b="$base_nodes_sum" -v c="$cand_nodes_sum" 'BEGIN { if (b == 0) print "nan"; else print ((c - b) * 100.0) / b; }')
node_budget_status="N/A"
if [[ "$node_growth_raw" != "nan" ]]; then
    if awk -v v="$node_growth_raw" 'BEGIN { exit !(v <= 15.0) }'; then
        node_budget_status="OK (<= 15%)"
    else
        node_budget_status="HIGH (> 15%)"
    fi
fi

echo
echo "Summary"
echo "- Cases run: $case_count"
if (( expected_count > 0 )); then
    echo "- Tactical reliability (expected cases): baseline $base_pass/$expected_count, candidate $cand_pass/$expected_count"
fi
echo "- Total elapsed: baseline ${base_elapsed_sum}ms, candidate ${cand_elapsed_sum}ms (change: ${time_growth})"
echo "- Avg elapsed/case: baseline ${base_elapsed_avg}ms, candidate ${cand_elapsed_avg}ms"
echo "- Total nodes (effective): baseline ${base_nodes_sum}, candidate ${cand_nodes_sum} (change: ${node_growth}, budget: ${node_budget_status})"
if (( node_fallback_cases > 0 )); then
    echo "- Node metric fallback used on ${node_fallback_cases} case(s): qNodes used when a total-nodes field was missing."
fi
echo "- Total qNodes: baseline ${base_qnodes_sum}, candidate ${cand_qnodes_sum} (change: ${qnode_growth})"
echo "- Total delta skips: baseline ${base_delta_sum}, candidate ${cand_delta_sum}"
echo "- CSV report: $CSV_OUT"
