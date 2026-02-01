# WATTx Mining Pool + Mining Game System

## Overview

A comprehensive multi-coin mining pool integrated with an NFT-based mining game where:
- Miners choose algorithm and coin (BTC, LTC, XMR, ETC, KAS, DASH, ALT)
- 1% pool fee **held in native coins** (BTC, LTC, XMR, etc.) in the GamePool
- NFT mining rigs "mine" from the multi-coin pool by consuming WATT tokens
- NFT miners earn actual BTC/LTC/XMR/etc. based on their rig's algorithm trait
- Consumed WATT **transferred to staking pool** for idle NFT stakers
- Real hashrate contributes to WATTx multi-algorithm consensus

## Existing Deployed Contracts

### Polygon Network
| Contract | Address |
|----------|---------|
| Mining Rig NFTs | `0x970a8b10147e3459d3cbf56329b76ac18d329728` |
| WATT Token | `0xE960d5076cd3169C343Ee287A2c3380A222e5839` |
| NFT Staking | `0xcbfcA68D10B2ec60a0FB2Bc58F7F0Bfd32CD5275` |

### Altcoinchain Network
| Contract | Address |
|----------|---------|
| Mining Rig NFTs | `0xf9670e5D46834561813CA79854B3d7147BBbFfb2` |
| WATT Token | `0x6645143e49B3a15d8F205658903a55E520444698` |
| NFT Staking | `0xe463045318393095F11ed39f1a98332aBCc1A7b1` |

## Contract Architecture

```
contracts/mining-game/
├── contracts/
│   ├── interfaces/
│   │   ├── IWATT.sol              # WATT token interface
│   │   ├── IMiningRigNFT.sol      # Mining rig NFT interface
│   │   ├── IGamePool.sol          # Game pool interface
│   │   ├── IMiningEngine.sol      # Mining engine interface
│   │   ├── IStakingPool.sol       # Staking pool interface
│   │   ├── IPoolRegistry.sol      # Pool operator registry interface
│   │   └── IWTXBridge.sol         # WTX-WATT bridge interface
│   ├── libraries/
│   │   └── TraitCalculator.sol    # NFT trait calculations
│   ├── nfts/
│   │   └── MiningRigNFT.sol       # ERC721 mining rig NFTs
│   ├── game/
│   │   ├── GamePool.sol           # Multi-coin fee tracker
│   │   ├── MiningEngine.sol       # Virtual mining logic
│   │   ├── StakingPool.sol        # Idle NFT staking
│   │   ├── PoolRegistry.sol       # Pool operator staking & node registry
│   │   └── WTXBridge.sol          # WTX ↔ WATT token exchange
│   ├── testing/
│   │   ├── MockWATT.sol           # Mock token for testing
│   │   └── MiningGameTest.sol     # Test helper
│   └── MergedMiningRewardsV2.sol  # Pool→WTX consensus bridge
├── scripts/
│   ├── deploy.js              # Full deployment script
│   └── add-nft-contract.js    # Add new NFT models
├── hardhat.config.js
└── package.json
```

## NFT Traits

Each Mining Rig NFT has the following traits:

| Trait | Range | Description |
|-------|-------|-------------|
| hashRate | 1-10000 | Mining power (affected by rarity) |
| algorithm | 0-6 | Determines which coin pool to mine from |
| efficiency | 1-100 | Power efficiency rating |
| wattConsumption | 100-5000 | WATT tokens consumed per hour |
| rarity | 0-4 | Common(0) to Legendary(4) |
| cooling | 1-10 | Affects uptime |
| durability | 1-100 | Affects maintenance |

### Algorithm Types
| ID | Algorithm | Coin |
|----|-----------|------|
| 0 | SHA256D | Bitcoin (BTC) |
| 1 | Scrypt | Litecoin (LTC) |
| 2 | Ethash | Ethereum Classic (ETC) |
| 3 | RandomX | Monero (XMR) |
| 4 | Equihash | Altcoinchain (ALT) |
| 5 | X11 | Dash (DASH) |
| 6 | kHeavyHash | Kaspa (KAS) |

### Rarity Distribution
| Rarity | Probability | Stat Multiplier |
|--------|-------------|-----------------|
| Common | 50% | 1.0x |
| Uncommon | 30% | 1.25x |
| Rare | 15% | 1.6x |
| Epic | 4.5% | 2.2x |
| Legendary | 0.5% | 3.0x |

## Game Mechanics

### 1. Virtual Mining (MiningEngine)

Users deposit WATT tokens as "electricity" to power their NFT mining rigs:

```solidity
// Start mining with a rig
function startMining(address nftContract, uint256 rigId, uint256 wattAmount) external;

// Stop mining and get remaining WATT back
function stopMining(address nftContract, uint256 rigId) external;

// Claim mining rewards
function claimRewards(address nftContract, uint256 rigId) external;

// Add more WATT to extend runtime
function depositWatt(address nftContract, uint256 rigId, uint256 amount) external;
```

**WATT Consumption:**
- Rigs consume WATT hourly based on their `wattConsumption` trait
- Consumed WATT is transferred to the StakingPool (not burned)
- Users can check remaining runtime via `getEstimatedRuntime()`

**Earnings:**
- Earnings based on: `effectivePower × time × rewardRate / totalAlgoHashRate`
- Effective power = `hashRate × efficiency / wattConsumption`
- Rewards paid in the coin matching the rig's algorithm

### 2. Idle Staking (StakingPool)

Users can stake idle NFTs (not actively mining) to earn from consumed WATT:

