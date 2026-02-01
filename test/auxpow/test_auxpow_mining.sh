#!/bin/bash
#
# Test AuxPoW Merged Mining for WATTx
#
# This script sets up:
# 1. Mock Monero RPC server
# 2. WATTx regtest node
# 3. Starts the merged stratum server
# 4. Optionally connects XMRig for actual mining
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WATTX_DIR="/home/nuts/Documents/WATTx/WATTx-0.1.7-dev"
WATTX_BIN="$WATTX_DIR/build/bin"
WATTX_DATA="$SCRIPT_DIR/wattx_regtest_data"

# Set library path for RandomX and other shared libs
export LD_LIBRARY_PATH="$WATTX_DIR/build/src/randomx:$WATTX_DIR/build/src/crypto/x25x:$LD_LIBRARY_PATH"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}  WATTx AuxPoW Merged Mining Test${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""

# Check if binaries exist
if [ ! -f "$WATTX_BIN/wattxd" ]; then
    echo -e "${RED}Error: wattxd not found at $WATTX_BIN/wattxd${NC}"
    echo "Please build WATTx first: cd $WATTX_DIR/build && make -j\$(nproc)"
    exit 1
fi

# Create data directory
mkdir -p "$WATTX_DATA"

# Cleanup function
cleanup() {
    echo ""
    echo -e "${YELLOW}Cleaning up...${NC}"

    # Stop WATTx
    if [ -n "$WATTX_PID" ]; then
        echo "Stopping wattxd (PID: $WATTX_PID)..."
        kill $WATTX_PID 2>/dev/null || true
    fi

    # Stop mock Monero
    if [ -n "$MOCK_PID" ]; then
        echo "Stopping mock Monero RPC (PID: $MOCK_PID)..."
        kill $MOCK_PID 2>/dev/null || true
    fi

    echo -e "${GREEN}Done.${NC}"
}

trap cleanup EXIT

# Step 1: Start Mock Monero RPC
echo -e "${YELLOW}Step 1: Starting Mock Monero RPC server...${NC}"
python3 "$SCRIPT_DIR/mock_monero_rpc.py" &
MOCK_PID=$!
sleep 2

# Check if mock is running
if ! kill -0 $MOCK_PID 2>/dev/null; then
    echo -e "${RED}Error: Mock Monero RPC failed to start${NC}"
    exit 1
fi
echo -e "${GREEN}Mock Monero RPC running on port 18081 (PID: $MOCK_PID)${NC}"
echo ""

# Step 2: Start WATTx in regtest mode
echo -e "${YELLOW}Step 2: Starting WATTx regtest node...${NC}"

# Create regtest config
cat > "$WATTX_DATA/wattx.conf" << EOF
regtest=1
server=1
rpcuser=wattxtest
rpcpassword=testpass123
listen=0
listenonion=0
dnsseed=0

[regtest]
rpcport=14889
rpcallowip=127.0.0.1
EOF

"$WATTX_BIN/wattxd" -datadir="$WATTX_DATA" -daemon
WATTX_PID=$(pgrep -f "wattxd.*$WATTX_DATA" | head -1)
sleep 3

# Check if WATTx is running
if ! kill -0 $WATTX_PID 2>/dev/null; then
    echo -e "${RED}Error: wattxd failed to start${NC}"
    cat "$WATTX_DATA/regtest/debug.log" | tail -50
    exit 1
fi
echo -e "${GREEN}WATTx regtest running (PID: $WATTX_PID)${NC}"
echo ""

# Helper function for RPC calls
wattx_rpc() {
    "$WATTX_BIN/wattx-cli" -datadir="$WATTX_DATA" -rpcuser=wattxtest -rpcpassword=testpass123 "$@"
}

# Wait for RPC to be ready
echo "Waiting for RPC to be ready..."
for i in {1..30}; do
    if wattx_rpc getblockchaininfo &>/dev/null; then
        break
    fi
    sleep 1
done

# Step 3: Generate initial blocks and get an address
echo -e "${YELLOW}Step 3: Setting up wallet and generating initial blocks...${NC}"

# Create wallet if needed
wattx_rpc createwallet "test" 2>/dev/null || true

# Get a new address for mining rewards
WATTX_ADDR=$(wattx_rpc getnewaddress)
echo "WATTx mining address: $WATTX_ADDR"

# Generate some initial blocks
echo "Generating 10 initial blocks..."
wattx_rpc generatetoaddress 10 "$WATTX_ADDR" >/dev/null
echo ""

# Show blockchain info
echo -e "${YELLOW}Blockchain info:${NC}"
wattx_rpc getblockchaininfo | head -10
echo ""

# Step 4: Start merged stratum server
echo -e "${YELLOW}Step 4: Starting merged stratum server...${NC}"

# Use a fake Monero stagenet address (starts with 5 for stagenet)
MONERO_ADDR="5AkDpMBNLqsG6bJcYYnHQKjvpYqNAhSj9CXVR7cLFGmZ8Y3qH6JjMdZqT8qWvNiUfG9kLaP4xWrVbBmXuCdRn9eE2KpYt7Q"

# Start merged stratum using curl for proper JSON types
RESULT=$(curl -s --user wattxtest:testpass123 --data-binary "{\"jsonrpc\":\"1.0\",\"id\":\"test\",\"method\":\"startmergedstratum\",\"params\":[3337,\"127.0.0.1\",18081,\"$MONERO_ADDR\",\"$WATTX_ADDR\"]}" -H 'content-type: text/plain;' http://127.0.0.1:14889/ 2>&1)

# Check for errors
if echo "$RESULT" | grep -q '"error".*null'; then
    echo "$RESULT" | python3 -c "import sys,json; d=json.load(sys.stdin); print(json.dumps(d.get('result',{}), indent=2))"
elif echo "$RESULT" | grep -q '"error"'; then
    echo -e "${RED}Failed to start merged stratum:${NC}"
    echo "$RESULT" | python3 -c "import sys,json; d=json.load(sys.stdin); print(json.dumps(d.get('error',{}), indent=2))"
    echo ""
    echo "Checking debug log..."
    tail -50 "$WATTX_DATA/regtest/debug.log"
    exit 1
else
    echo -e "${RED}Failed to start merged stratum:${NC}"
    echo "$RESULT"
    exit 1
fi

echo "$RESULT"
echo ""

# Step 5: Show status
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}  Test Environment Ready!${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo "Components running:"
echo "  - Mock Monero RPC:    http://127.0.0.1:18081"
echo "  - WATTx Regtest:      RPC port 14889"
echo "  - Merged Stratum:     stratum+tcp://127.0.0.1:3337"
echo ""
echo "WATTx Address: $WATTX_ADDR"
echo ""
echo -e "${YELLOW}To test with XMRig:${NC}"
echo "  xmrig -o 127.0.0.1:3337 -u $WATTX_ADDR -p x --coin monero"
echo ""
echo -e "${YELLOW}To check stratum status:${NC}"
echo "  $WATTX_BIN/wattx-cli -datadir=$WATTX_DATA -rpcuser=wattxtest -rpcpassword=testpass123 getstratuminfo"
echo ""
echo -e "${YELLOW}To watch debug log:${NC}"
echo "  tail -f $WATTX_DATA/regtest/debug.log"
echo ""
echo "Press Ctrl+C to stop all services..."
echo ""

# Keep running and show log
tail -f "$WATTX_DATA/regtest/debug.log"
