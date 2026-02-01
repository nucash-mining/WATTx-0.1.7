// WATTx Privacy Bridge - Main Application
let provider = null;
let signer = null;
let connectedAddress = null;
let currentChainId = null;

// Initialize app
document.addEventListener('DOMContentLoaded', () => {
    initTabs();
    initEventListeners();
    checkWalletConnection();
    updateChainSelectors();
});

// Tab navigation
function initTabs() {
    document.querySelectorAll('.tab').forEach(tab => {
        tab.addEventListener('click', () => {
            const tabId = tab.dataset.tab;

            document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
            document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));

            tab.classList.add('active');
            document.getElementById(`${tabId}-tab`).classList.add('active');

            if (tabId === 'status') {
                loadBridgeStatus();
            }
        });
    });
}

// Event listeners
function initEventListeners() {
    // Wallet connection
    document.getElementById('connect-btn').addEventListener('click', connectWallet);

    // Deposit events
    document.getElementById('deposit-chain').addEventListener('change', onDepositChainChange);
    document.getElementById('deposit-amount').addEventListener('input', updateFees);
    document.getElementById('approve-btn').addEventListener('click', approveUSDT);
    document.getElementById('deposit-btn').addEventListener('click', makeDeposit);

    // Amount presets
    document.querySelectorAll('.preset').forEach(btn => {
        btn.addEventListener('click', () => {
            document.querySelectorAll('.preset').forEach(b => b.classList.remove('active'));
            btn.classList.add('active');
            document.getElementById('deposit-amount').value = btn.dataset.amount;
            updateFees();
        });
    });

    // Custom contract events
    document.getElementById('custom-bridge').addEventListener('input', onCustomContractChange);
    document.getElementById('custom-usdt').addEventListener('input', onCustomContractChange);

    // Withdraw events
    document.getElementById('withdraw-note').addEventListener('input', parseNote);
    document.getElementById('load-note-btn').addEventListener('click', () => {
        document.getElementById('note-file').click();
    });
    document.getElementById('note-file').addEventListener('change', loadNoteFromFile);
    document.getElementById('use-connected').addEventListener('click', useConnectedWallet);
    document.getElementById('withdraw-btn').addEventListener('click', makeWithdrawal);
    document.getElementById('withdraw-chain').addEventListener('change', onWithdrawChainChange);
    document.getElementById('withdraw-amount-input').addEventListener('input', updateWithdrawFees);

    // Note management
    document.getElementById('copy-note').addEventListener('click', copyNote);
    document.getElementById('download-note').addEventListener('click', downloadNote);

    // Modal
    document.getElementById('modal-close').addEventListener('click', closeModal);

    // Listen for account/network changes
    if (window.ethereum) {
        window.ethereum.on('accountsChanged', handleAccountsChanged);
        window.ethereum.on('chainChanged', handleChainChanged);
    }
}

// Get current deposit config (supports custom contracts)
function getCurrentDepositConfig() {
    const chainKey = document.getElementById('deposit-chain').value;

    if (chainKey === 'custom') {
        return {
            chainId: currentChainId,
            name: 'Custom',
            symbol: 'ETH',
            rpc: null, // Use connected wallet
            usdt: document.getElementById('custom-usdt').value,
            usdtDecimals: parseInt(document.getElementById('custom-decimals').value) || 6,
            bridge: document.getElementById('custom-bridge').value,
            verifier: null,
            explorer: '',
            deployed: true
        };
    }

    return CHAIN_CONFIG[chainKey];
}

// Custom contract change handler
async function onCustomContractChange() {
    const bridge = document.getElementById('custom-bridge').value;
    const usdt = document.getElementById('custom-usdt').value;

    if (ethers.isAddress(bridge) && ethers.isAddress(usdt) && connectedAddress) {
        try {
            const config = getCurrentDepositConfig();
            await loadUSDTBalance('custom', config);
        } catch (e) {
            console.error('Custom contract error:', e);
            document.getElementById('usdt-balance').textContent = 'Error loading balance';
        }
    }
}

