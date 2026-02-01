/**
 * Chain Handler Factory and Registry
 */

import { Algorithm, ChainConfig } from '../types.js';
import { ChainHandler } from './chain_handler.js';
import { BitcoinHandler } from './bitcoin_handler.js';
import { MoneroHandler } from './monero_handler.js';
import { createLogger } from '../utils/logger.js';

const logger = createLogger('ChainFactory');

export { ChainHandler, ShareResult, CoinbaseData } from './chain_handler.js';
export { BitcoinHandler } from './bitcoin_handler.js';
export { MoneroHandler } from './monero_handler.js';

/**
 * Create a chain handler based on algorithm type
 */
export function createChainHandler(config: ChainConfig): ChainHandler {
    switch (config.algorithm) {
        case Algorithm.SHA256D:
            return new BitcoinHandler(config);

        case Algorithm.RANDOMX:
            return new MoneroHandler(config);

        case Algorithm.SCRYPT:
            // Litecoin handler - uses similar structure to Bitcoin
            // In production, implement ScryptHandler with proper hashing
            logger.warn('Scrypt handler not fully implemented, using Bitcoin handler');
            return new BitcoinHandler({ ...config, algorithm: Algorithm.SHA256D });

        case Algorithm.ETHASH:
            // Ethereum Classic handler
            logger.warn('Ethash handler not fully implemented');
            throw new Error('Ethash handler not implemented');

        case Algorithm.EQUIHASH:
            // Zcash/Altcoinchain handler
            logger.warn('Equihash handler not fully implemented');
            throw new Error('Equihash handler not implemented');

        case Algorithm.X11:
            // Dash handler
            logger.warn('X11 handler not fully implemented');
            throw new Error('X11 handler not implemented');

        case Algorithm.KHEAVYHASH:
            // Kaspa handler
            logger.warn('kHeavyHash handler not fully implemented');
            throw new Error('kHeavyHash handler not implemented');

        default:
            throw new Error(`Unknown algorithm: ${config.algorithm}`);
    }
}

/**
 * Chain Manager - handles multiple chains
 */
export class ChainManager {
    private handlers: Map<string, ChainHandler> = new Map();
    private algoHandlers: Map<Algorithm, ChainHandler[]> = new Map();

    constructor(configs: ChainConfig[]) {
        for (const config of configs) {
            if (!config.enabled) {
                logger.info(`Chain ${config.symbol} is disabled, skipping`);
                continue;
            }

            try {
                const handler = createChainHandler(config);
                this.handlers.set(config.symbol, handler);

                // Group by algorithm
                const existing = this.algoHandlers.get(config.algorithm) || [];
                existing.push(handler);
                this.algoHandlers.set(config.algorithm, existing);

                logger.info(`Initialized chain handler for ${config.symbol} (${config.algorithm})`);
            } catch (err) {
                logger.error(`Failed to create handler for ${config.symbol}`, {
                    error: (err as Error).message,
                });
            }
        }
    }

    getHandler(symbol: string): ChainHandler | undefined {
        return this.handlers.get(symbol);
    }

    getHandlersByAlgo(algo: Algorithm): ChainHandler[] {
        return this.algoHandlers.get(algo) || [];
    }

    getAllHandlers(): ChainHandler[] {
        return Array.from(this.handlers.values());
    }

    getEnabledChains(): ChainHandler[] {
        return this.getAllHandlers().filter(h => h.enabled);
    }

    getSupportedAlgorithms(): Algorithm[] {
        return Array.from(this.algoHandlers.keys());
    }
}

export default ChainManager;
