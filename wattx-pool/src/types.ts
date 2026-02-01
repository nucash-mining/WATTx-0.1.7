/**
 * WATTx Pool Type Definitions
 */

// ============================================================================
// Algorithm Types
// ============================================================================

export enum Algorithm {
    SHA256D = 0,     // Bitcoin
    SCRYPT = 1,      // Litecoin
    ETHASH = 2,      // Ethereum Classic
    RANDOMX = 3,     // Monero
    EQUIHASH = 4,    // Zcash/Altcoinchain
    X11 = 5,         // Dash
    KHEAVYHASH = 6,  // Kaspa
}

export const AlgorithmNames: Record<Algorithm, string> = {
    [Algorithm.SHA256D]: 'SHA256D',
    [Algorithm.SCRYPT]: 'Scrypt',
    [Algorithm.ETHASH]: 'Ethash',
    [Algorithm.RANDOMX]: 'RandomX',
    [Algorithm.EQUIHASH]: 'Equihash',
    [Algorithm.X11]: 'X11',
    [Algorithm.KHEAVYHASH]: 'kHeavyHash',
};

// ============================================================================
// Coin Types
// ============================================================================

export enum Coin {
    BTC = 0,
    LTC = 1,
    XMR = 2,
    ETC = 3,
    KAS = 4,
    DASH = 5,
    ALT = 6,
}

export const CoinNames: Record<Coin, string> = {
    [Coin.BTC]: 'Bitcoin',
    [Coin.LTC]: 'Litecoin',
    [Coin.XMR]: 'Monero',
    [Coin.ETC]: 'Ethereum Classic',
    [Coin.KAS]: 'Kaspa',
    [Coin.DASH]: 'Dash',
    [Coin.ALT]: 'Altcoinchain',
};

export const AlgorithmToCoin: Record<Algorithm, Coin> = {
    [Algorithm.SHA256D]: Coin.BTC,
    [Algorithm.SCRYPT]: Coin.LTC,
    [Algorithm.ETHASH]: Coin.ETC,
    [Algorithm.RANDOMX]: Coin.XMR,
    [Algorithm.EQUIHASH]: Coin.ALT,
    [Algorithm.X11]: Coin.DASH,
    [Algorithm.KHEAVYHASH]: Coin.KAS,
};

// ============================================================================
// Configuration Types
// ============================================================================

export interface ChainConfig {
    name: string;
    symbol: string;
    algorithm: Algorithm;
    port: number;
    daemon: {
        host: string;
        port: number;
        user?: string;
        password?: string;
    };
    wallet: string;
    enabled: boolean;
    minPayout: bigint;
    confirmations: number;
}

export interface PoolConfig {
    name: string;
    fee: number;  // 0.01 = 1%
    feeAddress: string;
    payoutInterval: number;  // seconds
    chains: ChainConfig[];
    wattx: {
        rpcHost: string;
        rpcPort: number;
        rpcUser: string;
        rpcPassword: string;
    };
    contracts: {
        watt: string;
        rigNFT: string;
        gamePool: string;
        miningEngine: string;
        stakingPool: string;
        rewardsV2: string;
    };
}

// ============================================================================
// Mining Types
// ============================================================================

export interface BlockTemplate {
    blob: string;
    difficulty: bigint;
    height: number;
    prevHash: string;
    seedHash?: string;  // For RandomX
    reservedOffset?: number;
}

export interface Share {
    minerId: string;
    minerAddress: string;
    wattxAddress?: string;
    jobId: string;
    nonce: string;
    result: string;
    difficulty: bigint;
    timestamp: number;
    valid: boolean;
    blockCandidate: boolean;
}

export interface MinerShare {
    address: string;
    shares: bigint;
    hashrate: number;
}

export interface Job {
    id: string;
    chain: string;
    algorithm: Algorithm;
    blob: string;
    target: string;
    height: number;
    seedHash?: string;
    wattxTarget?: string;
    wattxHeight?: number;
    createdAt: number;
}

// ============================================================================
// Client Types
// ============================================================================

export interface Client {
    id: string;
    socket: unknown;
    algorithm: Algorithm;
    minerAddress: string;
    wattxAddress?: string;
    workerName: string;
    authorized: boolean;
    subscribed: boolean;
    difficulty: bigint;

    // Stats per chain
    sharesAccepted: Map<string, bigint>;
    sharesRejected: bigint;
    blocksFound: Map<string, number>;
    wattxBlocksFound: number;

    // Timestamps
    connectedAt: number;
    lastActivity: number;
    lastShareAt: number;
}

// ============================================================================
// Stats Types
// ============================================================================

export interface PoolStats {
    hashrate: Map<Algorithm, number>;
    miners: number;
    workers: number;
    blocksFound: Map<string, number>;
    sharesAccepted: Map<string, bigint>;
    sharesRejected: bigint;
    wattxBlocksFound: number;
    uptime: number;
    lastBlockFound: Map<string, number>;
}

export interface MinerStats {
    address: string;
    hashrate: Map<Algorithm, number>;
    workers: number;
    sharesAccepted: Map<string, bigint>;
    sharesRejected: bigint;
    blocksFound: Map<string, number>;
    pendingBalance: Map<Coin, bigint>;
    totalPaid: Map<Coin, bigint>;
    lastShareAt: number;
}

// ============================================================================
// Payout Types
// ============================================================================

export interface PayoutRecord {
    id: string;
    miner: string;
    coin: Coin;
    amount: bigint;
    txid: string;
    timestamp: number;
    status: 'pending' | 'confirmed' | 'failed';
}

export interface PendingPayout {
    miner: string;
    coin: Coin;
    amount: bigint;
    createdAt: number;
}

// ============================================================================
// Game Pool Types
// ============================================================================

export interface GamePoolDeposit {
    coin: Coin;
    amount: bigint;
    epoch: number;
    timestamp: number;
    txHash?: string;
}

export interface GamePoolWithdrawal {
    miner: string;
    coin: Coin;
    amount: bigint;
    timestamp: number;
    status: 'pending' | 'authorized' | 'processed';
    txid?: string;
}
