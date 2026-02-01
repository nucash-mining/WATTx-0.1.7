# WATTx Cross-Chain Privacy Pools

## Overview

The WATTx Privacy Pool system enables anonymous cross-chain transfers of USDT using Monero-inspired privacy techniques. Users can:

1. **Deposit** USDT from any supported chain (Ethereum, BSC, Polygon, etc.)
2. **Hold** funds privately on WATTx with shielded balances
3. **Withdraw** to ANY chain anonymously - no traceable link to deposit

## Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           EXTERNAL CHAINS                                    │
│  ┌────────────┐ ┌────────────┐ ┌────────────┐ ┌────────────┐ ┌────────────┐ │
│  │  Ethereum  │ │    BSC     │ │  Polygon   │ │ Altcoinchain│ │  Arbitrum  │ │
│  │ PrivacyPool│ │ PrivacyPool│ │ PrivacyPool│ │ PrivacyPool │ │ PrivacyPool│ │
│  │   (USDT)   │ │   (USDT)   │ │   (USDT)   │ │   (USDT)    │ │   (USDT)   │ │
│  │  chainId:1 │ │ chainId:56 │ │chainId:137 │ │chainId:2330 │ │chainId:42161│
│  └─────┬──────┘ └─────┬──────┘ └─────┬──────┘ └──────┬──────┘ └─────┬──────┘ │
│        │              │              │               │              │        │
│        └──────────────┴──────────────┴───────┬───────┴──────────────┘        │
│                                              │ LayerZero v2                   │
└──────────────────────────────────────────────┼───────────────────────────────┘
                       │
┌──────────────────────▼───────────────────────────────────────────┐
│                      WATTx EVM                                    │
│  ┌───────────────────────────────────────────────────────────┐   │
│  │                  PrivacyController                         │   │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────────┐    │   │
│  │  │ MerkleTree  │  │  Verifier   │  │ Chain Registry  │    │   │
│  │  │ Commitments │  │  ZK Proofs  │  │ Liquidity Track │    │   │
│  │  └─────────────┘  └─────────────┘  └─────────────────┘    │   │
│  └───────────────────────────────────────────────────────────┘   │
│                              │                                    │
│                              ▼                                    │
│  ┌───────────────────────────────────────────────────────────┐   │
│  │              WATTx UTXO Layer (Future)                     │   │
│  │         Ring Signatures + Confidential Transactions        │   │
│  └───────────────────────────────────────────────────────────┘   │
└──────────────────────────────────────────────────────────────────┘
```

## Contracts

### PrivacyPool.sol (External Chains)
Deployed on each supported chain (Ethereum, BSC, Polygon, etc.)

- Holds USDT liquidity in pool
- Fixed denominations: 100, 1000, 10,000, 100,000 USDT
- Sends deposit notifications to WATTx via LayerZero
- Processes withdrawals when authorized by WATTx

### PrivacyController.sol (WATTx EVM)
Central controller managing all privacy operations

- Maintains Merkle tree of commitments
- Verifies ZK proofs for withdrawals
- Tracks liquidity per chain
- Coordinates cross-chain withdrawals

### MerkleTree.sol
Incremental Merkle tree for commitment storage

- 20 levels (supports ~1M commitments)
- 100 root history for verification flexibility
- Keccak256 hashing (Poseidon for ZK in circuits)

### IVerifier.sol / MockVerifier.sol
ZK proof verifier interface

- Groth16 verification for withdrawal proofs
- Mock implementation for testing

### StealthAddress.sol
Library for stealth address operations

- Commitment generation
- Nullifier derivation
- View tag creation for efficient scanning

## Privacy Flow

### Deposit Flow

```
User (Ethereum)                    PrivacyPool (ETH)              WATTx Controller
      │                                  │                              │
      │  1. Approve USDT                 │                              │
      │─────────────────────────────────>│                              │
      │                                  │                              │
      │  2. deposit(amount, stealthKey)  │                              │
      │─────────────────────────────────>│                              │
      │                                  │                              │
      │                                  │  3. Lock USDT in pool        │
      │                                  │                              │
      │                                  │  4. LayerZero message        │
      │                                  │─────────────────────────────>│
      │                                  │                              │
      │                                  │                              │  5. Create commitment
      │                                  │                              │  6. Add to Merkle tree
      │                                  │                              │  7. Emit ShieldedMint
      │                                  │                              │
      │<─────────────────────────────────────────────────────────────────│
      │  8. User now has shielded balance on WATTx                      │