```solidity
// Stake an idle rig
function stake(address nftContract, uint256 rigId) external;

// Unstake and claim rewards
function unstake(address nftContract, uint256 rigId) external;

// Claim WATT rewards
function claimRewards(address nftContract, uint256 rigId) external;
```

**Stake Weight:**
- Weight = `hashRate × efficiency × rarityMultiplier`
- Higher rarity = more WATT rewards

### 3. Multi-Coin Fee Pool (GamePool)

The pool server reports 1% fees in each native coin:

```solidity
// Pool server reports deposits (operator only)
function reportDeposit(Coin coin, uint256 amount) external;

// MiningEngine authorizes withdrawals
function authorizeWithdrawal(address miner, Coin coin, uint256 amount) external;

// Pool server confirms off-chain payout
function confirmWithdrawal(address miner, Coin coin, uint256 amount, string txid) external;
```

### 4. Pool Operator Registry (PoolRegistry)

Mining pool operators must stake WATT tokens and run a WATTx node to register:

**Requirements:**
- Stake 100,000 WATT tokens
- Run a WATTx node with RPC and P2P endpoints
- Submit regular heartbeats (hourly) to prove node is online
- 90-day unstaking hold period

```solidity
// Register pool with WATTx node
function registerPoolWithNode(
    string calldata name,
    string calldata description,
    string calldata stratumEndpoint,
    string calldata nodeRpcEndpoint,
    string calldata nodeP2pEndpoint,
    uint256 feePercent,
    uint256 minPayout,
    uint8[] calldata algorithms
) external;

// Submit heartbeat (called by operator or authorized relayer)
function submitHeartbeat(address operator, uint256 blockHeight) external;

// Request unstaking (starts 90-day hold period)
function requestUnstake() external;

// Complete unstake after hold period
function completeUnstake() external;
```

**Node Status:**
- `isNodeOnline(operator)` - Returns true if heartbeat received within 4 hours
- `getActivePools()` - Only returns pools with online nodes
- Pools with offline nodes are hidden from NFT miners

### 5. WTX-WATT Bridge (WTXBridge)

Exchange WTX (native WATTx coin) with WATT tokens on Polygon and Altcoinchain:

```solidity
// Request swap from WTX to WATT (after depositing WTX on WATTx chain)
function requestWtxToWatt(uint256 amount, string calldata wtxTxHash) external;

// Request swap from WATT to WTX (WATT transferred to bridge)
function requestWattToWtx(uint256 amount, string calldata wtxAddress) external;

// Complete swap (operator only, after verifying cross-chain tx)
function completeSwap(uint256 requestId) external;

// Get quote for swap
function getQuote(SwapDirection direction, uint256 amount) external view
    returns (uint256 outputAmount, uint256 fee);
```

**Swap Limits:**
- Minimum swap: 100 WATT/WTX
- Maximum swap: 1,000,000 WATT/WTX per request
- Daily user limit: 100,000 WATT/WTX
- Swap fee: 0.5% (configurable, max 5%)

## Adding New NFT Models

The system supports multiple NFT contracts. To add a new rig model:

1. Deploy the new NFT contract implementing `IMiningRigNFT`
2. Run the add script:
```bash
NEW_NFT_CONTRACT=0x... \
MINING_ENGINE_CONTRACT=0x... \
STAKING_POOL_CONTRACT=0x... \
npx hardhat run scripts/add-nft-contract.js --network <network>
```

3. Authorize game contracts on the new NFT:
```solidity
newNFT.setAuthorizedContract(miningEngineAddress, true);
newNFT.setAuthorizedContract(stakingPoolAddress, true);
```

## Deployment

### Prerequisites
```bash
cd contracts/mining-game
npm install
cp .env.example .env
# Edit .env with your configuration
```

### Deploy to Local Network
```bash
npx hardhat node
npx hardhat run scripts/deploy.js --network localhost
```

### Deploy to WATTx
```bash
npx hardhat run scripts/deploy.js --network wattx_mainnet
```

### Deploy to Polygon (uses existing WATT and NFT)
```bash
npx hardhat run scripts/deploy.js --network polygon
```

## Pool Server Integration

The pool server in `wattx-pool/` connects to:
- Parent chain daemons (BTC, LTC, XMR, etc.)
- GamePool contract (for fee deposits)
- MergedMiningRewardsV2 (for WTX rewards)

See `wattx-pool/.env.example` for configuration.

## Security Considerations

1. **Access Control**
   - Only operators can report GamePool deposits
   - Only MiningEngine can authorize withdrawals
   - Only owner can add new NFT contracts
   - Pool operators must stake 100,000 WATT with 90-day lock
   - Bridge operators verify cross-chain transactions

2. **Reentrancy Protection**
   - All contracts use OpenZeppelin's ReentrancyGuard

3. **Transfer Restrictions**
   - NFTs cannot be transferred while staked or mining

4. **Emergency Functions**
   - Owner can recover stuck NFTs/tokens in emergencies
   - Owner can force-close malicious pools (stake forfeited)
   - Bridge can be paused in emergencies
   - All emergency functions have appropriate access controls

5. **Pool Operator Requirements**
   - Must run a WATTx node (verified via heartbeat)
   - Node must stay online (offline nodes hidden from miners)
   - 90-day unstaking delay prevents hit-and-run attacks

6. **Bridge Security**
   - Swap limits prevent large-scale exploits
   - Daily user limits
   - 7-day request expiry for uncompleted swaps
   - Liquidity managed by owner

## Testing

```bash
cd contracts/mining-game
npx hardhat test
```

## License

MIT License
