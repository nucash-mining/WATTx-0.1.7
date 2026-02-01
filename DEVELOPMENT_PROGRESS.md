# WATTx Development Progress

## Version: 0.1.7-dev

### Completed Features

---

## 1. X25X Multi-Algorithm Mining Framework

**Status:** ✅ Implemented and Tested

### Overview
Implemented a multi-algorithm Proof-of-Work system that allows miners to submit proofs using various algorithms. This enables merged mining with other cryptocurrencies and provides flexibility for miners with different hardware.

### Supported Algorithms
| Algorithm | ID | Target Use Case |
|-----------|-----|-----------------|
| SHA256d   | 0   | Bitcoin ASIC miners |
| Scrypt    | 1   | Litecoin miners |
| Ethash    | 2   | Ethereum classic miners |
| RandomX   | 3   | Monero CPU miners |
| Equihash  | 4   | Zcash miners |
| X11       | 5   | Dash miners |
| kHeavyHash| 6   | Kaspa miners |

### Key Files
- `src/crypto/x25x/x25x.h` - Algorithm definitions and interface
- `src/crypto/x25x/x25x.cpp` - Implementation of mining algorithms
- `src/crypto/x25x/kheavyhash.h` - Kaspa's kHeavyHash implementation
- `src/validation.cpp` - Block validation with X25X support

### Activation Heights
| Network | X25X Activation Height |
|---------|------------------------|
| Mainnet | 100,000 |
| Testnet | 1,000 |
| Regtest | 1 |

### Testing Results
```
Block 0 (genesis): RandomX validation (pre-X25X)
Block 1+: X25X multi-algorithm validation
Successfully mined 806+ blocks in regtest
```

---

## 2. Dynamic Stake Maturity System

**Status:** ✅ Implemented and Tested

### Overview
Implemented a dynamic stake maturity system where the number of confirmations required for coins to be eligible for staking halves with each block reward halving. This creates easier PoS entry as PoW rewards decrease.

### Economics
- Base stake maturity: 500 blocks (mainnet), 20 blocks (regtest for testing)
- Halves with each reward halving era
- Minimum floor: 10 blocks (mainnet), 2 blocks (regtest)

### Implementation Details

**Consensus Parameters (`src/consensus/params.h`):**
```cpp
/** Base minimum confirmations for coins to be eligible for staking */
int nStakeMinConfirmations{500};

/** Minimum stake confirmations floor (won't go below this) */
int nMinStakeConfirmationsFloor{10};

/** Get stake min confirmations at a given height */
int StakeMinConfirmations(int height) const {
    // Calculates halvings based on SubsidyHalvingWeight
    // Halves stake maturity for each halving era
    // Enforces minimum floor
}
```

### Testing Results (Regtest)
| Era | Block Range | Stake Maturity |
|-----|-------------|----------------|
| 0   | 1-199       | 20 blocks      |
| 1   | 200-399     | 10 blocks      |
| 2   | 400-599     | 5 blocks       |
| 3+  | 600+        | 2 blocks (floor) |

### RPC Output (`getstakinginfo`)
```json
{
  "stakematurity": 2,
  "basestakematurity": 20,
  "stakematurityera": 3,
  ...
}
```

---

## 3. Hard Fork Activation System

**Status:** ✅ Implemented

### Overview
Implemented height-based hard fork activation for X25X mining. Pre-fork blocks use RandomX validation, post-fork blocks use X25X multi-algorithm validation.

### Key Changes
- `CheckHeaderPoWAtHeight()` - Height-aware PoW validation
- `CheckBlockHeader()` - Modified to determine height from previous block
- `CheckIndexProof()` - Updated to use height-aware validation
- `GenerateBlock()` - RPC mining is X25X-aware

---

## 4. WATTx Block Explorer

**Status:** ✅ Implemented and Running

### Overview
Custom Etherscan-like block explorer with full blockchain browsing, contract verification, and token tracking.

### Components
- **Backend API** (Node.js/Express): `/home/nuts/Documents/WATTx/wattx-explorer/backend`
- **Frontend** (React/Vite): `/home/nuts/Documents/WATTx/wattx-explorer/frontend`
- **Database**: SQLite with automatic indexing

### Features
- Block/Transaction/Address browsing
- Smart contract verification
- Contract read/write interaction
- Token detection and tracking (QRC-20/ERC-20)
- Real-time blockchain indexing
- Search by block, tx hash, or address
- PoW/PoS block type display

### API Endpoints
| Endpoint | Description |
|----------|-------------|
| `GET /api/blocks` | List recent blocks |
| `GET /api/block/:id` | Get block by height or hash |
| `GET /api/tx/:hash` | Get transaction details |
| `GET /api/address/:addr` | Get address info and balances |
| `POST /api/contract/verify` | Verify contract source |
| `POST /api/contract/:addr/read` | Call contract view function |
| `GET /api/token/:addr` | Get token info |
| `GET /api/chain` | Get chain statistics |

### Running the Explorer
```bash
# Backend (port 3001)
cd wattx-explorer/backend && npm start

# Frontend (port 5173)
cd wattx-explorer/frontend && npm run dev
```

---

## 5. Qt Wallet

**Status:** ✅ Built and Functional

### Build Configuration
```bash
cmake -DBUILD_GUI=ON .
make -j16
```

