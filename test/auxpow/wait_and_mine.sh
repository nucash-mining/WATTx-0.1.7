#!/bin/bash
#
# Wait for Monero sync and start merged mining
#

MONERO_ADDR="4AsjKppNcHfJPekAPKVMsecyVT1v35MVn4N6dsXYSVTZHWsmC66u3sDT5NYavm5udMXHf32Ntb4N2bJqhnN4Gfq2GKZYmMK"
WATTX_ADDR="wV6T6taCUMQGrJofEzVsH95siehbhKsNRr"

export LD_LIBRARY_PATH="/home/nuts/Documents/WATTx/WATTx-0.1.7-dev/build/src/randomx:/home/nuts/Documents/WATTx/WATTx-0.1.7-dev/build/src/crypto/x25x:$LD_LIBRARY_PATH"

echo "========================================="
echo "  Waiting for Monero mainnet sync..."
echo "========================================="
echo ""

while true; do
    # Get current height
    RESULT=$(curl -s --max-time 5 http://127.0.0.1:18081/get_height 2>/dev/null)

    if [ -z "$RESULT" ]; then
        echo "Monero RPC not responding, waiting..."
        sleep 30
        continue
    fi

    HEIGHT=$(echo "$RESULT" | python3 -c "import sys,json; print(json.load(sys.stdin).get('height',0))" 2>/dev/null || echo "0")

    # Try to get block template
    TEMPLATE=$(curl -s --max-time 10 --data-binary "{\"jsonrpc\":\"2.0\",\"id\":\"0\",\"method\":\"get_block_template\",\"params\":{\"wallet_address\":\"$MONERO_ADDR\",\"reserve_size\":200}}" \
      -H 'content-type: application/json' \
      http://127.0.0.1:18081/json_rpc 2>/dev/null)

    if echo "$TEMPLATE" | grep -q "blocktemplate_blob"; then
        echo ""
        echo "========================================="
        echo "  SYNCED! Block templates available!"
        echo "========================================="
        echo ""
        break
    fi

    # Show progress
    echo -ne "\rHeight: $HEIGHT - Still syncing...   "
    sleep 30
done

# Start merged stratum
echo "Starting merged stratum server..."

# Stop any existing
curl -s --user wattxtest:testpass123 --data-binary '{"jsonrpc":"1.0","id":"test","method":"stopmergedstratum","params":[]}' \
  -H 'content-type: text/plain;' http://127.0.0.1:14889/ >/dev/null 2>&1

sleep 2

# Start with mainnet
RESULT=$(curl -s --user wattxtest:testpass123 \
  --data-binary "{\"jsonrpc\":\"1.0\",\"id\":\"test\",\"method\":\"startmergedstratum\",\"params\":[3337,\"127.0.0.1\",18081,\"$MONERO_ADDR\",\"$WATTX_ADDR\"]}" \
  -H 'content-type: text/plain;' \
  http://127.0.0.1:14889/)

if echo "$RESULT" | grep -q '"success":true'; then
    echo ""
    echo "========================================="
    echo "  MERGED MINING READY!"
    echo "========================================="
    echo ""
    echo "Stratum server: stratum+tcp://127.0.0.1:3337"
    echo ""
    echo "To start mining with XMRig:"
    echo "  xmrig -o 127.0.0.1:3337 -u $WATTX_ADDR -p x --coin monero"
    echo ""
    echo "Watching debug log..."
    echo ""
    tail -f /home/nuts/Documents/WATTx/WATTx-0.1.7-dev/test/auxpow/wattx_regtest_data/regtest/debug.log | grep -i "merged\|monero\|block\|share"
else
    echo "Failed to start merged stratum:"
    echo "$RESULT"
fi
