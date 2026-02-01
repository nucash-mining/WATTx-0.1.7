/**
 * Chain Handler Interface and Base Class
 */

import { Algorithm, BlockTemplate, ChainConfig, Share } from '../types.js';
import { JsonRpcClient } from '../utils/rpc.js';
import { createLogger } from '../utils/logger.js';

const logger = createLogger('ChainHandler');

export interface ShareResult {
    valid: boolean;
    blockCandidate: boolean;
    blockHash?: string;
    difficulty: bigint;
    error?: string;
}

export interface CoinbaseData {
    coinbaseTx: Buffer;
    merkleBranch: string[];
    merkleRoot: string;
    reserveOffset: number;
    reserveSize: number;
}

/**
 * Abstract base class for chain handlers
 */
export abstract class ChainHandler {
    protected config: ChainConfig;
    protected rpc: JsonRpcClient;
    protected logger;

    constructor(config: ChainConfig) {
        this.config = config;
        this.rpc = new JsonRpcClient({
            host: config.daemon.host,
            port: config.daemon.port,
            user: config.daemon.user,
            password: config.daemon.password,
        });
        this.logger = createLogger(`Chain:${config.symbol}`);
    }

    // Identity
    get name(): string { return this.config.name; }
    get symbol(): string { return this.config.symbol; }
    get algorithm(): Algorithm { return this.config.algorithm; }
    get port(): number { return this.config.port; }
    get enabled(): boolean { return this.config.enabled; }
    get walletAddress(): string { return this.config.wallet; }

    // Abstract methods to implement
    abstract getBlockTemplate(): Promise<BlockTemplate>;
    abstract validateShare(share: Share, template: BlockTemplate): Promise<ShareResult>;
    abstract submitBlock(blockData: string): Promise<boolean>;
    abstract calculatePoWHash(data: Buffer, seedHash?: string): Promise<Buffer>;
    abstract difficultyToTarget(difficulty: bigint): Buffer;

    // Common methods
    async getBalance(): Promise<bigint> {
        try {
            const result = await this.rpc.callBitcoin<{ balance: number }>('getbalance');
            return BigInt(Math.floor(result.balance * 1e8));
        } catch (err) {
            this.logger.error('Failed to get balance', { error: (err as Error).message });
            return 0n;
        }
    }

    async sendPayment(address: string, amount: bigint): Promise<string | null> {
        try {
            const satoshis = Number(amount) / 1e8;
            const txid = await this.rpc.callBitcoin<string>('sendtoaddress', [address, satoshis]);
            this.logger.info(`Payment sent`, { address, amount: satoshis, txid });
            return txid;
        } catch (err) {
            this.logger.error('Failed to send payment', { address, amount, error: (err as Error).message });
            return null;
        }
    }

    async getBlockHeight(): Promise<number> {
        try {
            return await this.rpc.callBitcoin<number>('getblockcount');
        } catch (err) {
            this.logger.error('Failed to get block height', { error: (err as Error).message });
            return 0;
        }
    }

    protected targetToHex(target: Buffer): string {
        return target.toString('hex').padStart(64, '0');
    }

    protected hexToBuffer(hex: string): Buffer {
        return Buffer.from(hex.padStart(64, '0'), 'hex');
    }

    protected reverseBuffer(buffer: Buffer): Buffer {
        const reversed = Buffer.alloc(buffer.length);
        for (let i = 0; i < buffer.length; i++) {
            reversed[i] = buffer[buffer.length - 1 - i];
        }
        return reversed;
    }
}

export default ChainHandler;
