/**
 * WATTx Mining Pool Server
 * Multi-coin mining pool with Mining Game integration
 */

import dotenv from 'dotenv';
import { ethers } from 'ethers';
import { Algorithm, AlgorithmToCoin, ChainConfig, Client, PoolConfig, Share } from './types.js';
import { ChainManager } from './chains/index.js';
import { StratumServer } from './server/stratum_server.js';
import { FeeSplitter } from './payouts/fee_splitter.js';
import { WATTxBridge } from './consensus/wattx_bridge.js';
import { createLogger } from './utils/logger.js';

dotenv.config();

const logger = createLogger('PoolServer');

// Default configuration
const DEFAULT_CONFIG: PoolConfig = {
    name: 'WATTx Mining Pool',
    fee: 0.01,
    feeAddress: process.env.FEE_ADDRESS || '',
    payoutInterval: 3600,
    chains: [
        {
            name: 'Bitcoin',
            symbol: 'BTC',
            algorithm: Algorithm.SHA256D,
            port: 3333,
            daemon: {
                host: process.env.BTC_DAEMON_HOST || '127.0.0.1',
                port: parseInt(process.env.BTC_DAEMON_PORT || '8332'),
                user: process.env.BTC_DAEMON_USER,
                password: process.env.BTC_DAEMON_PASS,
            },
            wallet: process.env.BTC_WALLET || '',
            enabled: process.env.BTC_ENABLED === 'true',
            minPayout: BigInt(process.env.BTC_MIN_PAYOUT || '100000'), // 0.001 BTC
            confirmations: 6,
        },
        {
            name: 'Litecoin',
            symbol: 'LTC',
            algorithm: Algorithm.SCRYPT,
            port: 3334,
            daemon: {
                host: process.env.LTC_DAEMON_HOST || '127.0.0.1',
                port: parseInt(process.env.LTC_DAEMON_PORT || '9332'),
                user: process.env.LTC_DAEMON_USER,
                password: process.env.LTC_DAEMON_PASS,
            },
            wallet: process.env.LTC_WALLET || '',
            enabled: process.env.LTC_ENABLED === 'true',
            minPayout: BigInt(process.env.LTC_MIN_PAYOUT || '1000000'), // 0.01 LTC
            confirmations: 12,
        },
        {
            name: 'Monero',
            symbol: 'XMR',
            algorithm: Algorithm.RANDOMX,
            port: 3335,
            daemon: {
                host: process.env.XMR_DAEMON_HOST || '127.0.0.1',
                port: parseInt(process.env.XMR_DAEMON_PORT || '18081'),
            },
            wallet: process.env.XMR_WALLET || '',
            enabled: process.env.XMR_ENABLED === 'true',
            minPayout: BigInt(process.env.XMR_MIN_PAYOUT || '10000000000'), // 0.01 XMR
            confirmations: 10,
        },
        {
            name: 'Ethereum Classic',
            symbol: 'ETC',
            algorithm: Algorithm.ETHASH,
            port: 3336,
            daemon: {
                host: process.env.ETC_DAEMON_HOST || '127.0.0.1',
                port: parseInt(process.env.ETC_DAEMON_PORT || '8545'),
            },
            wallet: process.env.ETC_WALLET || '',
            enabled: process.env.ETC_ENABLED === 'true',
            minPayout: BigInt(process.env.ETC_MIN_PAYOUT || '100000000000000000'), // 0.1 ETC
            confirmations: 120,
        },
        {
            name: 'Kaspa',
            symbol: 'KAS',
            algorithm: Algorithm.KHEAVYHASH,
            port: 3337,
            daemon: {
                host: process.env.KAS_DAEMON_HOST || '127.0.0.1',
                port: parseInt(process.env.KAS_DAEMON_PORT || '16110'),
            },
            wallet: process.env.KAS_WALLET || '',
            enabled: process.env.KAS_ENABLED === 'true',
            minPayout: BigInt(process.env.KAS_MIN_PAYOUT || '10000000000'), // 100 KAS
            confirmations: 100,
        },
        {
            name: 'Dash',
            symbol: 'DASH',
            algorithm: Algorithm.X11,
            port: 3338,
            daemon: {
                host: process.env.DASH_DAEMON_HOST || '127.0.0.1',
                port: parseInt(process.env.DASH_DAEMON_PORT || '9998'),
                user: process.env.DASH_DAEMON_USER,
                password: process.env.DASH_DAEMON_PASS,
            },
            wallet: process.env.DASH_WALLET || '',
            enabled: process.env.DASH_ENABLED === 'true',
            minPayout: BigInt(process.env.DASH_MIN_PAYOUT || '10000000'), // 0.1 DASH
            confirmations: 6,
        },
        {
            name: 'Altcoinchain',
            symbol: 'ALT',
            algorithm: Algorithm.ETHASH,
            port: 3339,
            daemon: {
                host: process.env.ALT_DAEMON_HOST || '127.0.0.1',
                port: parseInt(process.env.ALT_DAEMON_PORT || '8646'),
            },
            wallet: process.env.ALT_WALLET || '',
            enabled: process.env.ALT_ENABLED === 'true',
            minPayout: BigInt(process.env.ALT_MIN_PAYOUT || '1000000000000000000'), // 1 ALT
            confirmations: 12,
        },
    ],
    wattx: {
        rpcHost: process.env.WATTX_RPC_HOST || '127.0.0.1',
        rpcPort: parseInt(process.env.WATTX_RPC_PORT || '1337'),
        rpcUser: process.env.WATTX_RPC_USER || 'pool',
        rpcPassword: process.env.WATTX_RPC_PASS || '',
    },
    contracts: {
        watt: process.env.WATT_CONTRACT || '',
        rigNFT: process.env.RIG_NFT_CONTRACT || '',
        gamePool: process.env.GAME_POOL_CONTRACT || '',
        miningEngine: process.env.MINING_ENGINE_CONTRACT || '',
        stakingPool: process.env.STAKING_POOL_CONTRACT || '',
        rewardsV2: process.env.REWARDS_V2_CONTRACT || '',
    },
};

