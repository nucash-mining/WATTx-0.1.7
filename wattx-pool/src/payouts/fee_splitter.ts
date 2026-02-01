/**
 * Fee Splitter - Handles 99/1% reward distribution
 */

import { ethers } from 'ethers';
import { Coin, MinerShare, PendingPayout, GamePoolDeposit } from '../types.js';
import { createLogger } from '../utils/logger.js';

const logger = createLogger('FeeSplitter');

// GamePool contract ABI (minimal)
const GAME_POOL_ABI = [
    'function reportDeposit(uint8 coin, uint256 amount) external',
    'function reportDeposits(uint8[] coins, uint256[] amounts) external',
    'event CoinDeposited(uint8 indexed coin, uint256 amount, uint256 indexed epoch)',
];

export class FeeSplitter {
    private readonly POOL_FEE = 0.01; // 1%
    private readonly MINER_SHARE = 0.99; // 99%

    private gamePoolContract: ethers.Contract | null = null;
    private pendingPayouts: Map<string, PendingPayout[]> = new Map();
    private pendingGameDeposits: Map<Coin, bigint> = new Map();

    // Accumulate deposits for batch processing
    private depositBatchSize = 10;

    constructor(
        private provider: ethers.Provider,
        private signer: ethers.Signer,
        gamePoolAddress?: string
    ) {
        if (gamePoolAddress) {
            this.gamePoolContract = new ethers.Contract(
                gamePoolAddress,
                GAME_POOL_ABI,
                signer
            );
        }
    }

    /**
     * Process block reward and split between miners and game pool
     */
    async processBlockReward(
        chain: string,
        coin: Coin,
        blockReward: bigint,
        miners: MinerShare[]
    ): Promise<{ minerPayouts: PendingPayout[]; gameFee: bigint }> {
        // Calculate fee
        const feeAmount = (blockReward * BigInt(Math.floor(this.POOL_FEE * 10000))) / 10000n;
        const minerReward = blockReward - feeAmount;

        logger.info(`Processing block reward`, {
            chain,
            blockReward: blockReward.toString(),
            feeAmount: feeAmount.toString(),
            minerReward: minerReward.toString(),
            minerCount: miners.length,
        });

        // Calculate total shares
        const totalShares = miners.reduce((sum, m) => sum + m.shares, 0n);
        if (totalShares === 0n) {
            logger.warn('No shares to distribute');
            return { minerPayouts: [], gameFee: feeAmount };
        }

        // 99% to miners proportionally
        const minerPayouts: PendingPayout[] = [];
        for (const miner of miners) {
            const share = (minerReward * miner.shares) / totalShares;
            if (share > 0n) {
                minerPayouts.push({
                    miner: miner.address,
                    coin,
                    amount: share,
                    createdAt: Date.now(),
                });
            }
        }

        // Queue miner payouts
        this.queueMinerPayouts(chain, minerPayouts);

        // 1% to game pool
        await this.queueGameDeposit(coin, feeAmount);

        return { minerPayouts, gameFee: feeAmount };
    }

    /**
     * Queue payouts for miners
     */
    private queueMinerPayouts(chain: string, payouts: PendingPayout[]): void {
        const existing = this.pendingPayouts.get(chain) || [];
        this.pendingPayouts.set(chain, [...existing, ...payouts]);

        logger.debug(`Queued ${payouts.length} miner payouts for ${chain}`);
    }

    /**
     * Queue deposit to game pool
     */
    private async queueGameDeposit(coin: Coin, amount: bigint): Promise<void> {
        const existing = this.pendingGameDeposits.get(coin) || 0n;
        this.pendingGameDeposits.set(coin, existing + amount);

        logger.debug(`Queued game pool deposit`, {
            coin,
            amount: amount.toString(),
            total: (existing + amount).toString(),
        });

        // Check if we should flush
        const depositCount = Array.from(this.pendingGameDeposits.values()).filter(v => v > 0n).length;
        if (depositCount >= this.depositBatchSize) {
            await this.flushGameDeposits();
        }
    }

    /**
     * Flush pending game pool deposits to contract
     */
    async flushGameDeposits(): Promise<string | null> {
        if (!this.gamePoolContract) {
            logger.warn('No game pool contract configured');
            return null;
        }

        const coins: number[] = [];
        const amounts: bigint[] = [];

        for (const [coin, amount] of this.pendingGameDeposits.entries()) {
            if (amount > 0n) {
                coins.push(coin);
                amounts.push(amount);
            }
        }

        if (coins.length === 0) {
            return null;
        }

        try {
            logger.info(`Flushing ${coins.length} game pool deposits`);

            const tx = await this.gamePoolContract.reportDeposits(coins, amounts);
            const receipt = await tx.wait();

            logger.info(`Game pool deposits reported`, {
                txHash: receipt.hash,
                coins: coins.length,
            });

            // Clear pending
            this.pendingGameDeposits.clear();

            return receipt.hash;
        } catch (err) {
            logger.error('Failed to report game pool deposits', {
                error: (err as Error).message,
            });
            return null;
        }
    }

    /**
     * Get pending payouts for a chain
     */
    getPendingPayouts(chain: string): PendingPayout[] {
        return this.pendingPayouts.get(chain) || [];
    }

    /**
     * Get all pending payouts
     */
    getAllPendingPayouts(): Map<string, PendingPayout[]> {
        return this.pendingPayouts;
    }

    /**
     * Remove processed payouts
     */
    removePayout(chain: string, miner: string, amount: bigint): void {
        const payouts = this.pendingPayouts.get(chain) || [];
        const idx = payouts.findIndex(p => p.miner === miner && p.amount === amount);
        if (idx >= 0) {
            payouts.splice(idx, 1);
            this.pendingPayouts.set(chain, payouts);
        }
    }

    /**
     * Get pending game deposits (for monitoring)
     */
    getPendingGameDeposits(): Map<Coin, bigint> {
        return new Map(this.pendingGameDeposits);
    }

    /**
     * Consolidate pending payouts (combine same miner addresses)
     */
    consolidatePayouts(chain: string): void {
        const payouts = this.pendingPayouts.get(chain) || [];
        const consolidated = new Map<string, PendingPayout>();

        for (const payout of payouts) {
            const key = `${payout.miner}-${payout.coin}`;
            const existing = consolidated.get(key);
            if (existing) {
                existing.amount += payout.amount;
            } else {
                consolidated.set(key, { ...payout });
            }
        }

        this.pendingPayouts.set(chain, Array.from(consolidated.values()));
        logger.debug(`Consolidated ${payouts.length} payouts to ${consolidated.size} for ${chain}`);
    }
}

export default FeeSplitter;