```

### Withdrawal Flow

```
User                               WATTx Controller              PrivacyPool (BSC)
  │                                      │                              │
  │  1. Generate ZK proof off-chain      │                              │
  │     - Proves: knows commitment       │                              │
  │     - Proves: nullifier correct      │                              │
  │     - Hides: which commitment        │                              │
  │                                      │                              │
  │  2. withdraw(proof, nullifier, etc.) │                              │
  │─────────────────────────────────────>│                              │
  │                                      │                              │
  │                                      │  3. Verify ZK proof          │
  │                                      │  4. Check nullifier unused   │
  │                                      │  5. Mark nullifier used      │
  │                                      │                              │
  │                                      │  6. LayerZero message        │
  │                                      │─────────────────────────────>│
  │                                      │                              │
  │                                      │                              │  7. Verify from WATTx
  │                                      │                              │  8. Transfer USDT
  │                                      │                              │
  │<───────────────────────────────────────────────────────────────────│
  │  9. User receives USDT on BSC (untraceable to deposit)             │
```

## Privacy Guarantees

### What's Hidden
- **Deposit-Withdrawal Link**: No on-chain link between deposit and withdrawal
- **Amount**: Fixed denominations create large anonymity sets
- **Timing**: Withdraw any time after deposit
- **Chain**: Withdraw to different chain than deposit

### What's Public
- **Deposit**: Amount, source chain, depositor address
- **Withdrawal**: Amount, destination chain, recipient address
- **But**: No link between the two

### Anonymity Set
The anonymity set size equals the number of deposits of the same denomination:
- 1,000 deposits of 100 USDT = anonymity set of 1,000
- Larger denominations may have smaller anonymity sets

## ZK Circuits (Future)

### withdraw.circom
Proves:
1. Prover knows a leaf (commitment) in the Merkle tree
2. Prover knows the secret that created the commitment
3. The nullifier is correctly derived from the secret

Public Inputs:
- Merkle root
- Nullifier hash
- Amount
- Recipient address

Private Inputs:
- Secret (known only to depositor)
- Path elements (Merkle proof)
- Path indices

## Deployment

### Testnet
```bash
cd contracts/privacy
npx hardhat run scripts/deploy.js --network sepolia
```

### Required Configuration
1. Deploy PrivacyController on WATTx EVM
2. Deploy PrivacyPool on each supported chain:
   - Ethereum (chainId: 1)
   - BSC (chainId: 56)
   - Polygon (chainId: 137)
   - Altcoinchain (chainId: 2330)
   - Arbitrum (chainId: 42161)
   - Avalanche (chainId: 43114)
3. Configure LayerZero trusted remotes
4. Set initial liquidity per chain

### Deploy to Altcoinchain
```bash
npx hardhat run scripts/deploy.js --network altcoinchain
```

**Note:** Altcoinchain requires LayerZero endpoint deployment or custom bridge integration.

## Fee Structure

- **Deposit Fee**: 0.1% (configurable, max 1%)
- **Withdrawal Fee**: 0.1% (configurable, max 1%)
- **LayerZero Fee**: Paid by user in native token

## Security Considerations

1. **ZK Verifier**: Must use production verifier (not MockVerifier) on mainnet
2. **Liquidity Tracking**: Ensures pools have sufficient funds
3. **Nullifier Replay**: Prevented by storing used nullifiers
4. **Root History**: 100 roots kept to prevent race conditions
5. **Reentrancy**: Protected with OpenZeppelin ReentrancyGuard
6. **Access Control**: Admin functions restricted to owner

## Future Enhancements

1. **UTXO Privacy Layer**: Full Monero-style privacy on WATTx native chain
   - Ring signatures for sender privacy
   - Confidential transactions for amount hiding
   - Stealth addresses for receiver privacy

2. **Poseidon Hash**: Replace Keccak with ZK-friendly Poseidon hash

3. **Relayer Network**: Enable gasless withdrawals

4. **Multi-Asset Support**: Support for other stablecoins (USDC, DAI)

5. **Compliance Tools**: Optional view key disclosure for auditors

---

*Part of WATTx v0.1.7-dev Privacy Infrastructure*
