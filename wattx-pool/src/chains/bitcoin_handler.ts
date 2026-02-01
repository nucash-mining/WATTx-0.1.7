/**
 * Bitcoin Chain Handler (SHA256D)
 * Also usable for BCH, BSV with minor modifications
 */

import crypto from 'crypto';
import { ChainHandler, ShareResult } from './chain_handler.js';
import { Algorithm, BlockTemplate, ChainConfig, Share } from '../types.js';

interface BitcoinBlockTemplate {
    version: number;
    previousblockhash: string;
    transactions: Array<{
        data: string;
        hash: string;
        fee: number;
    }>;
    coinbaseaux: { flags: string };
    coinbasevalue: number;
    target: string;
    mintime: number;
    curtime: number;
    mutable: string[];
    noncerange: string;
    bits: string;
    height: number;
}

export class BitcoinHandler extends ChainHandler {
    private currentTemplate: BitcoinBlockTemplate | null = null;

    constructor(config: ChainConfig) {
        super(config);
        if (config.algorithm !== Algorithm.SHA256D) {
            throw new Error('BitcoinHandler requires SHA256D algorithm');
        }
    }

    async getBlockTemplate(): Promise<BlockTemplate> {
        const template = await this.rpc.callBitcoin<BitcoinBlockTemplate>('getblocktemplate', [
            { rules: ['segwit'] }
        ]);

        this.currentTemplate = template;

        // Convert target to difficulty
        const targetBuf = this.hexToBuffer(template.target);
        const difficulty = this.targetToDifficulty(targetBuf);

        // Build the hashing blob (header structure)
        const headerBlob = this.buildHeaderBlob(template);

        return {
            blob: headerBlob.toString('hex'),
            difficulty,
            height: template.height,
            prevHash: template.previousblockhash,
        };
    }

