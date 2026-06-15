#!/bin/bash

# Configuration
ENGINE="./engine/chess-engine --mode=gui"

ENGINE_CMD="./engine/chess-engine --mode=gui"

# Ensure the engine exists and is executable
if [[ ! -x "./engine/chess-engine" ]]; then
    echo "Error: Engine binary not found or not executable at ./engine/chess-engine"
    exit 1
fi

# Cleanup function to kill the engine process
cleanup() {
    if [[ -n "$ENGINE_PID" ]]; then
        kill "$ENGINE_PID" 2>/dev/null
    fi
}
trap cleanup EXIT

# Start the engine as a coprocess
coproc ENGINE_PROC { ./engine/chess-engine --mode=gui; }
ENGINE_PID=$ENGINE_PROC_PID

# Helper to write to the engine with a small delay
send_cmd() {
    echo "<< $1"
    echo "$1" >&${ENGINE_PROC[1]}
    sleep 0.1
}

# Helper to read from the engine until "bestmove" is seen or timeout occurs
read_bestmove() {
    local timeout=$1
    local start_time=$(date +%s)
    
    while :; do
        # Read from engine with a 0.1s timeout
        if read -t 0.1 -u ${ENGINE_PROC[0]} line; then
            # Only print info depth or bestmove to keep output clean, but always echo bestmove
            if [[ "$line" == bestmove* ]]; then
                echo ">> $line"
                return 0
            elif [[ "$line" != info*nodes* ]] && [[ "$line" != info*string* ]]; then
                # Optional: echo ">> $line" for full debug
                true
            fi
        fi
        
        local current_time=$(date +%s)
        if (( current_time - start_time >= timeout )); then
            echo "Timeout waiting for bestmove!"
            return 1
        fi
    done
}

echo "Starting UCI tests..."
send_cmd "uci"
send_cmd "isready"

# Wait a brief moment for initialization
sleep 0.5


# ==========================================
# Test 1 — Normal move/response cycle
# ==========================================
echo -e "\n=== Test 1: Normal move/response cycle ==="
send_cmd "ucinewgame"
send_cmd "position startpos"
send_cmd "go movetime 1000"

if read_bestmove 3; then
    echo -e "\033[0;32mPASS: Test 1\033[0m"
else
    echo -e "\033[0;31mFAIL: Test 1\033[0m"
    exit 1
fi


# ==========================================
# Test 2 — Fast ponder exit (the critical one)
# ==========================================
echo -e "\n=== Test 2: Fast ponder exit ==="
# FEN where black is in the corner (h1) with the 2nd rank cut off by a white rook (a2)
# Black has exactly ONE legal move: Kg1.
FEN="8/8/8/8/8/8/R7/K6k b - - 0 1"

send_cmd "ucinewgame"
send_cmd "position fen $FEN"
send_cmd "go ponder wtime 60000 btime 60000"

# Wait 200ms. The engine will instantly find the only legal move and finish the search.
# With our recent fix, it should wait silently in the spin loop without printing bestmove.
sleep 0.2

# Now trigger the ponderhit
send_cmd "ponderhit"

if read_bestmove 3; then
    echo -e "\033[0;32mPASS: Test 2\033[0m"
else
    echo -e "\033[0;31mFAIL: Test 2 (engine hung or desynced!)\033[0m"
    exit 1
fi


# ==========================================
# Test 3 — Unexpected move during ponder
# ==========================================
echo -e "\n=== Test 3: Unexpected move during ponder (stop path) ==="
send_cmd "ucinewgame"
send_cmd "position startpos"
send_cmd "go ponder wtime 60000 btime 60000"

# Let it ponder for half a second
sleep 0.5

# Send stop (simulating the opponent playing a different move)
send_cmd "stop"

if read_bestmove 2; then
    echo -e "\033[0;32mPASS: Test 3a (responded to stop)\033[0m"
else
    echo -e "\033[0;31mFAIL: Test 3a (did not respond to stop)\033[0m"
    exit 1
fi

# Immediately start a new search
send_cmd "position startpos moves e2e4 e7e5"
send_cmd "go movetime 500"

if read_bestmove 2; then
    echo -e "\033[0;32mPASS: Test 3b (responded to new search)\033[0m"
else
    echo -e "\033[0;31mFAIL: Test 3b (new search failed)\033[0m"
    exit 1
fi


echo -e "\n\033[1;32mALL TESTS PASSED SUCCESSFULLY!\033[0m"
exit 0
