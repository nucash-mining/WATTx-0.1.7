# WATTx Privacy Bridge dApp

Cross-chain privacy bridge interface for anonymous USDT transfers between Ethereum Sepolia, Altcoinchain, and WATTx EVM.

## Features

- **Multi-chain Support**: Ethereum Sepolia, Altcoinchain, WATTx EVM
- **Privacy Deposits**: Shield USDT with zero-knowledge commitments
- **Anonymous Withdrawals**: Withdraw to any address without linking to deposit
- **Cross-chain Transfers**: Deposit on one chain, withdraw on another
- **Local Note Storage**: Securely store commitment notes in browser

## Quick Start

```bash
# Install dependencies
npm install

# Start development server
npm run dev

# Build for production
npm run build
```

## Deployed Contracts

### Ethereum Sepolia (chainId: 11155111)
| Contract | Address |
|----------|---------|
| MockUSDT | `0x31D69920F5b500bc54103288C5E6aB88bfA3675c` |
| MockVerifier | `0x2809d3e1fCFC49Ec4236FB4592f9ccd450C386F6` |
| PrivacyPoolStandalone | `0x5234026b87c43f15fCf40421BD286Ba6319FB888` |
| PrivacyPoolBridge | `0xEcf50a033B4c104b9F1938bac54cA59fcC819606` |

### Altcoinchain (chainId: 2330)
| Contract | Address |
|----------|---------|
| MockUSDT | `0xB538B48C1BC3A6C32e12Af29B5894B0f904f8991` |
| MockVerifier | `0x9707d020C68d9A65fC4b3d3E57CA38B9BBfDAa38` |
| PrivacyPoolBridge | `0x0E6632A37099C11113Bd31Aa187B69b1729d2AB3` |

## How It Works

### Deposit Flow
1. Select source chain and amount (fixed denominations: 100, 1k, 10k, 100k USDT)
2. Generate a commitment (secret + nullifier hash)
3. **IMPORTANT**: Save the secret note (download or copy)
4. Approve USDT and confirm deposit
5. Your USDT is now shielded in the privacy pool

### Withdrawal Flow
1. Select destination chain (can be different from deposit chain)
2. Paste or upload your secret note
3. Enter recipient address (can be any address)
4. Confirm withdrawal with ZK proof
5. Recipient receives USDT with no link to your deposit

### Cross-Chain Flow
For cross-chain transfers, a relayer must sync the Merkle root from the source chain to the destination chain. The dApp handles this automatically when roots are synced.

## Fees
- Deposit: 0.1%
- Same-chain withdrawal: 0.1%
- Cross-chain withdrawal: 0.3%

## Security Notes

- **Backup your notes**: Notes are stored locally. Clear browser data = lose access
- **Use fixed amounts**: Privacy depends on anonymity set size
- **Wait for mixing**: More deposits = better privacy
- **Testnet only**: This is testnet software for testing

## Tech Stack

- React 18 + Vite
- ethers.js v6
- Tailwind CSS
- react-hot-toast
- react-icons

## Development

```bash
# Run dev server
npm run dev

# Build for production
npm run build

# Preview production build
npm run preview
```

## License

MIT
