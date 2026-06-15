#!/bin/bash

# Configuration
ENGINE="./../chess-engine --mode=gui"

ENGINE_CMD="./../chess-engine --mode=gui"

# Ensure the engine exists and is executable
if [[ ! -x "./../chess-engine" ]]; then
    echo "Error: Engine binary not found or not executable at ./../chess-engine"
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
coproc ENGINE_PROC { ./../chess-engine --mode=gui; }
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


# ==========================================
# Test 4 — Ponder hit after real thinking time
# ==========================================
echo -e "\n=== Test 4: Ponder hit after real thinking time ==="
send_cmd "ucinewgame"
send_cmd "position startpos"
send_cmd "go ponder wtime 60000 btime 60000"

echo "Waiting 3 seconds for engine to think..."
sleep 3
send_cmd "ponderhit"

if read_bestmove 5; then
    echo -e "\033[0;32mPASS: Test 4\033[0m"
else
    echo -e "\033[0;31mFAIL: Test 4\033[0m"
    exit 1
fi

# ==========================================
# Test 5 — Ucinewgame clears state
# ==========================================
echo -e "\n=== Test 5: Ucinewgame clears state ==="
# Play a short sequence
send_cmd "position startpos moves e2e4 e7e5 g1f3 b8c6"
send_cmd "go movetime 1000"
read_bestmove 3 > /dev/null || true # Ignore the result, just let it finish

send_cmd "ucinewgame"
send_cmd "position startpos"
send_cmd "go movetime 500"

if read_bestmove 3; then
    echo -e "\033[0;32mPASS: Test 5\033[0m"
else
    echo -e "\033[0;31mFAIL: Test 5\033[0m"
    exit 1
fi

# ==========================================
# Test 6 — Low time panic
# ==========================================
echo -e "\n=== Test 6: Low time panic ==="
send_cmd "ucinewgame"
send_cmd "position startpos"
send_cmd "go wtime 500 btime 500 winc 0 binc 0"

if read_bestmove 2; then
    echo -e "\033[0;32mPASS: Test 6\033[0m"
else
    echo -e "\033[0;31mFAIL: Test 6\033[0m"
    exit 1
fi

# ==========================================
# Test 7 — No legal moves position (stalemate)
# ==========================================
echo -e "\n=== Test 7: No legal moves position (stalemate) ==="
send_cmd "ucinewgame"
# A simple stalemate position where white to move has no moves
STALEMATE_FEN="8/8/8/8/8/5kq1/8/7K w - - 0 1"
send_cmd "position fen $STALEMATE_FEN"
send_cmd "go movetime 1000"

if read_bestmove 2; then
    echo -e "\033[0;32mPASS: Test 7\033[0m"
else
    echo -e "\033[0;31mFAIL: Test 7\033[0m"
    exit 1
fi

# ==========================================
# Test 8 — Rapid successive stop/start cycle
# ==========================================
echo -e "\n=== Test 8: Rapid successive stop/start cycle ==="
send_cmd "ucinewgame"

for i in 1 2 3; do
    echo "--- Cycle $i ---"
    send_cmd "position startpos"
    send_cmd "go movetime 5000"
    sleep 0.2
    send_cmd "stop"
    if ! read_bestmove 2; then
        echo -e "\033[0;31mFAIL: Test 8 Cycle $i (stop timeout)\033[0m"
        exit 1
    fi
    
    send_cmd "position startpos moves e2e4"
    send_cmd "go movetime 500"
    if ! read_bestmove 2; then
        echo -e "\033[0;31mFAIL: Test 8 Cycle $i (500ms timeout)\033[0m"
        exit 1
    fi
done
echo -e "\033[0;32mPASS: Test 8\033[0m"


# ==========================================
# Test 9 — Graceful silence after stop (session release simulation)
# ==========================================
echo -e "\n=== Test 9: Graceful silence after stop ==="
send_cmd "ucinewgame"
send_cmd "position startpos"
send_cmd "go movetime 5000"
sleep 0.2
send_cmd "stop"

if read_bestmove 2; then
    echo "Got bestmove, now waiting 5 seconds..."
else
    echo -e "\033[0;31mFAIL: Test 9 (did not respond to stop)\033[0m"
    exit 1
fi

sleep 5
send_cmd "isready"

# Helper to read readyok
read_readyok() {
    local timeout=$1
    local start_time=$(date +%s)
    
    while :; do
        if read -t 0.1 -u ${ENGINE_PROC[0]} line; then
            if [[ "$line" == "readyok" ]]; then
                echo ">> $line"
                return 0
            fi
        fi
        
        local current_time=$(date +%s)
        if (( current_time - start_time >= timeout )); then
            echo "Timeout waiting for readyok!"
            return 1
        fi
    done
}

if read_readyok 2; then
    echo -e "\033[0;32mPASS: Test 9\033[0m"
else
    echo -e "\033[0;31mFAIL: Test 9 (Engine unresponsive after silence)\033[0m"
    exit 1
fi

echo -e "\n\033[1;32mALL 9 TESTS PASSED SUCCESSFULLY!\033[0m"
exit 0