// Update chain selectors to show deployed status
function updateChainSelectors() {
    const depositSelect = document.getElementById('deposit-chain');
    const withdrawSelect = document.getElementById('withdraw-chain');

    // Clear and rebuild
    depositSelect.innerHTML = '';
    withdrawSelect.innerHTML = '';

    Object.entries(CHAIN_CONFIG).forEach(([key, config]) => {
        const status = config.deployed ? '' : ' (Coming Soon)';
        const disabled = !config.deployed;

        const depositOption = new Option(`${config.name}${status}`, key);
        depositOption.disabled = disabled;
        depositSelect.add(depositOption);

        const withdrawOption = new Option(`${config.name}${status}`, key);
        withdrawOption.disabled = disabled;
        withdrawSelect.add(withdrawOption);
    });

    // Set defaults - Polygon for deposit, Altcoinchain for withdraw
    depositSelect.value = 'polygon';
    withdrawSelect.value = 'altcoinchain';
}

// Wallet connection
async function connectWallet() {
    if (!window.ethereum) {
        alert('Please install MetaMask or another Web3 wallet!');
        return;
    }

    try {
        provider = new ethers.BrowserProvider(window.ethereum);
        const accounts = await provider.send('eth_requestAccounts', []);
        signer = await provider.getSigner();
        connectedAddress = accounts[0];

        const network = await provider.getNetwork();
        currentChainId = Number(network.chainId);

        updateWalletUI();
        onDepositChainChange();

    } catch (error) {
        console.error('Connection error:', error);
        alert('Failed to connect wallet: ' + error.message);
    }
}

function updateWalletUI() {
    document.getElementById('connect-btn').classList.add('hidden');
    document.getElementById('wallet-info').classList.remove('hidden');
    document.getElementById('wallet-address').textContent =
        connectedAddress.slice(0, 6) + '...' + connectedAddress.slice(-4);

    const chainEntry = getChainByChainId(currentChainId);
    const chainName = chainEntry ? chainEntry[1].name : `Chain ${currentChainId}`;
    document.getElementById('wallet-network').textContent = chainName;
}

async function checkWalletConnection() {
    if (window.ethereum) {
        try {
            provider = new ethers.BrowserProvider(window.ethereum);
            const accounts = await provider.send('eth_accounts', []);
            if (accounts.length > 0) {
                signer = await provider.getSigner();
                connectedAddress = accounts[0];
                const network = await provider.getNetwork();
                currentChainId = Number(network.chainId);
                updateWalletUI();
                onDepositChainChange();
            }
        } catch (e) {
            console.log('Not connected');
        }
    }
}

function handleAccountsChanged(accounts) {
    if (accounts.length === 0) {
        document.getElementById('connect-btn').classList.remove('hidden');
        document.getElementById('wallet-info').classList.add('hidden');
        connectedAddress = null;
    } else {
        connectedAddress = accounts[0];
        updateWalletUI();
    }
}

function handleChainChanged(chainIdHex) {
    currentChainId = parseInt(chainIdHex, 16);
    updateWalletUI();
    onDepositChainChange();
}

// Switch network
async function switchNetwork(chainKey) {
    const config = CHAIN_CONFIG[chainKey];
    if (!config) return false;

    const chainIdHex = '0x' + config.chainId.toString(16);

    try {
        await window.ethereum.request({
            method: 'wallet_switchEthereumChain',
            params: [{ chainId: chainIdHex }]
        });
        return true;
    } catch (switchError) {
        if (switchError.code === 4902) {
            try {
                await window.ethereum.request({
                    method: 'wallet_addEthereumChain',
                    params: [{
                        chainId: chainIdHex,
                        chainName: config.name,
                        nativeCurrency: {
                            name: config.symbol,
                            symbol: config.symbol,
                            decimals: 18
                        },
                        rpcUrls: [config.rpc],
                        blockExplorerUrls: [config.explorer]
                    }]
                });
                return true;
            } catch (addError) {
                console.error('Failed to add network:', addError);
                return false;
            }
        }
        console.error('Failed to switch network:', switchError);
        return false;
    }
}