class WATTxMiningPool {
    private config: PoolConfig;
    private chainManager: ChainManager;
    private stratumServers: Map<string, StratumServer> = new Map();
    private feeSplitter: FeeSplitter;
    private wattxBridge: WATTxBridge;

    // Stats
    private startTime = Date.now();
    private totalBlocksFound = new Map<string, number>();
    private totalSharesAccepted = new Map<string, bigint>();

    constructor(config: PoolConfig = DEFAULT_CONFIG) {
        this.config = config;
        this.chainManager = new ChainManager(config.chains);

        // Initialize EVM provider
        const evmRpcUrl = process.env.EVM_RPC_URL || 'http://127.0.0.1:8545';
        const provider = new ethers.JsonRpcProvider(evmRpcUrl);
        const privateKey = process.env.POOL_PRIVATE_KEY || '';
        const signer = new ethers.Wallet(privateKey, provider);

        // Initialize fee splitter
        this.feeSplitter = new FeeSplitter(
            provider,
            signer,
            config.contracts.gamePool
        );

        // Initialize WATTx bridge
        this.wattxBridge = new WATTxBridge({
            rpcHost: config.wattx.rpcHost,
            rpcPort: config.wattx.rpcPort,
            rpcUser: config.wattx.rpcUser,
            rpcPassword: config.wattx.rpcPassword,
            evmRpcUrl,
            rewardsContractAddress: config.contracts.rewardsV2,
            privateKey,
        });
    }

    async start(): Promise<void> {
        logger.info('Starting WATTx Mining Pool', { name: this.config.name });

        // Start stratum servers for each enabled chain
        for (const handler of this.chainManager.getEnabledChains()) {
            try {
                const server = new StratumServer(
                    {
                        port: handler.port,
                        algorithm: handler.algorithm,
                        maxClients: 500,
                        jobTimeout: 60,
                        defaultDifficulty: 10000n,
                    },
                    handler,
                    (client, share) => this.handleShare(handler.symbol, client, share),
                    (chain, blockHash) => this.handleBlock(chain, blockHash)
                );

                await server.start();
                this.stratumServers.set(handler.symbol, server);

                // Create initial job
                const job = await server.createJob();
                if (job) {
                    server.broadcastJob(job);
                }

                logger.info(`Stratum server started for ${handler.name}`, {
                    symbol: handler.symbol,
                    port: handler.port,
                    algorithm: Algorithm[handler.algorithm],
                });
            } catch (err) {
                logger.error(`Failed to start stratum for ${handler.symbol}`, {
                    error: (err as Error).message,
                });
            }
        }

        // Start WATTx bridge
        this.wattxBridge.start();

        // Start job refresh timers
        this.startJobRefresh();

        // Start payout processor
        this.startPayoutProcessor();

        logger.info('WATTx Mining Pool started successfully');
    }

    async stop(): Promise<void> {
        logger.info('Stopping WATTx Mining Pool');

        // Stop stratum servers
        for (const server of this.stratumServers.values()) {
            server.stop();
        }

        // Stop WATTx bridge
        this.wattxBridge.stop();

        // Flush pending payouts
        await this.feeSplitter.flushGameDeposits();

        logger.info('WATTx Mining Pool stopped');
    }

