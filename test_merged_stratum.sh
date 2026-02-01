#!/bin/bash
# Test script for WATTx merged mining stratum server

WATTXD="/home/nuts/Documents/WATTx/WATTx-0.1.7-dev/build/bin/wattxd"
WATTX_CLI="/home/nuts/Documents/WATTx/WATTx-0.1.7-dev/build/bin/wattx-cli"
DATADIR="$HOME/.wattx"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${YELLOW}=== WATTx Merged Mining Stratum Test ===${NC}"

# Check if wattxd is already running
if pgrep -x "wattxd" > /dev/null; then
    echo -e "${GREEN}wattxd is already running${NC}"
else
    echo -e "${YELLOW}Starting wattxd in regtest mode...${NC}"
    $WATTXD -regtest -daemon
    sleep 5
fi

# Check RPC connection
echo -e "${YELLOW}Checking RPC connection...${NC}"
$WATTX_CLI -regtest getblockchaininfo > /dev/null 2>&1
if [ $? -ne 0 ]; then
    echo -e "${RED}Failed to connect to wattxd RPC${NC}"
    exit 1
fi
echo -e "${GREEN}RPC connection OK${NC}"

# Get block count
BLOCKS=$($WATTX_CLI -regtest getblockcount)
echo "Current block height: $BLOCKS"

# If no blocks, mine some initial blocks
if [ "$BLOCKS" -lt 10 ]; then
    echo -e "${YELLOW}Mining initial blocks...${NC}"
    # Get a new address for mining
    MINING_ADDR=$($WATTX_CLI -regtest getnewaddress "mining" "legacy")
    echo "Mining address: $MINING_ADDR"

    # Mine 110 blocks (100 for coinbase maturity + 10 buffer)
    $WATTX_CLI -regtest generatetoaddress 110 "$MINING_ADDR"
    echo -e "${GREEN}Mined initial blocks${NC}"
fi

# Test addresses for merged mining
WATTX_WALLET=$($WATTX_CLI -regtest getnewaddress "merged_mining" "legacy")
echo "WATTx wallet for mining: $WATTX_WALLET"

# For testing, we'll use a fake Monero address (stagenet format)
MONERO_WALLET="54SQQmdS3e4C1x5bfLJDjSGLXt3cXhVBWRCyRTtHRhUaLqEJDK1CgvWNPvvAoUkWvP14dqNXLxo3eBdmVQsn2Gc7TGaUhrT"

echo ""
echo -e "${YELLOW}=== Starting Merged Stratum Server ===${NC}"
echo "Port: 3337"
echo "Monero daemon: 127.0.0.1:18081 (will fail without real Monero node)"
echo "Monero wallet: $MONERO_WALLET"
echo "WATTx wallet: $WATTX_WALLET"

# Start the merged stratum server
RESULT=$($WATTX_CLI -regtest startmergedstratum 3337 "127.0.0.1" 18081 "$MONERO_WALLET" "$WATTX_WALLET" 2>&1)
echo "Start result: $RESULT"

# Check server status
sleep 2
echo ""
echo -e "${YELLOW}=== Stratum Server Status ===${NC}"
$WATTX_CLI -regtest getmergedstratuminfo

# Test stratum connection
echo ""
echo -e "${YELLOW}=== Testing Stratum Protocol ===${NC}"
echo "Sending login request to stratum server..."

# Send a stratum login request using netcat
LOGIN_REQ='{"id":1,"jsonrpc":"2.0","method":"login","params":{"login":"test_worker","pass":"x","agent":"test/1.0"}}'

echo "$LOGIN_REQ" | timeout 5 nc -q 2 127.0.0.1 3337 2>/dev/null
NC_RESULT=$?

if [ $NC_RESULT -eq 0 ]; then
    echo -e "${GREEN}Stratum server responded${NC}"
else
    echo -e "${RED}Stratum server did not respond (exit code: $NC_RESULT)${NC}"
    echo "This may be expected if no Monero node is connected"
fi

echo ""
echo -e "${YELLOW}=== Test Complete ===${NC}"
echo "To stop the stratum server: wattx-cli -regtest stopmergedstratum"
echo "To stop wattxd: wattx-cli -regtest stop"