// Deposit chain change handler
async function onDepositChainChange() {
    const chainKey = document.getElementById('deposit-chain').value;

    // Handle custom contract option
    if (chainKey === 'custom') {
        document.getElementById('custom-contract-section').classList.remove('hidden');
        document.getElementById('usdt-balance').textContent = 'Enter contract addresses';
        document.getElementById('approve-btn').disabled = true;
        document.getElementById('deposit-btn').disabled = true;
        return;
    } else {
        document.getElementById('custom-contract-section').classList.add('hidden');
    }

    const config = CHAIN_CONFIG[chainKey];

    if (!config || !config.deployed) {
        document.getElementById('usdt-balance').textContent = 'Bridge not deployed';
        document.getElementById('approve-btn').disabled = true;
        document.getElementById('deposit-btn').disabled = true;
        return;
    }

    // Check if on correct network
    if (currentChainId !== config.chainId) {
        document.getElementById('usdt-balance').textContent = `Switch to ${config.name}`;
        document.getElementById('approve-btn').textContent = `Switch to ${config.name}`;
        document.getElementById('approve-btn').onclick = () => switchNetwork(chainKey);
        document.getElementById('approve-btn').disabled = false;
        document.getElementById('deposit-btn').disabled = true;
        return;
    }

    // Reset approve button
    document.getElementById('approve-btn').textContent = 'Approve USDT';
    document.getElementById('approve-btn').onclick = approveUSDT;

    // Load USDT balance
    if (connectedAddress) {
        await loadUSDTBalance(chainKey);
    }

    updateFees();
}

async function loadUSDTBalance(chainKey, customConfig = null) {
    const config = customConfig || CHAIN_CONFIG[chainKey];

    try {
        const usdtContract = new ethers.Contract(config.usdt, ERC20_ABI, provider);
        const balance = await usdtContract.balanceOf(connectedAddress);
        const formatted = ethers.formatUnits(balance, config.usdtDecimals);
        document.getElementById('usdt-balance').textContent =
            parseFloat(formatted).toLocaleString() + ' USDT';

        // Check allowance
        const allowance = await usdtContract.allowance(connectedAddress, config.bridge);
        const allowanceNum = parseFloat(ethers.formatUnits(allowance, config.usdtDecimals));

        if (allowanceNum > 0) {
            document.getElementById('approve-btn').textContent =
                `Approved: ${allowanceNum.toLocaleString()} USDT`;
            document.getElementById('deposit-btn').disabled = false;
        } else {
            document.getElementById('approve-btn').textContent = 'Approve USDT';
            document.getElementById('approve-btn').disabled = false;
            document.getElementById('deposit-btn').disabled = true;
        }

    } catch (error) {
        console.error('Failed to load balance:', error);
        document.getElementById('usdt-balance').textContent = 'Error loading balance';
    }
}

// Update fee display
function updateFees() {
    const amount = parseFloat(document.getElementById('deposit-amount').value) || 0;

    const shieldFee = amount * FEES.shieldingFee / 10000;
    const bridgeFee = amount * FEES.bridgeFee / 10000;
    const receiveAmount = amount - shieldFee - bridgeFee;

    document.getElementById('shield-fee').textContent = shieldFee.toFixed(2) + ' USDT';
    document.getElementById('bridge-fee').textContent = bridgeFee.toFixed(2) + ' USDT';
    document.getElementById('receive-amount').textContent = receiveAmount.toFixed(2) + ' USDT';
}

