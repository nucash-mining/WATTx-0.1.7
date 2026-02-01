/**
 * Stratum Server - Multi-algorithm mining protocol server
 */

import net from 'net';
import { v4 as uuidv4 } from 'uuid';
import { Algorithm, Client, Job, Share } from '../types.js';
import { ChainHandler, ShareResult } from '../chains/chain_handler.js';
import { createLogger } from '../utils/logger.js';

const logger = createLogger('StratumServer');

interface StratumMessage {
    id: string | number | null;
    method?: string;
    params?: unknown[];
    result?: unknown;
    error?: { code: number; message: string } | null;
}

export interface StratumServerConfig {
    port: number;
    algorithm: Algorithm;
    maxClients: number;
    jobTimeout: number;
    defaultDifficulty: bigint;
}

export type ShareCallback = (client: Client, share: Share) => Promise<ShareResult>;
export type BlockCallback = (chain: string, blockHash: string) => Promise<void>;

export class StratumServer {
    private server: net.Server | null = null;
    private clients: Map<string, Client> = new Map();
    private currentJob: Job | null = null;
    private jobs: Map<string, Job> = new Map();
    private jobCounter = 0;

    private onShare: ShareCallback;
    private onBlock: BlockCallback;

    constructor(
        private config: StratumServerConfig,
        private chainHandler: ChainHandler,
        onShare: ShareCallback,
        onBlock: BlockCallback
    ) {
        this.onShare = onShare;
        this.onBlock = onBlock;
    }

    async start(): Promise<void> {
        this.server = net.createServer((socket) => this.handleConnection(socket));

        return new Promise((resolve, reject) => {
            this.server!.listen(this.config.port, '0.0.0.0', () => {
                logger.info(`Stratum server started`, {
                    port: this.config.port,
                    algorithm: Algorithm[this.config.algorithm],
                    chain: this.chainHandler.symbol,
                });
                resolve();
            });

            this.server!.on('error', (err) => {
                logger.error('Server error', { error: err.message });
                reject(err);
            });
        });
    }

    stop(): void {
        if (this.server) {
            this.server.close();
            for (const client of this.clients.values()) {
                this.disconnectClient(client.id);
            }
            this.clients.clear();
        }
    }

    private handleConnection(socket: net.Socket): void {
        const clientId = uuidv4();

        const client: Client = {
            id: clientId,
            socket,
            algorithm: this.config.algorithm,
            minerAddress: '',
            workerName: '',
            authorized: false,
            subscribed: false,
            difficulty: this.config.defaultDifficulty,
            sharesAccepted: new Map(),
            sharesRejected: 0n,
            blocksFound: new Map(),
            wattxBlocksFound: 0,
            connectedAt: Date.now(),
            lastActivity: Date.now(),
            lastShareAt: 0,
        };

        this.clients.set(clientId, client);

        logger.debug(`Client connected`, {
            clientId,
            remoteAddress: socket.remoteAddress,
        });

        let buffer = '';

        socket.on('data', (data) => {
            buffer += data.toString();
            client.lastActivity = Date.now();

            // Process complete messages
            const lines = buffer.split('\n');
            buffer = lines.pop() || '';

            for (const line of lines) {
                if (line.trim()) {
                    this.handleMessage(client, line.trim());
                }
            }
        });

        socket.on('error', (err) => {
            logger.debug('Socket error', { clientId, error: err.message });
            this.disconnectClient(clientId);
        });

        socket.on('close', () => {
            logger.debug('Client disconnected', { clientId });
            this.disconnectClient(clientId);
        });
    }

    private handleMessage(client: Client, message: string): void {
        try {
            const msg: StratumMessage = JSON.parse(message);

            switch (msg.method) {
                case 'mining.subscribe':
                    this.handleSubscribe(client, msg.id);
                    break;
                case 'mining.authorize':
                    this.handleAuthorize(client, msg.id, msg.params as string[]);
                    break;
                case 'mining.submit':
                    this.handleSubmit(client, msg.id, msg.params as string[]);
                    break;
                case 'mining.get_job':
                case 'getjob':
                    this.handleGetJob(client, msg.id);
                    break;
                case 'login':
                    // XMR-style login
                    this.handleLogin(client, msg.id, msg.params as unknown[]);
                    break;
                case 'submit':
                    // XMR-style submit
                    this.handleXmrSubmit(client, msg.id, msg.params as unknown[]);
                    break;
                default:
                    logger.debug('Unknown method', { method: msg.method });
                    this.sendError(client, msg.id, -1, 'Unknown method');
            }
        } catch (err) {
            logger.debug('Failed to parse message', { message, error: (err as Error).message });
        }
    }

    // ============================================================================
    // Stratum Protocol Handlers
    // ============================================================================

