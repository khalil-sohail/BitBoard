# Step 1: Navigate to the repo root
cd /home/ksohail/Documents/chess-engine

# Step 2: Compile the C++ engine (only needed once, or when engine code changes)
cd engine && make && cd ../website

# Step 3: Start the dev server with hot-reloading
npm run dev:server
# → Starts on http://localhost:3000 with ENGINE_PATH pointing to the
#   compiled binary one directory up.

# --- OR: do both in one command ---
npm run dev:all