// Approve USDT
async function approveUSDT() {
    const config = getCurrentDepositConfig();
    const amount = document.getElementById('deposit-amount').value;

    if (!amount || parseFloat(amount) <= 0) {
        alert('Please enter an amount first');
        return;
    }

    showModal('Approving USDT', 'Please confirm in your wallet...');

    try {
        const usdtContract = new ethers.Contract(config.usdt, ERC20_ABI, signer);
        const amountWei = ethers.parseUnits(amount, config.usdtDecimals);

        const tx = await usdtContract.approve(config.bridge, amountWei);
        updateModalStatus('Waiting for confirmation...');
        showTxLink(config.explorer, tx.hash);

        await tx.wait();

        updateModalStatus('Approved successfully!');
        document.getElementById('approve-btn').textContent = `Approved: ${amount} USDT`;
        document.getElementById('deposit-btn').disabled = false;

        setTimeout(closeModal, 2000);

    } catch (error) {
        console.error('Approval failed:', error);
        updateModalStatus('Approval failed: ' + (error.reason || error.message));
        showModalClose();
    }
}

// Make deposit
async function makeDeposit() {
    const chainKey = document.getElementById('deposit-chain').value;
    const config = getCurrentDepositConfig();
    const amount = document.getElementById('deposit-amount').value;

    if (!amount || parseFloat(amount) <= 0) {
        alert('Please enter an amount');
        return;
    }

    showModal('Creating Deposit', 'Generating commitment...');

    try {
        // Generate random nullifier and secret
        const nullifier = ethers.randomBytes(31);
        const secret = ethers.randomBytes(31);

        // Create commitment = hash(nullifier, secret)
        const preimage = ethers.concat([nullifier, secret]);
        const commitment = ethers.keccak256(preimage);

        updateModalStatus('Please confirm deposit in your wallet...');

        const bridgeContract = new ethers.Contract(config.bridge, BRIDGE_ABI, signer);
        const amountWei = ethers.parseUnits(amount, config.usdtDecimals);

        // Deposit with commitment and amount (ERC20 transferFrom happens in contract)
        const tx = await bridgeContract.deposit(commitment, amountWei, {
            gasLimit: 500000
        });

        updateModalStatus('Waiting for confirmation...');
        showTxLink(config.explorer, tx.hash);

        const receipt = await tx.wait();

        // Get leaf index from event
        let leafIndex = 0;
        for (const log of receipt.logs) {
            try {
                const parsed = bridgeContract.interface.parseLog(log);
                if (parsed && parsed.name === 'Deposit') {
                    leafIndex = parsed.args.leafIndex;
                    break;
                }
            } catch (e) {}
        }

        // Create note object
        const note = {
            version: 1,
            chain: chainKey === 'custom' ? 'custom' : chainKey,
            chainId: config.chainId,
            bridgeAddress: config.bridge,
            usdtAddress: config.usdt,
            usdtDecimals: config.usdtDecimals,
            amount: amount,
            nullifier: ethers.hexlify(nullifier),
            secret: ethers.hexlify(secret),
            commitment: commitment,
            leafIndex: Number(leafIndex),
            depositTx: tx.hash,
            timestamp: Date.now()
        };

        // Display note
        const noteString = JSON.stringify(note, null, 2);
        document.getElementById('secret-note').value = noteString;
        document.getElementById('note-output').classList.remove('hidden');

        updateModalStatus('Deposit successful! Save your note!');
        showModalClose();

        // Store locally
        saveNoteToStorage(note);

    } catch (error) {
        console.error('Deposit failed:', error);
        updateModalStatus('Deposit failed: ' + (error.reason || error.message));
        showModalClose();
    }
}

// Parse withdrawal note
function parseNote() {
    const noteText = document.getElementById('withdraw-note').value.trim();

    if (!noteText) {
        document.getElementById('note-details').classList.add('hidden');
        document.getElementById('withdraw-btn').disabled = true;
        return;
    }

    try {
        const note = JSON.parse(noteText);

        if (!note.nullifier || !note.secret || !note.chain) {
            throw new Error('Invalid note format');
        }

        const config = CHAIN_CONFIG[note.chain];

        document.getElementById('note-chain').textContent = config ? config.name : note.chain;
        document.getElementById('note-amount').textContent = note.amount + ' USDT';
        document.getElementById('note-status').textContent = 'Valid';
        document.getElementById('note-status').style.color = 'var(--success)';

        document.getElementById('note-details').classList.remove('hidden');
        document.getElementById('withdraw-btn').disabled = false;

        // Set default amount and load pool liquidity
        document.getElementById('withdraw-amount-input').value = note.amount;
        onWithdrawChainChange();

    } catch (error) {
        document.getElementById('note-chain').textContent = '--';
        document.getElementById('note-amount').textContent = '--';
        document.getElementById('note-status').textContent = 'Invalid format';
        document.getElementById('note-status').style.color = 'var(--danger)';
        document.getElementById('note-details').classList.remove('hidden');
        document.getElementById('withdraw-btn').disabled = true;
    }
}