    private handleSubscribe(client: Client, id: string | number | null): void {
        client.subscribed = true;

        const sessionId = client.id.slice(0, 8);
        const extraNonce1 = Buffer.alloc(4).toString('hex');

        this.sendResult(client, id, [
            [
                ['mining.set_difficulty', sessionId],
                ['mining.notify', sessionId],
            ],
            extraNonce1,
            4, // extraNonce2 size
        ]);

        logger.debug('Client subscribed', { clientId: client.id });
    }

    private handleAuthorize(client: Client, id: string | number | null, params: string[]): void {
        if (!params || params.length < 1) {
            this.sendError(client, id, -1, 'Invalid parameters');
            return;
        }

        const [login, password] = params;

        // Parse address.worker format
        const parts = login.split('.');
        client.minerAddress = parts[0];
        client.workerName = parts[1] || 'default';

        // Extract WATTx address if provided in password
        if (password && password.startsWith('wtx:')) {
            client.wattxAddress = password.slice(4);
        }

        client.authorized = true;
        this.sendResult(client, id, true);

        // Send current job
        if (this.currentJob) {
            this.sendJob(client, this.currentJob);
        }

        logger.info('Client authorized', {
            clientId: client.id,
            miner: client.minerAddress,
            worker: client.workerName,
        });
    }

    private async handleSubmit(client: Client, id: string | number | null, params: string[]): Promise<void> {
        if (!client.authorized) {
            this.sendError(client, id, -1, 'Not authorized');
            return;
        }

        if (params.length < 4) {
            this.sendError(client, id, -1, 'Invalid parameters');
            return;
        }

        const [workerName, jobId, extraNonce2, nonce, nTime] = params;

        const job = this.jobs.get(jobId);
        if (!job) {
            this.sendError(client, id, 21, 'Job not found');
            return;
        }

        const share: Share = {
            minerId: client.id,
            minerAddress: client.minerAddress,
            wattxAddress: client.wattxAddress,
            jobId,
            nonce,
            result: extraNonce2, // For validation
            difficulty: client.difficulty,
            timestamp: Date.now(),
            valid: false,
            blockCandidate: false,
        };

        try {
            const result = await this.onShare(client, share);

            if (result.valid) {
                share.valid = true;
                share.blockCandidate = result.blockCandidate;

                const accepted = client.sharesAccepted.get(this.chainHandler.symbol) || 0n;
                client.sharesAccepted.set(this.chainHandler.symbol, accepted + 1n);
                client.lastShareAt = Date.now();

                this.sendResult(client, id, true);

                if (result.blockCandidate && result.blockHash) {
                    const found = client.blocksFound.get(this.chainHandler.symbol) || 0;
                    client.blocksFound.set(this.chainHandler.symbol, found + 1);
                    await this.onBlock(this.chainHandler.symbol, result.blockHash);
                }
            } else {
                client.sharesRejected++;
                this.sendError(client, id, 23, result.error || 'Invalid share');
            }
        } catch (err) {
            client.sharesRejected++;
            this.sendError(client, id, 20, 'Internal error');
        }
    }

    private handleGetJob(client: Client, id: string | number | null): void {
        if (!client.authorized) {
            this.sendError(client, id, -1, 'Not authorized');
            return;
        }

        if (this.currentJob) {
            this.sendJob(client, this.currentJob);
        } else {
            this.sendError(client, id, -1, 'No job available');
        }
    }

    // XMR-style handlers
    private handleLogin(client: Client, id: string | number | null, params: unknown[]): void {
        const loginParams = params[0] as { login: string; pass?: string; agent?: string };

        const parts = loginParams.login.split('.');
        client.minerAddress = parts[0];
        client.workerName = parts[1] || 'default';

        if (loginParams.pass && loginParams.pass.startsWith('wtx:')) {
            client.wattxAddress = loginParams.pass.slice(4);
        }

        client.authorized = true;
        client.subscribed = true;

        // Send login result with job
        const response: Record<string, unknown> = {
            id: client.id.slice(0, 8),
            status: 'OK',
        };

        if (this.currentJob) {
            response.job = this.formatXmrJob(this.currentJob);
        }

        this.sendResult(client, id, response);

        logger.info('XMR-style login', {
            clientId: client.id,
            miner: client.minerAddress,
            agent: loginParams.agent,
        });
    }

