// Private Bridge - Chain Configuration
// Supports all major EVM chains

const CHAINS = {
    polygon: {
        id: 137,
        name: 'Polygon',
        shortName: 'MATIC',
        icon: 'POL',
        rpc: 'https://polygon-bor-rpc.publicnode.com',
        explorer: 'https://polygonscan.com',
        bridge: '0x555f7642A0420EAF329925Aad8A440b60ac4bD9D',
        tokens: {
            usdt: { address: '0xc2132D05D31c914a87C6611C10748AEb04B58e8F', decimals: 6 },
            usdc: { address: '0x2791Bca1f2de4661ED88A30C99A7a9449Aa84174', decimals: 6 },
            dai: { address: '0x8f3Cf7ad23Cd3CaDbD9735AFf958023239c6A063', decimals: 18 }
        },
        deployed: true,
        color: '#8247E5'
    },
    ethereum: {
        id: 1,
        name: 'Ethereum',
        shortName: 'ETH',
        icon: 'ETH',
        rpc: 'https://eth.llamarpc.com',
        explorer: 'https://etherscan.io',
        bridge: null,
        tokens: {
            usdt: { address: '0xdAC17F958D2ee523a2206206994597C13D831ec7', decimals: 6 },
            usdc: { address: '0xA0b86991c6218b36c1d19D4a2e9Eb0cE3606eB48', decimals: 6 },
            dai: { address: '0x6B175474E89094C44Da98b954EesdedeCD73f', decimals: 18 }
        },
        deployed: false,
        color: '#627EEA'
    },
    bsc: {
        id: 56,
        name: 'BNB Chain',
        shortName: 'BNB',
        icon: 'BNB',
        rpc: 'https://bsc-dataseed.binance.org',
        explorer: 'https://bscscan.com',
        bridge: null,
        tokens: {
            usdt: { address: '0x55d398326f99059fF775485246999027B3197955', decimals: 18 },
            usdc: { address: '0x8AC76a51cc950d9822D68b83fE1Ad97B32Cd580d', decimals: 18 },
            dai: { address: '0x1AF3F329e8BE154074D8769D1FFa4eE058B1DBc3', decimals: 18 }
        },
        deployed: false,
        color: '#F0B90B'
    },
    arbitrum: {
        id: 42161,
        name: 'Arbitrum',
        shortName: 'ARB',
        icon: 'ARB',
        rpc: 'https://arb1.arbitrum.io/rpc',
        explorer: 'https://arbiscan.io',
        bridge: null,
        tokens: {
            usdt: { address: '0xFd086bC7CD5C481DCC9C85ebE478A1C0b69FCbb9', decimals: 6 },
            usdc: { address: '0xaf88d065e77c8cC2239327C5EDb3A432268e5831', decimals: 6 },
            dai: { address: '0xDA10009cBd5D07dd0CeCc66161FC93D7c9000da1', decimals: 18 }
        },
        deployed: false,
        color: '#28A0F0'
    },
    optimism: {
        id: 10,
        name: 'Optimism',
        shortName: 'OP',
        icon: 'OP',
        rpc: 'https://mainnet.optimism.io',
        explorer: 'https://optimistic.etherscan.io',
        bridge: null,
        tokens: {
            usdt: { address: '0x94b008aA00579c1307B0EF2c499aD98a8ce58e58', decimals: 6 },
            usdc: { address: '0x0b2C639c533813f4Aa9D7837CAf62653d097Ff85', decimals: 6 },
            dai: { address: '0xDA10009cBd5D07dd0CeCc66161FC93D7c9000da1', decimals: 18 }
        },
        deployed: false,
        color: '#FF0420'
    },
    avalanche: {
        id: 43114,
        name: 'Avalanche',
        shortName: 'AVAX',
        icon: 'AVAX',
        rpc: 'https://api.avax.network/ext/bc/C/rpc',
        explorer: 'https://snowtrace.io',
        bridge: null,
        tokens: {
            usdt: { address: '0x9702230A8Ea53601f5cD2dc00fDBc13d4dF4A8c7', decimals: 6 },
            usdc: { address: '0xB97EF9Ef8734C71904D8002F8b6Bc66Dd9c48a6E', decimals: 6 },
            dai: { address: '0xd586E7F844cEa2F87f50152665BCbc2C279D8d70', decimals: 18 }
        },
        deployed: false,
        color: '#E84142'
    },
    base: {
        id: 8453,
        name: 'Base',
        shortName: 'BASE',
        icon: 'BASE',
        rpc: 'https://mainnet.base.org',
        explorer: 'https://basescan.org',
        bridge: null,
        tokens: {
            usdt: { address: '0xfde4C96c8593536E31F229EA8f37b2ADa2699bb2', decimals: 6 },
            usdc: { address: '0x833589fCD6eDb6E08f4c7C32D4f71b54bdA02913', decimals: 6 }
        },
        deployed: false,
        color: '#0052FF'
    },
    altcoinchain: {
        id: 2330,
        name: 'Altcoinchain',
        shortName: 'ALT',
        icon: 'ALT',
        rpc: 'https://alt-rpc2.minethepla.net',
        explorer: 'https://altscan.net',
        bridge: '0x92764bADc530D9929726F86f9Af1fFE285477da4',
        tokens: {
            usdt: { address: '0xA63C722Fc164e98cc47F05397fDab1aBb7A8f7CB', decimals: 6 }
        },
        deployed: true,
        color: '#00D4AA'
    }
};

// Fee configuration (basis points)
const FEES = {
    shield: 10,      // 0.1%
    unshield: 10,    // 0.1%
    bridge: 15       // 0.15%
};

// Contract ABIs
const ABIS = {
    erc20: [
        'function balanceOf(address) view returns (uint256)',
        'function decimals() view returns (uint8)',
        'function symbol() view returns (string)',
        'function approve(address, uint256) returns (bool)',
        'function allowance(address, address) view returns (uint256)'
    ],
    bridge: [
        'function deposit(bytes32 commitment, uint256 amount)',
        'function withdraw(bytes calldata proof, bytes32 root, bytes32 nullifier, address recipient, uint256 amount)',
        'function nullifiers(bytes32) view returns (bool)',
        'function isValidRoot(bytes32) view returns (bool, uint256)',
        'function getLastRoot() view returns (bytes32)',
        'function getLiquidity() view returns (uint256)',
        'function nextIndex() view returns (uint256)',
        'function addExternalRoot(bytes32 root, uint256 sourceChainId)',
        'event Deposit(bytes32 indexed commitment, uint256 indexed leafIndex, uint256 amount, uint256 timestamp)',
        'event Withdrawal(bytes32 indexed nullifier, address indexed recipient, uint256 amount, uint256 sourceChainId)'
    ]
};

// Helper functions
function getChainById(chainId) {
    return Object.entries(CHAINS).find(([_, c]) => c.id === chainId);
}

function getDeployedChains() {
    return Object.entries(CHAINS).filter(([_, c]) => c.deployed);
}

function getAllChains() {
    return Object.entries(CHAINS);
}

function formatAmount(amount, decimals) {
    return parseFloat(ethers.formatUnits(amount, decimals)).toLocaleString(undefined, {
        minimumFractionDigits: 2,
        maximumFractionDigits: 6
    });
}

function shortenAddress(address) {
    return address.slice(0, 6) + '...' + address.slice(-4);
}