// Load pool liquidity when withdraw chain changes
async function onWithdrawChainChange() {
    const chainKey = document.getElementById('withdraw-chain').value;
    const config = CHAIN_CONFIG[chainKey];

    if (!config || !config.deployed) {
        document.getElementById('pool-liquidity-info').textContent = 'Bridge not deployed';
        return;
    }

    document.getElementById('pool-liquidity-info').textContent = 'Loading liquidity...';

    try {
        const rpcProvider = new ethers.JsonRpcProvider(config.rpc);
        const bridgeContract = new ethers.Contract(config.bridge, BRIDGE_ABI, rpcProvider);
        const liquidity = await bridgeContract.getLiquidity();
        const formatted = ethers.formatUnits(liquidity, config.usdtDecimals);

        document.getElementById('pool-liquidity-info').textContent =
            `Pool liquidity: ${parseFloat(formatted).toLocaleString()} USDT`;

        // Auto-fill max available if note is loaded
        const noteText = document.getElementById('withdraw-note').value.trim();
        if (noteText) {
            try {
                const note = JSON.parse(noteText);
                const maxWithdraw = Math.min(parseFloat(note.amount), parseFloat(formatted));
                document.getElementById('withdraw-amount-input').value = maxWithdraw.toFixed(2);
                document.getElementById('withdraw-amount-input').max = maxWithdraw;
                updateWithdrawFees();
            } catch (e) {}
        }
    } catch (error) {
        console.error('Failed to load liquidity:', error);
        document.getElementById('pool-liquidity-info').textContent = 'Failed to load liquidity';
    }
}

function updateWithdrawFees() {
    const amountInput = document.getElementById('withdraw-amount-input').value;
    const amount = parseFloat(amountInput) || 0;

    const withdrawFee = amount * FEES.relayerFee / 10000;
    const finalAmount = amount - withdrawFee;

    document.getElementById('withdraw-amount').textContent = amount.toFixed(2) + ' USDT';
    document.getElementById('relayer-fee').textContent = withdrawFee.toFixed(2) + ' USDT';
    document.getElementById('final-receive').textContent = finalAmount.toFixed(2) + ' USDT';
}

// Load note from file
function loadNoteFromFile(event) {
    const file = event.target.files[0];
    if (!file) return;

    const reader = new FileReader();
    reader.onload = (e) => {
        document.getElementById('withdraw-note').value = e.target.result;
        parseNote();
    };
    reader.readAsText(file);
}

function useConnectedWallet() {
    if (connectedAddress) {
        document.getElementById('recipient-address').value = connectedAddress;
    }
}

