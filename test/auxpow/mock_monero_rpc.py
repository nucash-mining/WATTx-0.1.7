#!/usr/bin/env python3
"""
Mock Monero RPC Server for testing WATTx AuxPoW merged mining.

Simulates the Monero daemon JSON-RPC interface with responses that
allow the merged stratum server to generate jobs and validate shares.
"""

import json
import hashlib
import time
import struct
from http.server import HTTPServer, BaseHTTPRequestHandler
from threading import Lock

# Mock state
class MoneroState:
    def __init__(self):
        self.height = 1000000
        self.difficulty = 100000  # Low difficulty for testing
        self.seed_hash = "0" * 64  # RandomX seed (zeros for testing)
        self.prev_hash = "1" * 64
        self.lock = Lock()
        self.submitted_blocks = []

    def get_block_template(self, wallet_address):
        """Generate a mock block template."""
        with self.lock:
            # Monero block template structure (simplified)
            # Real format: https://www.getmonero.org/resources/developer-guides/daemon-rpc.html

            timestamp = int(time.time())

            # Create a mock coinbase transaction
            # Format: version(1) + unlock_time(varint) + vin_count(1) + vin(gen) + vout_count(1) + vout + extra
            coinbase_hex = self._create_mock_coinbase(wallet_address, self.height)

            # Block hashing blob (76 bytes for RandomX):
            # major_version(1) + minor_version(1) + timestamp(varint) + prev_hash(32) + nonce(4) + tree_root(32)
            hashing_blob = self._create_hashing_blob(timestamp)

            # Full block template blob
            template_blob = self._create_template_blob(timestamp, coinbase_hex)

            return {
                "blocktemplate_blob": template_blob,
                "blockhashing_blob": hashing_blob,
                "difficulty": self.difficulty,
                "difficulty_top64": 0,
                "expected_reward": 600000000000,  # 0.6 XMR in atomic units
                "height": self.height,
                "prev_hash": self.prev_hash,
                "reserved_offset": 130,  # Offset for extra nonce in coinbase
                "seed_hash": self.seed_hash,
                "seed_height": self.height - (self.height % 2048),
                "status": "OK",
                "untrusted": False,
                "wide_difficulty": hex(self.difficulty)
            }

    def _create_mock_coinbase(self, wallet_address, height):
        """Create a simplified mock coinbase transaction."""
        # Version
        tx = "02"  # Version 2 (RingCT)

        # Unlock time (height + 60)
        tx += self._encode_varint(height + 60)

        # Input count (1 for coinbase)
        tx += "01"

        # Input type (0xff = coinbase/gen)
        tx += "ff"

        # Block height as varint
        tx += self._encode_varint(height)

        # Output count
        tx += "01"

        # Output amount (0 for RingCT)
        tx += "00"

        # Output target type (tagged key)
        tx += "02"

        # Fake output key (32 bytes)
        tx += "00" * 32

        # Extra field with space for merge mining tag
        extra_len = 64  # Leave room for WATTx merge mining tag
        tx += self._encode_varint(extra_len)

        # Extra content: TX_EXTRA_NONCE tag + length + reserved space
        tx += "02"  # TX_EXTRA_NONCE
        tx += self._encode_varint(extra_len - 2)
        tx += "00" * (extra_len - 3)  # Reserved space for merge mining

        # RingCT type (null/pruned for coinbase mock)
        tx += "00"

        return tx

    def _create_hashing_blob(self, timestamp):
        """Create the 76-byte block hashing blob."""
        blob = ""

        # Major version
        blob += "0e"  # Version 14

        # Minor version
        blob += "0e"

        # Timestamp as varint
        blob += self._encode_varint(timestamp)

        # Previous block hash (32 bytes)
        blob += self.prev_hash

        # Nonce placeholder (4 bytes, will be replaced by miner)
        blob += "00000000"

        # Merkle tree root placeholder (32 bytes)
        blob += hashlib.sha256(f"merkle_{self.height}".encode()).hexdigest()

        return blob

    def _create_template_blob(self, timestamp, coinbase_hex):
        """Create the full block template blob.

        Format:
        1. Block header (major_ver, minor_ver, timestamp, prev_hash, nonce)
        2. Coinbase transaction (directly follows header, no count prefix)
        3. Number of non-coinbase tx hashes (varint)
        4. tx_hashes (32 bytes each)
        """
        blob = ""

        # Block header
        blob += "0e"  # Major version (14)
        blob += "0e"  # Minor version (14)
        blob += self._encode_varint(timestamp)
        blob += self.prev_hash  # 32 bytes
        blob += "00000000"  # Nonce (4 bytes)

        # Coinbase transaction (directly follows header)
        blob += coinbase_hex

        # Number of non-coinbase transactions (0 for mock)
        blob += "00"

        return blob

    def _encode_varint(self, n):
        """Encode integer as Monero varint."""
        result = ""
        while n >= 0x80:
            result += format((n & 0x7f) | 0x80, '02x')
            n >>= 7
        result += format(n, '02x')
        return result

    def submit_block(self, block_blob):
        """Accept a submitted block (always succeeds in mock)."""
        with self.lock:
            self.submitted_blocks.append({
                "blob": block_blob,
                "height": self.height,
                "timestamp": int(time.time())
            })
            # Advance to next block
            self.prev_hash = hashlib.sha256(block_blob.encode()).hexdigest()
            self.height += 1
            print(f"[MOCK] Block submitted! New height: {self.height}")
            return {"status": "OK"}


state = MoneroState()


class MoneroRPCHandler(BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        print(f"[MOCK RPC] {args[0]}")

    def do_POST(self):
        content_length = int(self.headers.get('Content-Length', 0))
        body = self.rfile.read(content_length).decode('utf-8')

        try:
            request = json.loads(body)
        except json.JSONDecodeError:
            self.send_error(400, "Invalid JSON")
            return

        method = request.get("method", "")
        params = request.get("params", {})
        req_id = request.get("id", 0)

        print(f"[MOCK RPC] Method: {method}")

        response = {"jsonrpc": "2.0", "id": req_id}

        if method == "get_block_template":
            wallet = params.get("wallet_address", "")
            result = state.get_block_template(wallet)
            response["result"] = result

        elif method == "submit_block":
            block_blob = params[0] if isinstance(params, list) else params.get("blob", "")
            result = state.submit_block(block_blob)
            response["result"] = result

        elif method == "get_info":
            response["result"] = {
                "height": state.height,
                "difficulty": state.difficulty,
                "status": "OK",
                "testnet": True,
                "stagenet": True,
            }

        elif method == "get_height":
            response["result"] = {
                "height": state.height,
                "status": "OK"
            }

        else:
            response["error"] = {
                "code": -32601,
                "message": f"Method not found: {method}"
            }

        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        self.wfile.write(json.dumps(response).encode())


def main():
    port = 18081  # Default Monero RPC port
    server = HTTPServer(("0.0.0.0", port), MoneroRPCHandler)
    print(f"=" * 60)
    print(f"Mock Monero RPC Server")
    print(f"=" * 60)
    print(f"Listening on port {port}")
    print(f"Initial height: {state.height}")
    print(f"Difficulty: {state.difficulty}")
    print(f"")
    print(f"Endpoints:")
    print(f"  POST /json_rpc - JSON-RPC interface")
    print(f"")
    print(f"Supported methods:")
    print(f"  - get_block_template")
    print(f"  - submit_block")
    print(f"  - get_info")
    print(f"  - get_height")
    print(f"=" * 60)

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down...")
        server.shutdown()


if __name__ == "__main__":
    main()
