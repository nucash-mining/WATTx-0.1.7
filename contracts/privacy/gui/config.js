// Chain configurations for WATTx Privacy Bridge
const CHAIN_CONFIG = {
    polygon: {
        chainId: 137,
        name: "Polygon",
        symbol: "MATIC",
        rpc: "https://polygon-bor-rpc.publicnode.com",
        usdt: "0xc2132D05D31c914a87C6611C10748AEb04B58e8F",
        usdtDecimals: 6,
        bridge: "0x555f7642A0420EAF329925Aad8A440b60ac4bD9D",
        verifier: "0xa2Ec8b090f732B13eFa022abaE93A713FcFa6C5B",
        explorer: "https://polygonscan.com",
        deployed: true
    },
    altcoinchain: {
        chainId: 2330,
        name: "Altcoinchain",
        symbol: "ALT",
        rpc: "https://alt-rpc2.minethepla.net",
        usdt: "0xA63C722Fc164e98cc47F05397fDab1aBb7A8f7CB",
        usdtDecimals: 6,
        bridge: "0x92764bADc530D9929726F86f9Af1fFE285477da4",
        verifier: "0x2E7D8869beAB61698e3B5Ec5C9Ed135da1a938e8",
        explorer: "https://altscan.net",
        deployed: true
    },
    bsc: {
        chainId: 56,
        name: "BNB Smart Chain",
        symbol: "BNB",
        rpc: "https://bsc-dataseed.binance.org",
        usdt: "0x55d398326f99059fF775485246999027B3197955",
        usdtDecimals: 18,
        bridge: null,
        verifier: null,
        explorer: "https://bscscan.com",
        deployed: false
    },
    arbitrum: {
        chainId: 42161,
        name: "Arbitrum One",
        symbol: "ETH",
        rpc: "https://arb1.arbitrum.io/rpc",
        usdt: "0xFd086bC7CD5C481DCC9C85ebE478A1C0b69FCbb9",
        usdtDecimals: 6,
        bridge: null,
        verifier: null,
        explorer: "https://arbiscan.io",
        deployed: false
    },
    avalanche: {
        chainId: 43114,
        name: "Avalanche C-Chain",
        symbol: "AVAX",
        rpc: "https://api.avax.network/ext/bc/C/rpc",
        usdt: "0x9702230A8Ea53601f5cD2dc00fDBc13d4dF4A8c7",
        usdtDecimals: 6,
        bridge: null,
        verifier: null,
        explorer: "https://snowtrace.io",
        deployed: false
    },
    optimism: {
        chainId: 10,
        name: "Optimism",
        symbol: "ETH",
        rpc: "https://mainnet.optimism.io",
        usdt: "0x94b008aA00579c1307B0EF2c499aD98a8ce58e58",
        usdtDecimals: 6,
        bridge: null,
        verifier: null,
        explorer: "https://optimistic.etherscan.io",
        deployed: false
    },
    base: {
        chainId: 8453,
        name: "Base",
        symbol: "ETH",
        rpc: "https://mainnet.base.org",
        usdt: "0xfde4C96c8593536E31F229EA8f37b2ADa2699bb2",
        usdtDecimals: 6,
        bridge: null,
        verifier: null,
        explorer: "https://basescan.org",
        deployed: false
    },
    ethereum: {
        chainId: 1,
        name: "Ethereum",
        symbol: "ETH",
        rpc: "https://eth.llamarpc.com",
        usdt: "0xdAC17F958D2ee523a2206206994597C13D831ec7",
        usdtDecimals: 6,
        bridge: null,
        verifier: null,
        explorer: "https://etherscan.io",
        deployed: false
    }
};

// Fee configuration (in basis points, 100 = 1%)
const FEES = {
    shieldingFee: 10,   // 0.1%
    bridgeFee: 5,       // 0.05%
    relayerFee: 10      // 0.1%
};

// Contract ABIs
const ERC20_ABI = [
    "function balanceOf(address owner) view returns (uint256)",
    "function decimals() view returns (uint8)",
    "function symbol() view returns (string)",
    "function approve(address spender, uint256 amount) returns (bool)",
    "function allowance(address owner, address spender) view returns (uint256)"
];

const BRIDGE_ABI = [
    "function deposit(bytes32 commitment, uint256 amount)",
    "function withdraw(bytes calldata proof, bytes32 root, bytes32 nullifier, address recipient, uint256 amount)",
    "function nullifiers(bytes32) view returns (bool)",
    "function isValidRoot(bytes32 root) view returns (bool isValid, uint256 sourceChainId)",
    "function getLastRoot() view returns (bytes32)",
    "function commitments(bytes32) view returns (bool)",
    "function token() view returns (address)",
    "function nextIndex() view returns (uint256)",
    "function getLiquidity() view returns (uint256)",
    "function getStats() view returns (uint256 deposited, uint256 withdrawn, uint256 bridgedIn, uint256 commitmentCount, bytes32 currentRoot, uint256 liquidity)",
    "function addLiquidity(uint256 amount)",
    "function depositFeeBps() view returns (uint256)",
    "function withdrawFeeBps() view returns (uint256)",
    "function bridgeFeeBps() view returns (uint256)",
    "event Deposit(bytes32 indexed commitment, uint256 indexed leafIndex, uint256 amount, uint256 timestamp)",
    "event Withdrawal(bytes32 indexed nullifier, address indexed recipient, uint256 amount, uint256 sourceChainId)"
];

// Helper to get chain config by chain ID
function getChainByChainId(chainId) {
    return Object.entries(CHAIN_CONFIG).find(([_, config]) => config.chainId === chainId);
}

// Helper to get deployed chains only
function getDeployedChains() {
    return Object.entries(CHAIN_CONFIG)
        .filter(([_, config]) => config.deployed)
        .map(([key, config]) => ({ key, ...config }));
}