    private async handleShare(chain: string, client: Client, share: Share): Promise<{ valid: boolean; blockCandidate: boolean; blockHash?: string; error?: string }> {
        const handler = this.chainManager.getHandler(chain);
        if (!handler) {
            return { valid: false, blockCandidate: false, error: 'Unknown chain' };
        }

        // Get current template
        const template = await handler.getBlockTemplate();

        // Validate share
        const result = await handler.validateShare(share, template);

        if (result.valid) {
            // Record for WATTx rewards
            this.wattxBridge.recordShare(share, handler.algorithm);

            // Update stats
            const accepted = this.totalSharesAccepted.get(chain) || 0n;
            this.totalSharesAccepted.set(chain, accepted + 1n);

            logger.debug('Share accepted', {
                chain,
                miner: share.minerAddress,
                difficulty: share.difficulty.toString(),
            });
        }

        return result;
    }

    private async handleBlock(chain: string, blockHash: string): Promise<void> {
        logger.info('Block found!', { chain, blockHash });

        const handler = this.chainManager.getHandler(chain);
        if (!handler) return;

        // Update stats
        const found = this.totalBlocksFound.get(chain) || 0;
        this.totalBlocksFound.set(chain, found + 1);

        // Get block reward and process fee split
        // Note: In production, you'd fetch the actual block reward
        const blockReward = 625000000n; // Placeholder: 6.25 BTC in satoshis

        // Get miner shares for this block
        const server = this.stratumServers.get(chain);
        if (!server) return;

        const clients = server.getClients();
        const minerShares = clients
            .filter(c => c.authorized)
            .map(c => ({
                address: c.minerAddress,
                shares: c.sharesAccepted.get(chain) || 0n,
                hashrate: 0, // Calculate from shares
            }));

        // Process reward distribution
        const coin = AlgorithmToCoin[handler.algorithm];
        await this.feeSplitter.processBlockReward(chain, coin, blockReward, minerShares);

        // Create new job
        const job = await server.createJob();
        if (job) {
            server.broadcastJob(job);
        }
    }

    private startJobRefresh(): void {
        // Refresh jobs every 30 seconds or on new block
        setInterval(async () => {
            for (const [chain, server] of this.stratumServers.entries()) {
                try {
                    const job = await server.createJob();
                    if (job) {
                        server.broadcastJob(job);
                    }
                } catch (err) {
                    logger.error(`Failed to refresh job for ${chain}`, {
                        error: (err as Error).message,
                    });
                }
            }
        }, 30000);
    }

    private startPayoutProcessor(): void {
        setInterval(async () => {
            try {
                // Consolidate payouts
                for (const chain of this.stratumServers.keys()) {
                    this.feeSplitter.consolidatePayouts(chain);
                }

                // Process payouts (in production, this would send actual transactions)
                const allPayouts = this.feeSplitter.getAllPendingPayouts();
                for (const [chain, payouts] of allPayouts.entries()) {
                    if (payouts.length > 0) {
                        logger.info(`Processing ${payouts.length} payouts for ${chain}`);
                        // In production: send actual payouts via chain handler
                    }
                }

                // Flush game pool deposits
                await this.feeSplitter.flushGameDeposits();

            } catch (err) {
                logger.error('Payout processor error', { error: (err as Error).message });
            }
        }, this.config.payoutInterval * 1000);
    }

    // Stats methods
    getStats(): object {
        const stats: Record<string, unknown> = {
            name: this.config.name,
            uptime: Date.now() - this.startTime,
            chains: {},
        };

        for (const [chain, server] of this.stratumServers.entries()) {
            const handler = this.chainManager.getHandler(chain);
            stats.chains = {
                ...(stats.chains as object),
                [chain]: {
                    algorithm: handler ? Algorithm[handler.algorithm] : 'unknown',
                    port: handler?.port,
                    clients: server.getClientCount(),
                    sharesAccepted: this.totalSharesAccepted.get(chain)?.toString() || '0',
                    blocksFound: this.totalBlocksFound.get(chain) || 0,
                },
            };
        }

        return stats;
    }
}

// Main entry point
async function main(): Promise<void> {
    const pool = new WATTxMiningPool();

    process.on('SIGINT', async () => {
        logger.info('Received SIGINT, shutting down...');
        await pool.stop();
        process.exit(0);
    });

    process.on('SIGTERM', async () => {
        logger.info('Received SIGTERM, shutting down...');
        await pool.stop();
        process.exit(0);
    });

    await pool.start();
}

main().catch((err) => {
    logger.error('Fatal error', { error: err.message });
    process.exit(1);
});

export { WATTxMiningPool };
export default WATTxMiningPool;
