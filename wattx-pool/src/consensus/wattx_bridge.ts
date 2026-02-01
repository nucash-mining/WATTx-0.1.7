/**
 * WATTx Consensus Bridge - Connects pool to WATTx chain
 */

import { ethers } from 'ethers';
import { Algorithm, Share } from '../types.js';
import { JsonRpcClient } from '../utils/rpc.js';
import { createLogger } from '../utils/logger.js';

const logger = createLogger('WATTxBridge');

// MergedMiningRewardsV2 ABI (minimal)
const REWARDS_V2_ABI = [
    'function submitShares(address miner, uint8 algo, uint256 shares) external',
    'function submitPoolShares(uint8 algo, address[] miners, uint256[] shares) external',
    'function currentEpoch() external view returns (uint256)',
    'function getAlgorithmBonus(uint256 epoch, uint8 algo) external view returns (uint256)',
    'event SharesSubmitted(address indexed miner, uint256 indexed epoch, uint8 algo, uint256 shares)',
    'event PoolSharesSubmitted(address indexed pool, uint256 indexed epoch, uint8 algo, uint256 minerCount, uint256 totalShares)',
];

interface ShareBatch {
    algorithm: Algorithm;
    shares: Map<string, bigint>;  // miner -> share count
    timestamp: number;
}

export interface WATTxBridgeConfig {
    rpcHost: string;
    rpcPort: number;
    rpcUser: string;
    rpcPassword: string;
    evmRpcUrl: string;
    rewardsContractAddress: string;
    privateKey: string;
}

export class WATTxBridge {
    private rpc: JsonRpcClient;
    private provider: ethers.Provider;
    private signer: ethers.Wallet;
    private rewardsContract: ethers.Contract;

    // Batch shares for efficient submission
    private pendingShares: Map<Algorithm, ShareBatch> = new Map();
    private batchInterval = 30000; // 30 seconds
    private batchTimer: NodeJS.Timeout | null = null;

    constructor(config: WATTxBridgeConfig) {
        // WATTx node RPC (for block submission)
        this.rpc = new JsonRpcClient({
            host: config.rpcHost,
            port: config.rpcPort,
            user: config.rpcUser,
            password: config.rpcPassword,
        });

        // EVM provider for smart contracts
        this.provider = new ethers.JsonRpcProvider(config.evmRpcUrl);
        this.signer = new ethers.Wallet(config.privateKey, this.provider);

        this.rewardsContract = new ethers.Contract(
            config.rewardsContractAddress,
            REWARDS_V2_ABI,
            this.signer
        );
    }

    /**
     * Start the bridge (begins batch submission timer)
     */
    start(): void {
        this.batchTimer = setInterval(() => {
            this.submitPendingShares().catch(err => {
                logger.error('Failed to submit pending shares', { error: err.message });
            });
        }, this.batchInterval);

        logger.info('WATTx bridge started');
    }

    /**
     * Stop the bridge
     */
    stop(): void {
        if (this.batchTimer) {
            clearInterval(this.batchTimer);
            this.batchTimer = null;
        }
    }

    /**
     * Record a share for batch submission
     */
    recordShare(share: Share, algorithm: Algorithm): void {
        let batch = this.pendingShares.get(algorithm);

        if (!batch) {
            batch = {
                algorithm,
                shares: new Map(),
                timestamp: Date.now(),
            };
            this.pendingShares.set(algorithm, batch);
        }

        const minerAddress = share.wattxAddress || share.minerAddress;
        const current = batch.shares.get(minerAddress) || 0n;
        batch.shares.set(minerAddress, current + 1n);
    }