    private async handleXmrSubmit(client: Client, id: string | number | null, params: unknown[]): Promise<void> {
        const submitParams = params[0] as { id: string; job_id: string; nonce: string; result: string };

        const share: Share = {
            minerId: client.id,
            minerAddress: client.minerAddress,
            wattxAddress: client.wattxAddress,
            jobId: submitParams.job_id,
            nonce: submitParams.nonce,
            result: submitParams.result,
            difficulty: client.difficulty,
            timestamp: Date.now(),
            valid: false,
            blockCandidate: false,
        };

        try {
            const result = await this.onShare(client, share);

            if (result.valid) {
                const accepted = client.sharesAccepted.get(this.chainHandler.symbol) || 0n;
                client.sharesAccepted.set(this.chainHandler.symbol, accepted + 1n);
                this.sendResult(client, id, { status: 'OK' });

                if (result.blockCandidate && result.blockHash) {
                    await this.onBlock(this.chainHandler.symbol, result.blockHash);
                }
            } else {
                client.sharesRejected++;
                this.sendError(client, id, -1, result.error || 'Invalid share');
            }
        } catch (err) {
            client.sharesRejected++;
            this.sendError(client, id, -1, 'Internal error');
        }
    }

    // ============================================================================
    // Job Management
    // ============================================================================

    async createJob(): Promise<Job | null> {
        try {
            const template = await this.chainHandler.getBlockTemplate();

            const job: Job = {
                id: this.generateJobId(),
                chain: this.chainHandler.symbol,
                algorithm: this.config.algorithm,
                blob: template.blob,
                target: this.chainHandler.difficultyToTarget(template.difficulty).toString('hex'),
                height: template.height,
                seedHash: template.seedHash,
                createdAt: Date.now(),
            };

            this.jobs.set(job.id, job);
            this.currentJob = job;

            // Clean old jobs
            this.cleanOldJobs();

            return job;
        } catch (err) {
            logger.error('Failed to create job', { error: (err as Error).message });
            return null;
        }
    }

    broadcastJob(job: Job): void {
        for (const client of this.clients.values()) {
            if (client.authorized && client.subscribed) {
                this.sendJob(client, job);
            }
        }
    }

    private sendJob(client: Client, job: Job): void {
        if (this.config.algorithm === Algorithm.RANDOMX) {
            // XMR-style job notification
            this.send(client, {
                jsonrpc: '2.0',
                method: 'job',
                params: this.formatXmrJob(job),
            });
        } else {
            // Bitcoin-style mining.notify
            this.send(client, {
                id: null,
                method: 'mining.notify',
                params: [
                    job.id,
                    job.blob.slice(0, 64), // prevhash
                    '', // coinbase1
                    '', // coinbase2
                    [], // merkle branches
                    '20000000', // version
                    job.target.slice(0, 8), // nbits
                    Math.floor(Date.now() / 1000).toString(16), // ntime
                    true, // clean jobs
                ],
            });
        }
    }

    private formatXmrJob(job: Job): Record<string, unknown> {
        return {
            blob: job.blob,
            job_id: job.id,
            target: job.target.slice(0, 8), // 4 bytes for target
            height: job.height,
            seed_hash: job.seedHash || '',
        };
    }

    private generateJobId(): string {
        return (++this.jobCounter).toString(16).padStart(8, '0');
    }

    private cleanOldJobs(): void {
        const maxAge = this.config.jobTimeout * 1000;
        const now = Date.now();

        for (const [id, job] of this.jobs.entries()) {
            if (now - job.createdAt > maxAge) {
                this.jobs.delete(id);
            }
        }
    }

    // ============================================================================
    // Communication Helpers
    // ============================================================================

    private send(client: Client, message: StratumMessage): void {
        try {
            const socket = client.socket as net.Socket;
            if (socket && !socket.destroyed) {
                socket.write(JSON.stringify(message) + '\n');
            }
        } catch (err) {
            logger.debug('Failed to send message', { clientId: client.id, error: (err as Error).message });
        }
    }

    private sendResult(client: Client, id: string | number | null, result: unknown): void {
        this.send(client, { id, result, error: null });
    }

    private sendError(client: Client, id: string | number | null, code: number, message: string): void {
        this.send(client, { id, result: null, error: { code, message } });
    }

    private disconnectClient(clientId: string): void {
        const client = this.clients.get(clientId);
        if (client) {
            const socket = client.socket as net.Socket;
            if (socket && !socket.destroyed) {
                socket.destroy();
            }
            this.clients.delete(clientId);
        }
    }

    // ============================================================================
    // Stats
    // ============================================================================

    getClientCount(): number {
        return this.clients.size;
    }

    getClients(): Client[] {
        return Array.from(this.clients.values());
    }

    getTotalSharesAccepted(): bigint {
        let total = 0n;
        for (const client of this.clients.values()) {
            const shares = client.sharesAccepted.get(this.chainHandler.symbol) || 0n;
            total += shares;
        }
        return total;
    }

    getTotalSharesRejected(): bigint {
        let total = 0n;
        for (const client of this.clients.values()) {
            total += client.sharesRejected;
        }
        return total;
    }
}

export default StratumServer;
