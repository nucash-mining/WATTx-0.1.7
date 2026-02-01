# WATTx-Monero Merged Mining Specification

## Overview

This document specifies the merged mining protocol between WATTx and Monero, enabling miners to secure both networks with a single RandomX computation and earn dual rewards (XMR + WTX).

## Architecture

```
                    ┌─────────────────────────────────┐
                    │         XMRig Miner             │
                    │    (Standard Monero Mining)     │
                    └───────────────┬─────────────────┘
                                    │
                    ┌───────────────▼─────────────────┐
                    │      Merged Mining Pool         │
                    │  - Creates combined templates   │
                    │  - Validates dual shares        │
                    │  - Distributes rewards          │
                    └───────────────┬─────────────────┘
                                    │
              ┌─────────────────────┼─────────────────────┐
              │                     │                     │
              ▼                     ▼                     ▼
    ┌─────────────────┐   ┌─────────────────┐   ┌─────────────────┐
    │  Monero Node    │   │   WATTx Node    │   │  Privacy Bridge │
    │  (XMR Rewards)  │   │  (WTX Rewards)  │   │  (Cross-chain)  │
    └─────────────────┘   └─────────────────┘   └─────────────────┘
```

## Phase 1: Merged Mining Protocol

### 1.1 Block Structure

**WATTx AuxPoW Block Header:**
```cpp
struct CAuxPowHeader {
    // Standard WATTx header fields
    int32_t nVersion;
    uint256 hashPrevBlock;
    uint256 hashMerkleRoot;
    uint32_t nTime;
    uint32_t nBits;
    uint32_t nNonce;

    // AuxPoW fields (for merged mining)
    bool fAuxPow;                    // True if this is a merged-mined block
    CMoneroBlockHeader parentBlock;  // Monero parent block header
    CMerkleBranch coinbaseBranch;    // Merkle path from coinbase to merkle root
    CMerkleBranch blockchainBranch;  // Merkle path for aux chain
    CTransaction coinbaseTx;         // Monero coinbase containing WATTx hash
};
```

### 1.2 Monero Coinbase Extra Field

The Monero coinbase transaction's "extra" field contains:
```
[TX_EXTRA_MERGE_MINING_TAG (1 byte)] [depth (1 byte)] [merkle_root (32 bytes)]
```

Where:
- `TX_EXTRA_MERGE_MINING_TAG` = 0x03
- `depth` = Position in merkle tree (0 for single aux chain)
- `merkle_root` = Hash of WATTx block header

### 1.3 Validation Rules

For a merged-mined WATTx block to be valid:
1. Parent Monero block header must hash to meet WATTx difficulty target
2. Coinbase transaction must be valid and in the parent block
3. Coinbase extra field must contain correct WATTx block hash
4. Merkle branch must prove coinbase inclusion in parent block
5. All standard WATTx block rules must be satisfied

### 1.4 Difficulty Adjustment

- WATTx maintains independent difficulty from Monero
- Merged-mined blocks use parent block hash for PoW validation
- Non-merged blocks (standalone) still supported for transition period

## Phase 2: Consensus Changes

### 2.1 Block Version

- Version 0x20000000: Standard PoW (legacy)
- Version 0x20010000: AuxPoW merged mining enabled

### 2.2 Fork Activation

- Activation height: [TO BE DETERMINED]
- After activation, both merged and standalone blocks accepted
- Gradual migration to merged mining

### 2.3 Reward Structure

```
WATTx Block Reward: 50 WTX
├── Miner (Merged Mining): 25 WTX (50%)
├── Staking Nodes: 12.5 WTX (25%)
├── Treasury: 5 WTX (10%)
└── Governance: 7.5 WTX (15%)
```

## Phase 3: Privacy Bridge

### 3.1 Transaction Batching

WATTx transactions are batched and committed to a merkle root:
```
WATTx Txns: [tx1, tx2, tx3, ... txN]
              │
              ▼
        Batch Merkle Root
              │
              ▼
    Committed to Monero (via mining)
```

### 3.2 Cross-Chain Atomic Swaps

Using Hash Time-Locked Contracts (HTLCs):
1. User locks WTX on WATTx with hash lock
2. User locks XMR on Monero with same hash lock
3. Reveal secret to claim both sides
4. Timeout returns funds if incomplete

### 3.3 Privacy Layer

- WATTx smart contracts generate swap intents
- Intents are anonymized through Monero's ring signatures
- Confirmations propagate back to WATTx
- Zero-knowledge proofs verify state transitions

## Implementation Files

### Core Consensus
- `src/auxpow/auxpow.h` - AuxPoW data structures
- `src/auxpow/auxpow.cpp` - AuxPoW validation
- `src/consensus/auxpow_validation.cpp` - Consensus rules

### Mining
- `src/miner/merged_miner.cpp` - Merged mining logic
- `src/stratum/merged_stratum.cpp` - Pool stratum server

### Privacy Bridge
- `contracts/PrivacyBridge.sol` - Batch commitment contract
- `contracts/AtomicSwap.sol` - HTLC swap contract
- `src/bridge/bridge_node.cpp` - Bridge node daemon

## Security Considerations

1. **51% Attack**: Combined hashpower of both chains provides security
2. **Selfish Mining**: Standard mitigations apply
3. **Bridge Security**: Multi-sig validators for cross-chain
4. **Privacy Leakage**: Ring signatures protect swap privacy
