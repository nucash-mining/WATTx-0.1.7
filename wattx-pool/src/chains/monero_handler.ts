/**
 * Monero Chain Handler (RandomX)
 */

import { ChainHandler, ShareResult } from './chain_handler.js';
import { Algorithm, BlockTemplate, ChainConfig, Share } from '../types.js';

interface MoneroBlockTemplate {
    blockhashing_blob: string;
    blocktemplate_blob: string;
    difficulty: number;
    expected_reward: number;
    height: number;
    prev_hash: string;
    reserved_offset: number;
    seed_hash: string;
    seed_height: number;
    status: string;
}

export class MoneroHandler extends ChainHandler {
    private currentTemplate: MoneroBlockTemplate | null = null;

    constructor(config: ChainConfig) {
        super(config);
        if (config.algorithm !== Algorithm.RANDOMX) {
            throw new Error('MoneroHandler requires RANDOMX algorithm');
        }
    }

    async getBlockTemplate(): Promise<BlockTemplate> {
        const template = await this.rpc.call<MoneroBlockTemplate>('get_block_template', {
            wallet_address: this.walletAddress,
            reserve_size: 32, // For merge mining tag
        });

        if (template.status !== 'OK') {
            throw new Error(`Failed to get block template: ${template.status}`);
        }

        this.currentTemplate = template;

        return {
            blob: template.blockhashing_blob,
            difficulty: BigInt(template.difficulty),
            height: template.height,
            prevHash: template.prev_hash,
            seedHash: template.seed_hash,
            reservedOffset: template.reserved_offset,
        };
    }

    async validateShare(share: Share, template: BlockTemplate): Promise<ShareResult> {
        if (!this.currentTemplate) {
            return { valid: false, blockCandidate: false, difficulty: 0n, error: 'No template' };
        }

        try {
            // For Monero, the miner sends the result hash directly
            const hashBuffer = Buffer.from(share.result, 'hex');
            const hashBigInt = BigInt('0x' + this.reverseBuffer(hashBuffer).toString('hex'));

            // Check against share difficulty
            const shareTarget = this.difficultyToTargetBigInt(share.difficulty);
            const valid = hashBigInt < shareTarget;

            if (!valid) {
                return {
                    valid: false,
                    blockCandidate: false,
                    difficulty: share.difficulty,
                    error: 'Hash above share target',
                };
            }

            // Check if it's a block candidate
            const networkTarget = this.difficultyToTargetBigInt(BigInt(this.currentTemplate.difficulty));
            const blockCandidate = hashBigInt < networkTarget;

            return {
                valid: true,
                blockCandidate,
                difficulty: share.difficulty,
                blockHash: blockCandidate ? share.result : undefined,
            };
        } catch (err) {
            return {
                valid: false,
                blockCandidate: false,
                difficulty: share.difficulty,
                error: (err as Error).message,
            };
        }
    }

    async submitBlock(blockData: string): Promise<boolean> {
        try {
            const result = await this.rpc.call<{ status: string }>('submit_block', [blockData]);
            if (result.status === 'OK') {
                this.logger.info('Monero block submitted successfully!');
                return true;
            }
            this.logger.error('Block submission rejected', { status: result.status });
            return false;
        } catch (err) {
            this.logger.error('Block submission failed', { error: (err as Error).message });
            return false;
        }
    }

    async calculatePoWHash(data: Buffer, seedHash?: string): Promise<Buffer> {
        // RandomX requires the native hasher - this is a placeholder
        // In production, use a native binding to the RandomX library
        this.logger.warn('RandomX hashing requires native library');
        return Buffer.alloc(32);
    }

    difficultyToTarget(difficulty: bigint): Buffer {
        // Monero uses: target = 2^256 / difficulty
        const maxTarget = BigInt('0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF');
        const target = maxTarget / difficulty;
        const hex = target.toString(16).padStart(64, '0');
        return Buffer.from(hex, 'hex');
    }

    private difficultyToTargetBigInt(difficulty: bigint): bigint {
        if (difficulty === 0n) return BigInt('0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF');
        const maxTarget = BigInt('0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF');
        return maxTarget / difficulty;
    }

    // Monero-specific methods

    /**
     * Build block blob with nonce for submission
     */
    buildBlockBlob(nonce: string): string {
        if (!this.currentTemplate) {
            throw new Error('No block template');
        }

        // Replace nonce in the block template blob
        const template = this.currentTemplate.blocktemplate_blob;
        const nonceOffset = 39 * 2; // Nonce is at byte 39
        const nonceHex = nonce.padStart(8, '0');

        return template.slice(0, nonceOffset) +
               nonceHex +
               template.slice(nonceOffset + 8);
    }

    /**
     * Inject merge mining tag into reserved space
     */
    injectMergeMiningTag(tag: Buffer): string {
        if (!this.currentTemplate) {
            throw new Error('No block template');
        }

        const template = this.currentTemplate.blocktemplate_blob;
        const offset = this.currentTemplate.reserved_offset * 2;
        const tagHex = tag.toString('hex');

        return template.slice(0, offset) +
               tagHex +
               template.slice(offset + tagHex.length);
    }

    /**
     * Get full block template for merge mining
     */
    getFullTemplate(): MoneroBlockTemplate | null {
        return this.currentTemplate;
    }
}

export default MoneroHandler;