    async validateShare(share: Share, template: BlockTemplate): Promise<ShareResult> {
        if (!this.currentTemplate) {
            return { valid: false, blockCandidate: false, difficulty: 0n, error: 'No template' };
        }

        try {
            // Parse nonce
            const nonce = parseInt(share.nonce, 16);

            // Build header with nonce
            const header = this.buildHeaderWithNonce(this.currentTemplate, nonce);

            // Calculate double SHA256
            const hash = this.sha256d(header);
            const hashBigInt = BigInt('0x' + this.reverseBuffer(hash).toString('hex'));

            // Check against share difficulty
            const shareTarget = this.difficultyToTargetBigInt(share.difficulty);
            const valid = hashBigInt < shareTarget;

            if (!valid) {
                return { valid: false, blockCandidate: false, difficulty: share.difficulty, error: 'Hash above target' };
            }

            // Check if it's a block candidate (meets network difficulty)
            const networkTarget = BigInt('0x' + this.currentTemplate.target);
            const blockCandidate = hashBigInt < networkTarget;

            return {
                valid: true,
                blockCandidate,
                difficulty: share.difficulty,
                blockHash: blockCandidate ? this.reverseBuffer(hash).toString('hex') : undefined,
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
            await this.rpc.callBitcoin('submitblock', [blockData]);
            this.logger.info('Block submitted successfully!');
            return true;
        } catch (err) {
            this.logger.error('Block submission failed', { error: (err as Error).message });
            return false;
        }
    }

    async calculatePoWHash(data: Buffer): Promise<Buffer> {
        return this.sha256d(data);
    }

    difficultyToTarget(difficulty: bigint): Buffer {
        // Bitcoin difficulty 1 target
        const maxTarget = BigInt('0x00000000FFFF0000000000000000000000000000000000000000000000000000');
        const target = maxTarget / difficulty;
        const hex = target.toString(16).padStart(64, '0');
        return Buffer.from(hex, 'hex');
    }

    private difficultyToTargetBigInt(difficulty: bigint): bigint {
        const maxTarget = BigInt('0x00000000FFFF0000000000000000000000000000000000000000000000000000');
        return maxTarget / difficulty;
    }

    private targetToDifficulty(target: Buffer): bigint {
        const maxTarget = BigInt('0x00000000FFFF0000000000000000000000000000000000000000000000000000');
        const targetBigInt = BigInt('0x' + target.toString('hex'));
        if (targetBigInt === 0n) return 1n;
        return maxTarget / targetBigInt;
    }

    private sha256d(data: Buffer): Buffer {
        const hash1 = crypto.createHash('sha256').update(data).digest();
        return crypto.createHash('sha256').update(hash1).digest();
    }

    private buildHeaderBlob(template: BitcoinBlockTemplate): Buffer {
        // Bitcoin block header: 80 bytes
        // version (4) + prevhash (32) + merkleroot (32) + time (4) + bits (4) + nonce (4)
        const header = Buffer.alloc(80);

        // Version (little-endian)
        header.writeUInt32LE(template.version, 0);

        // Previous block hash (little-endian)
        const prevHash = Buffer.from(template.previousblockhash, 'hex');
        this.reverseBuffer(prevHash).copy(header, 4);

        // Merkle root placeholder (will be filled during mining)
        Buffer.alloc(32).copy(header, 36);

        // Timestamp (little-endian)
        header.writeUInt32LE(template.curtime, 68);

        // Bits (little-endian)
        header.writeUInt32LE(parseInt(template.bits, 16), 72);

        // Nonce placeholder
        header.writeUInt32LE(0, 76);

        return header;
    }

    private buildHeaderWithNonce(template: BitcoinBlockTemplate, nonce: number): Buffer {
        const header = this.buildHeaderBlob(template);

        // Calculate merkle root from transactions
        const merkleRoot = this.calculateMerkleRoot(template);
        merkleRoot.copy(header, 36);

        // Set nonce
        header.writeUInt32LE(nonce, 76);

        return header;
    }

    private calculateMerkleRoot(template: BitcoinBlockTemplate): Buffer {
        // Build coinbase transaction
        const coinbaseTx = this.buildCoinbaseTx(template);
        const coinbaseHash = this.sha256d(coinbaseTx);

        // Get transaction hashes
        const txHashes = [coinbaseHash];
        for (const tx of template.transactions) {
            const hash = Buffer.from(tx.hash, 'hex');
            txHashes.push(this.reverseBuffer(hash));
        }

        // Calculate merkle root
        return this.merkleRoot(txHashes);
    }

    private buildCoinbaseTx(template: BitcoinBlockTemplate): Buffer {
        // Simplified coinbase transaction
        // In production, this would include proper scriptSig with pool tag and extra nonce
        const coinbase: number[] = [];

        // Version
        coinbase.push(0x01, 0x00, 0x00, 0x00);

        // Input count
        coinbase.push(0x01);

        // Previous output (null for coinbase)
        for (let i = 0; i < 32; i++) coinbase.push(0x00);
        coinbase.push(0xff, 0xff, 0xff, 0xff);

        // Script length and height
        const heightScript = this.encodeHeight(template.height);
        coinbase.push(heightScript.length);
        coinbase.push(...heightScript);

        // Sequence
        coinbase.push(0xff, 0xff, 0xff, 0xff);

        // Output count
        coinbase.push(0x01);

        // Value (little-endian, 8 bytes)
        const value = BigInt(template.coinbasevalue);
        const valueBuf = Buffer.alloc(8);
        valueBuf.writeBigUInt64LE(value);
        coinbase.push(...valueBuf);

        // Output script (P2PKH placeholder)
        const script = Buffer.from('76a914' + '00'.repeat(20) + '88ac', 'hex');
        coinbase.push(script.length);
        coinbase.push(...script);

        // Locktime
        coinbase.push(0x00, 0x00, 0x00, 0x00);

        return Buffer.from(coinbase);
    }

    private encodeHeight(height: number): number[] {
        if (height < 17) {
            return [0x50 + height];
        }

        const bytes: number[] = [];
        let h = height;
        while (h > 0) {
            bytes.push(h & 0xff);
            h >>= 8;
        }
        return [bytes.length, ...bytes];
    }

    private merkleRoot(hashes: Buffer[]): Buffer {
        if (hashes.length === 0) {
            return Buffer.alloc(32);
        }

        while (hashes.length > 1) {
            const newLevel: Buffer[] = [];
            for (let i = 0; i < hashes.length; i += 2) {
                const left = hashes[i];
                const right = i + 1 < hashes.length ? hashes[i + 1] : left;
                const combined = Buffer.concat([left, right]);
                newLevel.push(this.sha256d(combined));
            }
            hashes = newLevel;
        }

        return hashes[0];
    }
}

export default BitcoinHandler;