// Make withdrawal
async function makeWithdrawal() {
    const noteText = document.getElementById('withdraw-note').value.trim();
    const destChainKey = document.getElementById('withdraw-chain').value;
    const recipient = document.getElementById('recipient-address').value.trim();

    if (!recipient || !ethers.isAddress(recipient)) {
        alert('Please enter a valid recipient address');
        return;
    }

    const note = JSON.parse(noteText);
    const destConfig = CHAIN_CONFIG[destChainKey];

    if (!destConfig.deployed) {
        alert('Destination chain bridge not deployed yet');
        return;
    }

    // Check if we need to switch networks
    if (currentChainId !== destConfig.chainId) {
        const switched = await switchNetwork(destChainKey);
        if (!switched) {
            alert(`Please switch to ${destConfig.name} network`);
            return;
        }
        // Wait for network switch
        await new Promise(resolve => setTimeout(resolve, 1000));
        provider = new ethers.BrowserProvider(window.ethereum);
        signer = await provider.getSigner();
    }

    showModal('Processing Withdrawal', 'Generating ZK proof...');

    try {
        // Generate nullifier hash
        const nullifierHash = ethers.keccak256(note.nullifier);

        // For demo/testing, we use a dummy proof since verifier is disabled
        // In production, this would call snarkjs to generate the actual proof
        const proof = generateDummyProof();

        updateModalStatus('Checking if root is synced...');

        const bridgeContract = new ethers.Contract(destConfig.bridge, BRIDGE_ABI, signer);

        // Get root from source chain or use local
        let root;
        if (note.chain === destChainKey) {
            // Same chain withdrawal
            root = await bridgeContract.getLastRoot();
        } else {
            // Cross-chain - need synced root
            // For now, try last root (relayer should have synced)
            root = await bridgeContract.getLastRoot();

            // Check if root is valid (returns tuple: [isValid, sourceChainId])
            const [isValid, sourceChainId] = await bridgeContract.isValidRoot(root);
            if (!isValid) {
                updateModalStatus('Root not synced. Please wait for relayer.');
                showModalClose();
                return;
            }
        }

        // Check if already spent (using nullifiers mapping)
        const isSpent = await bridgeContract.nullifiers(nullifierHash);
        if (isSpent) {
            updateModalStatus('This note has already been spent!');
            showModalClose();
            return;
        }

        // Get withdrawal amount from input (user can withdraw less than deposited)
        const amountInput = document.getElementById('withdraw-amount-input').value;
        const amount = parseFloat(amountInput);

        if (!amount || amount <= 0) {
            updateModalStatus('Please enter a valid withdrawal amount');
            showModalClose();
            return;
        }

        const amountWei = ethers.parseUnits(amount.toString(), destConfig.usdtDecimals);

        // Check liquidity before attempting withdrawal
        updateModalStatus('Checking pool liquidity...');
        const liquidity = await bridgeContract.getLiquidity();
        console.log('Pool liquidity:', ethers.formatUnits(liquidity, destConfig.usdtDecimals), 'USDT');
        console.log('Requested amount:', amount, 'USDT');

        if (liquidity < amountWei) {
            const available = ethers.formatUnits(liquidity, destConfig.usdtDecimals);
            updateModalStatus(`Insufficient liquidity! Pool has ${available} USDT, you requested ${amount} USDT`);
            showModalClose();
            return;
        }

        updateModalStatus('Please confirm withdrawal in your wallet...');

        console.log('Withdraw params:', {
            proof: proof.slice(0, 20) + '...',
            root: root,
            nullifierHash: nullifierHash,
            recipient: recipient,
            amount: amountWei.toString()
        });

        // Call withdraw with new signature: (proof, root, nullifier, recipient, amount)
        const tx = await bridgeContract.withdraw(
            proof,
            root,
            nullifierHash,
            recipient,
            amountWei,
            { gasLimit: 500000 }
        );

        updateModalStatus('Waiting for confirmation...');
        showTxLink(destConfig.explorer, tx.hash);

        await tx.wait();

        updateModalStatus('Withdrawal successful!');
        showModalClose();

        // Mark note as spent
        markNoteAsSpent(note);

    } catch (error) {
        console.error('Withdrawal failed:', error);
        updateModalStatus('Withdrawal failed: ' + (error.reason || error.message));
        showModalClose();
    }
}

// Generate dummy proof (verifier disabled for testing)
function generateDummyProof() {
    // This is a placeholder - in production, use snarkjs to generate real proofs
    // The verifier is currently disabled (set to address(0)) for testing
    const proof = new Uint8Array(256);
    return ethers.hexlify(proof);
}

// Note storage
function saveNoteToStorage(note) {
    const notes = JSON.parse(localStorage.getItem('wattx_notes') || '[]');
    notes.push(note);
    localStorage.setItem('wattx_notes', JSON.stringify(notes));
}

