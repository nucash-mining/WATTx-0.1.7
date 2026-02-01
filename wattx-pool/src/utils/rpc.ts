import https from 'https';
import http from 'http';
import { createLogger } from './logger.js';

const logger = createLogger('RPC');

interface RpcConfig {
    host: string;
    port: number;
    user?: string;
    password?: string;
    ssl?: boolean;
}

interface RpcResponse<T> {
    result: T | null;
    error: { code: number; message: string } | null;
    id: string | number;
}

export class JsonRpcClient {
    private config: RpcConfig;
    private requestId = 0;

    constructor(config: RpcConfig) {
        this.config = config;
    }

    async call<T>(method: string, params: unknown[] = []): Promise<T> {
        const id = ++this.requestId;

        const body = JSON.stringify({
            jsonrpc: '2.0',
            method,
            params,
            id,
        });

        const options: http.RequestOptions = {
            hostname: this.config.host,
            port: this.config.port,
            path: '/json_rpc',
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
                'Content-Length': Buffer.byteLength(body),
            },
        };

        // Add basic auth if credentials provided
        if (this.config.user && this.config.password) {
            const auth = Buffer.from(`${this.config.user}:${this.config.password}`).toString('base64');
            options.headers = {
                ...options.headers,
                'Authorization': `Basic ${auth}`,
            };
        }

        return new Promise((resolve, reject) => {
            const proto = this.config.ssl ? https : http;

            const req = proto.request(options, (res) => {
                let data = '';

                res.on('data', (chunk) => {
                    data += chunk;
                });

                res.on('end', () => {
                    try {
                        const response: RpcResponse<T> = JSON.parse(data);

                        if (response.error) {
                            logger.error(`RPC error: ${response.error.message}`, {
                                method,
                                code: response.error.code,
                            });
                            reject(new Error(response.error.message));
                        } else {
                            resolve(response.result as T);
                        }
                    } catch (err) {
                        logger.error(`Failed to parse RPC response`, { method, data });
                        reject(err);
                    }
                });
            });

            req.on('error', (err) => {
                logger.error(`RPC request failed`, { method, error: err.message });
                reject(err);
            });

            req.write(body);
            req.end();
        });
    }

    // Bitcoin-style RPC (different path)
    async callBitcoin<T>(method: string, params: unknown[] = []): Promise<T> {
        const id = ++this.requestId;

        const body = JSON.stringify({
            jsonrpc: '1.0',
            method,
            params,
            id,
        });

        const options: http.RequestOptions = {
            hostname: this.config.host,
            port: this.config.port,
            path: '/',
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
                'Content-Length': Buffer.byteLength(body),
            },
        };

        if (this.config.user && this.config.password) {
            const auth = Buffer.from(`${this.config.user}:${this.config.password}`).toString('base64');
            options.headers = {
                ...options.headers,
                'Authorization': `Basic ${auth}`,
            };
        }

        return new Promise((resolve, reject) => {
            const proto = this.config.ssl ? https : http;

            const req = proto.request(options, (res) => {
                let data = '';

                res.on('data', (chunk) => {
                    data += chunk;
                });

                res.on('end', () => {
                    try {
                        const response: RpcResponse<T> = JSON.parse(data);

                        if (response.error) {
                            reject(new Error(response.error.message));
                        } else {
                            resolve(response.result as T);
                        }
                    } catch (err) {
                        reject(err);
                    }
                });
            });

            req.on('error', reject);
            req.write(body);
            req.end();
        });
    }
}

export default JsonRpcClient;
