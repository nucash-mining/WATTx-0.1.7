// Multi-chain configuration for Privacy Bridge
// Real USDT/USDT.e addresses on each chain

const CHAIN_CONFIG = {
    // Altcoinchain (our deployed MockUSDT for testing)
    altcoinchain: {
        chainId: 2330,
        name: "Altcoinchain",
        symbol: "ALT",
        rpc: "https://alt-rpc2.minethepla.net",
        usdt: "0xA63C722Fc164e98cc47F05397fDab1aBb7A8f7CB", // MockUSDT
        usdtDecimals: 6,
        bridge: "0x92764bADc530D9929726F86f9Af1fFE285477da4",
        verifier: "0x2E7D8869beAB61698e3B5Ec5C9Ed135da1a938e8",
        isTestnet: false,
        explorer: "https://altscan.net"
    },

    // Polygon Mainnet - DEPLOYED
    polygon: {
        chainId: 137,
        name: "Polygon",
        symbol: "MATIC",
        rpc: "https://polygon-bor-rpc.publicnode.com",
        usdt: "0xc2132D05D31c914a87C6611C10748AEb04B58e8F", // USDT (PoS)
        usdtDecimals: 6,
        bridge: "0x555f7642A0420EAF329925Aad8A440b60ac4bD9D",
        verifier: "0xa2Ec8b090f732B13eFa022abaE93A713FcFa6C5B",
        isTestnet: false,
        explorer: "https://polygonscan.com"
    },

    // Ethereum Mainnet
    ethereum: {
        chainId: 1,
        name: "Ethereum",
        symbol: "ETH",
        rpc: "https://eth.llamarpc.com",
        usdt: "0xdAC17F958D2ee523a2206206994597C13D831ec7", // Tether USD
        usdtDecimals: 6,
        bridge: null,
        verifier: null,
        isTestnet: false,
        explorer: "https://etherscan.io"
    },

    // BSC Mainnet
    bsc: {
        chainId: 56,
        name: "BNB Smart Chain",
        symbol: "BNB",
        rpc: "https://bsc-dataseed.binance.org",
        usdt: "0x55d398326f99059fF775485246999027B3197955", // BSC-USD (USDT)
        usdtDecimals: 18, // BSC USDT has 18 decimals!
        bridge: null,
        verifier: null,
        isTestnet: false,
        explorer: "https://bscscan.com"
    },

    // Arbitrum One
    arbitrum: {
        chainId: 42161,
        name: "Arbitrum One",
        symbol: "ETH",
        rpc: "https://arb1.arbitrum.io/rpc",
        usdt: "0xFd086bC7CD5C481DCC9C85ebE478A1C0b69FCbb9", // USDT
        usdtDecimals: 6,
        bridge: null,
        verifier: null,
        isTestnet: false,
        explorer: "https://arbiscan.io"
    },

    // Avalanche C-Chain
    avalanche: {
        chainId: 43114,
        name: "Avalanche C-Chain",
        symbol: "AVAX",
        rpc: "https://api.avax.network/ext/bc/C/rpc",
        usdt: "0x9702230A8Ea53601f5cD2dc00fDBc13d4dF4A8c7", // USDT
        usdtDecimals: 6,
        bridge: null,
        verifier: null,
        isTestnet: false,
        explorer: "https://snowtrace.io"
    },

    // Optimism
    optimism: {
        chainId: 10,
        name: "Optimism",
        symbol: "ETH",
        rpc: "https://mainnet.optimism.io",
        usdt: "0x94b008aA00579c1307B0EF2c499aD98a8ce58e58", // USDT
        usdtDecimals: 6,
        bridge: null,
        verifier: null,
        isTestnet: false,
        explorer: "https://optimistic.etherscan.io"
    },

    // Base
    base: {
        chainId: 8453,
        name: "Base",
        symbol: "ETH",
        rpc: "https://mainnet.base.org",
        usdt: "0xfde4C96c8593536E31F229EA8f37b2ADa2699bb2", // USDT (bridged)
        usdtDecimals: 6,
        bridge: null,
        verifier: null,
        isTestnet: false,
        explorer: "https://basescan.org"
    },

    // Sepolia (testnet - our deployed)
    sepolia: {
        chainId: 11155111,
        name: "Sepolia",
        symbol: "ETH",
        rpc: "https://ethereum-sepolia-rpc.publicnode.com",
        usdt: "0x44dB5E90cE123f9caf2A8078C233845ADcBBF2cB", // MockUSDT
        usdtDecimals: 6,
        bridge: "0xb0E9244981998a52924aF8f203de6Bc6cAD36a7E",
        verifier: "0x6D9A459e72Ac975bcF7150207ac61803B5A982E6",
        isTestnet: true,
        explorer: "https://sepolia.etherscan.io"
    }
};

// Get config by network name or chain ID
function getChainConfig(networkOrChainId) {
    if (typeof networkOrChainId === 'number') {
        return Object.values(CHAIN_CONFIG).find(c => c.chainId === networkOrChainId);
    }
    return CHAIN_CONFIG[networkOrChainId];
}

// Get all mainnet chains
function getMainnetChains() {
    return Object.entries(CHAIN_CONFIG)
        .filter(([_, config]) => !config.isTestnet)
        .map(([name, config]) => ({ name, ...config }));
}

// Get all deployed bridges
function getDeployedBridges() {
    return Object.entries(CHAIN_CONFIG)
        .filter(([_, config]) => config.bridge !== null)
        .map(([name, config]) => ({ name, ...config }));
}

module.exports = {
    CHAIN_CONFIG,
    getChainConfig,
    getMainnetChains,
    getDeployedBridges
};