function markNoteAsSpent(note) {
    const notes = JSON.parse(localStorage.getItem('wattx_notes') || '[]');
    const idx = notes.findIndex(n => n.commitment === note.commitment);
    if (idx >= 0) {
        notes[idx].spent = true;
        localStorage.setItem('wattx_notes', JSON.stringify(notes));
    }
}

// Copy and download note
function copyNote() {
    const noteText = document.getElementById('secret-note').value;
    navigator.clipboard.writeText(noteText);
    document.getElementById('copy-note').textContent = 'Copied!';
    setTimeout(() => {
        document.getElementById('copy-note').textContent = 'Copy Note';
    }, 2000);
}

function downloadNote() {
    const noteText = document.getElementById('secret-note').value;
    const note = JSON.parse(noteText);
    const blob = new Blob([noteText], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `wattx-note-${note.chain}-${Date.now()}.json`;
    a.click();
    URL.revokeObjectURL(url);
}

// Modal functions
function showModal(title, status) {
    document.getElementById('modal-title').textContent = title;
    document.getElementById('modal-status').textContent = status;
    document.getElementById('modal-tx-link').classList.add('hidden');
    document.getElementById('modal-close').classList.add('hidden');
    document.querySelector('.spinner').style.display = 'block';
    document.getElementById('tx-modal').classList.remove('hidden');
}

function updateModalStatus(status) {
    document.getElementById('modal-status').textContent = status;
}

function showTxLink(explorer, txHash) {
    const link = document.getElementById('modal-tx-link');
    link.href = `${explorer}/tx/${txHash}`;
    link.classList.remove('hidden');
}

function showModalClose() {
    document.getElementById('modal-close').classList.remove('hidden');
    document.querySelector('.spinner').style.display = 'none';
}

function closeModal() {
    document.getElementById('tx-modal').classList.add('hidden');
}

// Bridge status
async function loadBridgeStatus() {
    const deployedChains = getDeployedChains();

    for (const chain of deployedChains) {
        const statusEl = document.getElementById(`status-${chain.key}`);
        const poolEl = document.getElementById(`pool-${chain.key}`);

        if (!statusEl || !poolEl) continue;

        try {
            const rpcProvider = new ethers.JsonRpcProvider(chain.rpc);

            // Check bridge contract using getStats()
            const bridgeContract = new ethers.Contract(chain.bridge, BRIDGE_ABI, rpcProvider);
            const stats = await bridgeContract.getStats();
            const liquidity = ethers.formatUnits(stats.liquidity, chain.usdtDecimals);
            const deposits = Number(stats.commitmentCount);

            statusEl.classList.add('online');
            poolEl.innerHTML = `Pool: ${parseFloat(liquidity).toLocaleString()} USDT<br><small>${deposits} deposits</small>`;

        } catch (error) {
            console.error(`Failed to load status for ${chain.key}:`, error);

            // Fallback: check USDT balance directly
            try {
                const rpcProvider = new ethers.JsonRpcProvider(chain.rpc);
                const usdtContract = new ethers.Contract(chain.usdt, ERC20_ABI, rpcProvider);
                const poolBalance = await usdtContract.balanceOf(chain.bridge);
                const formatted = ethers.formatUnits(poolBalance, chain.usdtDecimals);

                statusEl.classList.add('online');
                poolEl.textContent = `Pool: ${parseFloat(formatted).toLocaleString()} USDT`;
            } catch (e2) {
                statusEl.classList.add('offline');
                poolEl.textContent = 'Offline';
            }
        }
    }

    // Mark non-deployed chains
    Object.entries(CHAIN_CONFIG).forEach(([key, config]) => {
        if (!config.deployed) {
            const statusEl = document.getElementById(`status-${key}`);
            const poolEl = document.getElementById(`pool-${key}`);
            if (statusEl) statusEl.classList.add('pending');
            if (poolEl) poolEl.textContent = 'Not deployed';
        }
    });
}
