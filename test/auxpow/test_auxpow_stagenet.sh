#!/bin/bash
#
# Test AuxPoW Merged Mining with Real Monero Stagenet
#
# This script sets up:
# 1. Monero stagenet node
# 2. WATTx regtest node
# 3. Merged stratum server connected to real Monero stagenet
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WATTX_DIR="/home/nuts/Documents/WATTx/WATTx-0.1.7-dev"
WATTX_BIN="$WATTX_DIR/build/bin"
WATTX_DATA="$SCRIPT_DIR/wattx_regtest_data"
MONERO_DIR="/home/nuts/Downloads/monero_gui/monero-gui-v0.18.4.2"
MONERO_DATA="$HOME/.bitmonero-stagenet"

# Set library path for RandomX
export LD_LIBRARY_PATH="$WATTX_DIR/build/src/randomx:$WATTX_DIR/build/src/crypto/x25x:$LD_LIBRARY_PATH"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}  WATTx AuxPoW Test - Monero Stagenet${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""

# Cleanup function
cleanup() {
    echo ""
    echo -e "${YELLOW}Cleaning up...${NC}"

    if [ -n "$WATTX_PID" ]; then
        echo "Stopping wattxd (PID: $WATTX_PID)..."
        kill $WATTX_PID 2>/dev/null || true
    fi

    # Don't stop monerod - let user keep it running for future tests
    echo -e "${CYAN}Note: Monero stagenet daemon left running for future use${NC}"
    echo -e "${CYAN}To stop it: pkill -f 'monerod.*stagenet'${NC}"

    echo -e "${GREEN}Done.${NC}"
}

trap cleanup EXIT

# Step 1: Check/Start Monero Stagenet
echo -e "${YELLOW}Step 1: Checking Monero stagenet daemon...${NC}"

if pgrep -f "monerod.*stagenet" > /dev/null; then
    echo -e "${GREEN}Monero stagenet already running${NC}"
else
    echo "Starting Monero stagenet daemon..."
    echo "This may take a while to sync initially (~5-10 GB download)"
    echo ""

    mkdir -p "$MONERO_DATA"

    "$MONERO_DIR/monerod" \
        --stagenet \
        --data-dir "$MONERO_DATA" \
        --rpc-bind-ip 127.0.0.1 \
        --rpc-bind-port 38081 \
        --confirm-external-bind \
        --non-interactive \
        --detach

    echo "Waiting for Monero daemon to start..."
    sleep 5
fi

# Check if Monero RPC is responding
echo "Checking Monero RPC..."
for i in {1..30}; do
    if curl -s --data-binary '{"jsonrpc":"2.0","id":"0","method":"get_info"}' \
         -H 'content-type: application/json' \
         http://127.0.0.1:38081/json_rpc 2>/dev/null | grep -q '"status":"OK"'; then
        MONERO_INFO=$(curl -s --data-binary '{"jsonrpc":"2.0","id":"0","method":"get_info"}' \
                      -H 'content-type: application/json' \
                      http://127.0.0.1:38081/json_rpc)
        MONERO_HEIGHT=$(echo "$MONERO_INFO" | python3 -c "import sys,json; print(json.load(sys.stdin)['result']['height'])" 2>/dev/null || echo "?")
        MONERO_SYNC=$(echo "$MONERO_INFO" | python3 -c "import sys,json; r=json.load(sys.stdin)['result']; print('synced' if r.get('synchronized',False) else 'syncing')" 2>/dev/null || echo "unknown")
        echo -e "${GREEN}Monero stagenet running - Height: $MONERO_HEIGHT ($MONERO_SYNC)${NC}"
        break
    fi
    sleep 2
done

if [ "$i" -eq 30 ]; then
    echo -e "${RED}Error: Monero daemon not responding${NC}"
    exit 1
fi
echo ""

# Step 2: Get or prompt for stagenet wallet address
echo -e "${YELLOW}Step 2: Stagenet wallet address...${NC}"

# Check if user has a stagenet wallet
if [ -f "$HOME/.monero-stagenet-address" ]; then
    MONERO_ADDR=$(cat "$HOME/.monero-stagenet-address")
    echo "Using saved stagenet address: ${MONERO_ADDR:0:20}..."
else
    echo ""
    echo -e "${CYAN}You need a Monero stagenet wallet address.${NC}"
    echo ""
    echo "Options:"
    echo "1. Open Monero GUI Wallet -> Select 'Stagenet' network"
    echo "2. Create/restore a wallet"
    echo "3. Copy your stagenet address (starts with '5' for stagenet)"
    echo ""
    echo "Stagenet faucets (to get test XMR):"
    echo "  - https://stagenet-faucet.xmr-tw.org/"
    echo ""
    read -p "Enter your Monero STAGENET address (starts with 5): " MONERO_ADDR

    if [[ ! "$MONERO_ADDR" =~ ^5 ]]; then
        echo -e "${RED}Warning: Stagenet addresses should start with '5'${NC}"
        read -p "Continue anyway? (y/n): " CONFIRM
        if [ "$CONFIRM" != "y" ]; then
            exit 1
        fi
    fi

    # Save for future use
    echo "$MONERO_ADDR" > "$HOME/.monero-stagenet-address"
    echo "Address saved to ~/.monero-stagenet-address"
fi
echo ""

# Step 3: Start WATTx regtest
echo -e "${YELLOW}Step 3: Starting WATTx regtest node...${NC}"

mkdir -p "$WATTX_DATA"

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
sleep 3
WATTX_PID=$(pgrep -f "wattxd.*$WATTX_DATA" | head -1)

if ! kill -0 $WATTX_PID 2>/dev/null; then
    echo -e "${RED}Error: wattxd failed to start${NC}"
    exit 1
fi
echo -e "${GREEN}WATTx regtest running (PID: $WATTX_PID)${NC}"
echo ""

# Helper function
wattx_rpc() {
    "$WATTX_BIN/wattx-cli" -datadir="$WATTX_DATA" -rpcuser=wattxtest -rpcpassword=testpass123 "$@"
}

# Wait for RPC
echo "Waiting for WATTx RPC..."
for i in {1..30}; do
    if wattx_rpc getblockchaininfo &>/dev/null; then
        break
    fi
    sleep 1
done

# Step 4: Setup WATTx wallet
echo -e "${YELLOW}Step 4: Setting up WATTx wallet...${NC}"

wattx_rpc createwallet "test" 2>/dev/null || wattx_rpc -rpcwallet=test getwalletinfo >/dev/null

WATTX_ADDR=$(wattx_rpc -rpcwallet=test getnewaddress)
echo "WATTx mining address: $WATTX_ADDR"

echo "Generating 10 initial blocks..."
wattx_rpc -rpcwallet=test generatetoaddress 10 "$WATTX_ADDR" >/dev/null
echo ""

# Step 5: Start merged stratum
echo -e "${YELLOW}Step 5: Starting merged stratum server...${NC}"

# Use curl for proper JSON types
RESULT=$(curl -s --user wattxtest:testpass123 --data-binary "{\"jsonrpc\":\"1.0\",\"id\":\"test\",\"method\":\"startmergedstratum\",\"params\":[3337,\"127.0.0.1\",38081,\"$MONERO_ADDR\",\"$WATTX_ADDR\"]}" -H 'content-type: text/plain;' http://127.0.0.1:14889/)

if echo "$RESULT" | grep -q '"success":true'; then
    echo -e "${GREEN}Merged stratum server started on port 3337${NC}"
else
    echo -e "${RED}Failed to start merged stratum:${NC}"
    echo "$RESULT"
    echo ""
    echo "Debug log:"
    tail -30 "$WATTX_DATA/regtest/debug.log"
    exit 1
fi
echo ""

# Step 6: Show status
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}  Test Environment Ready!${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo "Components running:"
echo "  - Monero Stagenet:    http://127.0.0.1:38081 (height: $MONERO_HEIGHT)"
echo "  - WATTx Regtest:      RPC port 14889"
echo "  - Merged Stratum:     stratum+tcp://127.0.0.1:3337"
echo ""
echo "Addresses:"
echo "  - Monero (stagenet): ${MONERO_ADDR:0:30}..."
echo "  - WATTx (regtest):   $WATTX_ADDR"
echo ""
echo -e "${YELLOW}To mine with XMRig:${NC}"
echo "  xmrig -o 127.0.0.1:3337 -u $WATTX_ADDR -p x --coin monero"
echo ""
echo -e "${YELLOW}To check status:${NC}"
echo "  # WATTx blocks:"
echo "  $WATTX_BIN/wattx-cli -datadir=$WATTX_DATA -rpcuser=wattxtest -rpcpassword=testpass123 -rpcwallet=test getblockcount"
echo ""
echo "  # Monero sync status:"
echo "  curl -s --data-binary '{\"jsonrpc\":\"2.0\",\"id\":\"0\",\"method\":\"get_info\"}' http://127.0.0.1:38081/json_rpc | python3 -m json.tool"
echo ""
echo -e "${CYAN}Note: Monero must be synced to receive valid block templates.${NC}"
echo -e "${CYAN}Stagenet sync can take 10-30 minutes on first run.${NC}"
echo ""
echo "Press Ctrl+C to stop WATTx (Monero will keep running)..."
echo ""

# Keep running and show log
tail -f "$WATTX_DATA/regtest/debug.log"