### Binary Location
`build/bin/wattx-qt`

---

## Build Configuration

### Prerequisites
- CMake 3.18+
- Qt5 (for GUI wallet)
- Berkeley DB 4.8 (for wallet)
- Boost libraries
- OpenSSL

### Build Commands
```bash
cd build
cmake -DBUILD_GUI=ON ..
make -j$(nproc)
```

### Binaries Produced
- `bin/wattxd` - Daemon
- `bin/wattx-cli` - Command-line interface
- `bin/wattx-qt` - Qt GUI wallet
- `bin/wattx-wallet` - Wallet utility
- `bin/wattx-tx` - Transaction utility

---

## Testing

### Regtest Quick Test
```bash
# Start daemon
./bin/wattxd -regtest -datadir=/tmp/wattx-test -daemon

# Create wallet
./bin/wattx-cli -regtest -datadir=/tmp/wattx-test createwallet "test"

# Get address and mine blocks
./bin/wattx-cli -regtest -datadir=/tmp/wattx-test -rpcwallet=test getnewaddress
./bin/wattx-cli -regtest -datadir=/tmp/wattx-test generatetoaddress 200 <address>

# Check staking info
./bin/wattx-cli -regtest -datadir=/tmp/wattx-test getstakinginfo
```

---

## Consensus Parameters Summary (Mainnet)

| Parameter | Value | Description |
|-----------|-------|-------------|
| Block Time | 2 minutes | Target spacing between blocks |
| Block Reward | 5 WTX (PoW) + 5 WTX (PoS) | 50 WTX per 10 minutes total |
| Halving Interval | 1,051,200 blocks | ~4 years |
| Base Stake Maturity | 500 blocks | Confirmations for staking eligibility |
| Stake Maturity Floor | 10 blocks | Minimum after all halvings |
| Min Validator Stake | 100,000 WTX | For super staker functionality |
| X25X Activation | Block 100,000 | Multi-algorithm mining activation |

---

## 6. LayerZero Cross-Chain Bridge

**Status:** ✅ Contracts Compiled

### Overview
LayerZero-based cross-chain bridge for WATTx tokens between WATTx chain and Ethereum (Sepolia testnet).

### Project Location
`/home/nuts/Documents/WATTx/wattx-bridge`

### Contracts
| Contract | Purpose | Deploy Chain |
|----------|---------|--------------|
| WATTxOFT | Native OFT token (mint/burn) | WATTx |
| WATTxOFTAdapter | Wraps existing ERC20 (lock/unlock) | Ethereum |
| TestWATTx | Test ERC20 for Sepolia testing | Ethereum |

### LayerZero Endpoint IDs
| Network | EID |
|---------|-----|
| WATTx | 40889 |
| Sepolia | 40161 |
| Ethereum Mainnet | 30101 |

### Architecture
```
Ethereum (Sepolia)                    WATTx Chain
┌─────────────────────┐              ┌─────────────────────┐
│    TestWATTx        │              │                     │
│    (ERC20)          │              │                     │
│         │           │              │                     │
│         ▼           │              │                     │
│  WATTxOFTAdapter    │◄────────────►│     WATTxOFT        │
│  (locks tokens)     │  LayerZero   │  (mints/burns)      │
└─────────────────────┘              └─────────────────────┘
```

### Deployment Scripts
- `scripts/deploy-sepolia.js` - Deploy to Ethereum Sepolia
- `scripts/deploy-wattx.js` - Deploy to WATTx chain
- `scripts/configure-peers.js` - Set up cross-chain peers
- `scripts/bridge-tokens.js` - Bridge tokens between chains

### Commands
```bash
# Compile contracts
npx hardhat compile

# Deploy to Sepolia
npx hardhat run scripts/deploy-sepolia.js --network sepolia

# Deploy to WATTx
npx hardhat run scripts/deploy-wattx.js --network wattx_regtest

# Configure peers (run on both networks)
npx hardhat run scripts/configure-peers.js --network sepolia
npx hardhat run scripts/configure-peers.js --network wattx_regtest

# Bridge tokens
npx hardhat run scripts/bridge-tokens.js --network sepolia
```

### Next Steps for Bridge
1. Deploy LayerZero endpoint on WATTx testnet
2. Deploy contracts to Sepolia
3. Test cross-chain token transfers
4. Integrate with block explorer

---

## Next Steps

1. **LayerZero Bridge Deployment**
   - Get Sepolia testnet ETH for deployment
   - Deploy contracts to Sepolia
   - Deploy LayerZero endpoint on WATTx
   - Test cross-chain transfers

2. **Mainnet Launch Preparation**
   - Finalize genesis block parameters
   - Set up seed nodes
   - Complete security audit

3. **Trust Tier System**
   - Implement validator heartbeat mechanism
   - Add uptime tracking
   - Create reward multiplier logic

4. **Merged Mining (AuxPoW)**
   - Complete Monero merged mining integration
   - Test cross-chain validation

5. **EVM Integration**
   - Test smart contract deployment via explorer
   - Verify EVM compatibility

---

## Version History

- **0.1.7-dev** - X25X multi-algorithm mining, dynamic stake maturity
- **0.1.6** - Initial WATTx fork from Qtum

---

*Last Updated: January 25, 2026*
