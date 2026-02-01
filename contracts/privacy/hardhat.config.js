require("@nomicfoundation/hardhat-toolbox");
try { require("dotenv").config(); } catch (e) {}

/** @type import('hardhat/config').HardhatUserConfig */
module.exports = {
    solidity: {
        version: "0.8.20",
        settings: {
            optimizer: {
                enabled: true,
                runs: 200,
            },
            viaIR: true,
        },
    },
    networks: {
        hardhat: {
            chainId: 31337,
        },
        // WATTx EVM Testnet
        wattx_testnet: {
            url: process.env.WATTX_TESTNET_RPC || "http://localhost:8545",
            chainId: parseInt(process.env.WATTX_CHAINID || "31337"),
            accounts: process.env.DEPLOYER_PRIVATE_KEY ? [process.env.DEPLOYER_PRIVATE_KEY] : [],
        },
        // WATTx EVM Mainnet
        wattx: {
            url: process.env.WATTX_RPC || "http://localhost:8545",
            chainId: parseInt(process.env.WATTX_CHAINID || "31337"),
            accounts: process.env.DEPLOYER_PRIVATE_KEY ? [process.env.DEPLOYER_PRIVATE_KEY] : [],
        },
        // Ethereum Sepolia (testnet)
        sepolia: {
            url: process.env.SEPOLIA_RPC || "https://ethereum-sepolia-rpc.publicnode.com",
            chainId: 11155111,
            accounts: process.env.SEPOLIA_PRIVATE_KEY ? [process.env.SEPOLIA_PRIVATE_KEY] : [],
        },
        // Ethereum Mainnet
        ethereum: {
            url: process.env.ETHEREUM_RPC || `https://mainnet.infura.io/v3/${process.env.INFURA_KEY}`,
            chainId: 1,
            accounts: process.env.DEPLOYER_PRIVATE_KEY ? [process.env.DEPLOYER_PRIVATE_KEY] : [],
        },
        // BSC Testnet
        bsc_testnet: {
            url: "https://data-seed-prebsc-1-s1.binance.org:8545",
            chainId: 97,
            accounts: process.env.DEPLOYER_PRIVATE_KEY ? [process.env.DEPLOYER_PRIVATE_KEY] : [],
        },
        // BSC Mainnet
        bsc: {
            url: "https://bsc-dataseed.binance.org",
            chainId: 56,
            accounts: process.env.DEPLOYER_PRIVATE_KEY ? [process.env.DEPLOYER_PRIVATE_KEY] : [],
        },
        // Polygon Mumbai (testnet)
        mumbai: {
            url: process.env.MUMBAI_RPC || "https://rpc-mumbai.maticvigil.com",
            chainId: 80001,
            accounts: process.env.DEPLOYER_PRIVATE_KEY ? [process.env.DEPLOYER_PRIVATE_KEY] : [],
        },
        // Polygon Mainnet
        polygon: {
            url: process.env.POLYGON_RPC || "https://polygon-bor-rpc.publicnode.com",
            chainId: 137,
            accounts: process.env.DEPLOYER_PRIVATE_KEY ? [process.env.DEPLOYER_PRIVATE_KEY] : [],
            gasPrice: 300000000000, // 300 gwei
        },
        // Altcoinchain EVM Mainnet
        altcoinchain: {
            url: process.env.ALTCOINCHAIN_RPC || "https://alt-rpc2.minethepla.net",
            chainId: 2330,
            accounts: process.env.DEPLOYER_PRIVATE_KEY ? [process.env.DEPLOYER_PRIVATE_KEY] : [],
        },
        // Altcoinchain EVM Testnet
        altcoinchain_testnet: {
            url: process.env.ALTCOINCHAIN_TESTNET_RPC || "http://localhost:8545",
            chainId: 23301, // Testnet chain ID (if different)
            accounts: process.env.DEPLOYER_PRIVATE_KEY ? [process.env.DEPLOYER_PRIVATE_KEY] : [],
        },
        // Arbitrum One
        arbitrum: {
            url: process.env.ARBITRUM_RPC || "https://arb1.arbitrum.io/rpc",
            chainId: 42161,
            accounts: process.env.DEPLOYER_PRIVATE_KEY ? [process.env.DEPLOYER_PRIVATE_KEY] : [],
        },
        // Avalanche C-Chain
        avalanche: {
            url: process.env.AVALANCHE_RPC || "https://api.avax.network/ext/bc/C/rpc",
            chainId: 43114,
            accounts: process.env.DEPLOYER_PRIVATE_KEY ? [process.env.DEPLOYER_PRIVATE_KEY] : [],
        },
        // Optimism Mainnet
        optimism: {
            url: process.env.OPTIMISM_RPC || "https://mainnet.optimism.io",
            chainId: 10,
            accounts: process.env.DEPLOYER_PRIVATE_KEY ? [process.env.DEPLOYER_PRIVATE_KEY] : [],
        },
        // Base Mainnet
        base: {
            url: process.env.BASE_RPC || "https://mainnet.base.org",
            chainId: 8453,
            accounts: process.env.DEPLOYER_PRIVATE_KEY ? [process.env.DEPLOYER_PRIVATE_KEY] : [],
        },
    },
    etherscan: {
        apiKey: {
            mainnet: process.env.ETHERSCAN_API_KEY || "",
            sepolia: process.env.ETHERSCAN_API_KEY || "",
            bsc: process.env.BSCSCAN_API_KEY || "",
            bscTestnet: process.env.BSCSCAN_API_KEY || "",
            polygon: process.env.POLYGONSCAN_API_KEY || "",
            polygonMumbai: process.env.POLYGONSCAN_API_KEY || "",
        },
    },
    paths: {
        sources: "./contracts",
        tests: "./test",
        cache: "./cache",
        artifacts: "./artifacts",
    },
    mocha: {
        timeout: 40000,
    },
};