    /**
     * Submit pending shares to the rewards contract
     */
    async submitPendingShares(): Promise<void> {
        for (const [algo, batch] of this.pendingShares.entries()) {
            if (batch.shares.size === 0) continue;

            try {
                const miners: string[] = [];
                const shares: bigint[] = [];

                for (const [miner, count] of batch.shares.entries()) {
                    // Validate address format
                    if (ethers.isAddress(miner)) {
                        miners.push(miner);
                        shares.push(count);
                    }
                }

                if (miners.length === 0) continue;

                logger.info(`Submitting ${miners.length} miner shares for ${Algorithm[algo]}`);

                const tx = await this.rewardsContract.submitPoolShares(
                    algo,
                    miners,
                    shares
                );

                const receipt = await tx.wait();

                logger.info(`Shares submitted successfully`, {
                    algo: Algorithm[algo],
                    miners: miners.length,
                    totalShares: shares.reduce((a, b) => a + b, 0n).toString(),
                    txHash: receipt.hash,
                });

                // Clear submitted shares
                batch.shares.clear();
                batch.timestamp = Date.now();

            } catch (err) {
                logger.error(`Failed to submit shares for ${Algorithm[algo]}`, {
                    error: (err as Error).message,
                });
            }
        }
    }

    /**
     * Submit an AuxPoW block when pool finds a block
     */
    async submitAuxPowBlock(
        wattxBlockHash: string,
        parentBlockHeader: string,
        coinbaseTx: string,
        coinbaseBranch: string[],
        blockchainBranch: string[]
    ): Promise<boolean> {
        try {
            const result = await this.rpc.callBitcoin<string>('submitauxpowblock', [
                wattxBlockHash,
                parentBlockHeader,
                coinbaseTx,
                coinbaseBranch,
                blockchainBranch,
            ]);

            if (result) {
                logger.info('AuxPoW block submitted successfully!', {
                    wattxBlockHash,
                });
                return true;
            }

            return false;
        } catch (err) {
            logger.error('Failed to submit AuxPoW block', {
                error: (err as Error).message,
                wattxBlockHash,
            });
            return false;
        }
    }

    /**
     * Get current WATTx block height
     */
    async getWattxHeight(): Promise<number> {
        try {
            return await this.rpc.callBitcoin<number>('getblockcount');
        } catch (err) {
            logger.error('Failed to get WATTx block height', { error: (err as Error).message });
            return 0;
        }
    }

    /**
     * Get WATTx block template for merge mining
     */
    async getWattxBlockTemplate(): Promise<{
        prevBlock: string;
        merkleRoot: string;
        height: number;
        bits: number;
        target: string;
    } | null> {
        try {
            const template = await this.rpc.callBitcoin<{
                previousblockhash: string;
                height: number;
                bits: string;
                target: string;
            }>('getblocktemplate', [{ rules: ['segwit'] }]);

            return {
                prevBlock: template.previousblockhash,
                merkleRoot: '', // Calculated during mining
                height: template.height,
                bits: parseInt(template.bits, 16),
                target: template.target,
            };
        } catch (err) {
            logger.error('Failed to get WATTx block template', { error: (err as Error).message });
            return null;
        }
    }

    /**
     * Get algorithm bonus from rewards contract
     */
    async getAlgorithmBonus(algorithm: Algorithm): Promise<number> {
        try {
            const epoch = await this.rewardsContract.currentEpoch();
            const bonus = await this.rewardsContract.getAlgorithmBonus(epoch, algorithm);
            return Number(bonus);
        } catch (err) {
            logger.error('Failed to get algorithm bonus', { error: (err as Error).message });
            return 100; // Default 1x
        }
    }

    /**
     * Get pending share counts for monitoring
     */
    getPendingShareStats(): Map<Algorithm, { miners: number; shares: bigint }> {
        const stats = new Map<Algorithm, { miners: number; shares: bigint }>();

        for (const [algo, batch] of this.pendingShares.entries()) {
            let totalShares = 0n;
            for (const count of batch.shares.values()) {
                totalShares += count;
            }
            stats.set(algo, {
                miners: batch.shares.size,
                shares: totalShares,
            });
        }

        return stats;
    }
}

export default WATTxBridge;
