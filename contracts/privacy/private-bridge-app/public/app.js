// Private Bridge - Main Application
let provider = null;
let signer = null;
let userAddress = null;
let currentChainId = null;
let selectedShieldChain = 'polygon';
let selectedTransferDestChain = 'altcoinchain';
let selectedUnshieldChain = 'polygon';
let selectedToken = 'usdt';
let notes = [];

// Initialize
document.addEventListener('DOMContentLoaded', () => {
    loadNotes();
    initTabs();
    initChainSelectors();
    initEventListeners();
    checkWalletConnection();
});

// Tab Navigation
function initTabs() {
    document.querySelectorAll('.tab').forEach(tab => {
        tab.addEventListener('click', () => {
            const tabId = tab.dataset.tab;
            document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
            document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));
            tab.classList.add('active');
            document.getElementById(`${tabId}-tab`).classList.add('active');
        });
    });
}

// Chain Selectors
function initChainSelectors() {
    renderChainSelector('shield-chain-selector', selectedShieldChain, (chain) => {
        selectedShieldChain = chain;
        onShieldChainChange();
    });

    renderChainSelector('transfer-dest-chain-selector', selectedTransferDestChain, (chain) => {
        selectedTransferDestChain = chain;
        onTransferDestChainChange();
    });

    renderChainSelector('unshield-chain-selector', selectedUnshieldChain, (chain) => {
        selectedUnshieldChain = chain;
        onUnshieldChainChange();
    });
}

function renderChainSelector(containerId, selected, onSelect) {
    const container = document.getElementById(containerId);
    container.innerHTML = '';

    getAllChains().forEach(([key, chain]) => {
        const div = document.createElement('div');
        div.className = `chain-option ${key === selected ? 'active' : ''} ${!chain.deployed ? 'disabled' : ''}`;
        div.innerHTML = `
            <div class="chain-icon" style="background: ${chain.color}20; color: ${chain.color}">${chain.icon}</div>
            <span>${chain.shortName}</span>
        `;

        if (chain.deployed) {
            div.addEventListener('click', () => {
                container.querySelectorAll('.chain-option').forEach(o => o.classList.remove('active'));
                div.classList.add('active');
                onSelect(key);
            });
        }

        container.appendChild(div);
    });
}

// Event Listeners
function initEventListeners() {
    // Wallet
    document.getElementById('connect-wallet').addEventListener('click', connectWallet);
    document.getElementById('disconnect-btn').addEventListener('click', disconnectWallet);

    // Shield Tab
    document.getElementById('shield-token').addEventListener('change', (e) => {
        selectedToken = e.target.value;
        updateTokenBalance();
    });
    document.getElementById('shield-amount').addEventListener('input', updateShieldFees);
    document.getElementById('shield-max').addEventListener('click', setMaxAmount);
    document.querySelectorAll('.preset').forEach(btn => {
        btn.addEventListener('click', () => {
            document.getElementById('shield-amount').value = btn.dataset.amount;
            updateShieldFees();
        });
    });
    document.getElementById('approve-btn').addEventListener('click', approveTokens);
    document.getElementById('shield-btn').addEventListener('click', shieldTokens);

    // Transfer Tab
    document.getElementById('transfer-note').addEventListener('input', parseTransferNote);
    document.getElementById('load-transfer-note').addEventListener('click', () => {
        document.getElementById('transfer-note-file').click();
    });
    document.getElementById('transfer-note-file').addEventListener('change', loadTransferNoteFile);
    document.getElementById('use-my-address').addEventListener('click', () => {
        document.getElementById('transfer-recipient').value = userAddress || '';
    });
    document.getElementById('transfer-amount').addEventListener('input', updateTransferFees);
    document.getElementById('transfer-btn').addEventListener('click', transferTokens);

    // Unshield Tab
    document.getElementById('unshield-note').addEventListener('input', parseUnshieldNote);
    document.getElementById('load-unshield-note').addEventListener('click', () => {
        document.getElementById('unshield-note-file').click();
    });
    document.getElementById('unshield-note-file').addEventListener('change', loadUnshieldNoteFile);
    document.getElementById('use-my-address-unshield').addEventListener('click', () => {
        document.getElementById('unshield-recipient').value = userAddress || '';
    });
    document.getElementById('unshield-amount').addEventListener('input', updateUnshieldFees);
    document.getElementById('unshield-btn').addEventListener('click', unshieldTokens);

    // Notes Tab
    document.getElementById('import-note-btn').addEventListener('click', () => {
        document.getElementById('import-note-file').click();
    });
    document.getElementById('import-note-file').addEventListener('change', importNoteFile);
    document.getElementById('export-all-btn').addEventListener('click', exportAllNotes);

    // Modals
    document.getElementById('modal-close').addEventListener('click', closeModal);
    document.getElementById('note-modal-close').addEventListener('click', closeNoteModal);
    document.getElementById('copy-note-btn').addEventListener('click', copyNewNote);
    document.getElementById('download-note-btn').addEventListener('click', downloadNewNote);
    document.getElementById('pool-modal-close').addEventListener('click', () => {
        document.getElementById('pool-modal').classList.add('hidden');
    });
    document.querySelectorAll('.modal-backdrop').forEach(b => {
        b.addEventListener('click', (e) => {
            e.target.closest('.modal').classList.add('hidden');
        });
    });

    // Footer links
    document.getElementById('show-pools').addEventListener('click', showPoolStatus);

    // Ethereum events
    if (window.ethereum) {
        window.ethereum.on('accountsChanged', handleAccountsChanged);
        window.ethereum.on('chainChanged', handleChainChanged);
    }
}

// Wallet Connection
async function connectWallet() {
    if (!window.ethereum) {
        alert('Please install MetaMask or another Web3 wallet');
        return;
    }

    try {
        provider = new ethers.BrowserProvider(window.ethereum);
        const accounts = await provider.send('eth_requestAccounts', []);
        signer = await provider.getSigner();
        userAddress = accounts[0];

        const network = await provider.getNetwork();
        currentChainId = Number(network.chainId);

        updateWalletUI();
        updateTokenBalance();
    } catch (error) {
        console.error('Connection failed:', error);
        alert('Failed to connect: ' + error.message);
    }
}

function disconnectWallet() {
    provider = null;
    signer = null;
    userAddress = null;
    currentChainId = null;

    document.getElementById('connect-wallet').classList.remove('hidden');
    document.getElementById('wallet-connected').classList.add('hidden');
    document.getElementById('network-badge').classList.add('hidden');
    document.getElementById('token-balance').textContent = '--';
}

async function checkWalletConnection() {
    if (window.ethereum) {
        try {
            provider = new ethers.BrowserProvider(window.ethereum);
            const accounts = await provider.send('eth_accounts', []);
            if (accounts.length > 0) {
                signer = await provider.getSigner();
                userAddress = accounts[0];
                const network = await provider.getNetwork();
                currentChainId = Number(network.chainId);
                updateWalletUI();
                updateTokenBalance();
            }
        } catch (e) {
            console.log('Not connected');
        }
    }
}

function updateWalletUI() {
    document.getElementById('connect-wallet').classList.add('hidden');
    document.getElementById('wallet-connected').classList.remove('hidden');
    document.getElementById('wallet-address').textContent = shortenAddress(userAddress);

    const chainEntry = getChainById(currentChainId);
    if (chainEntry) {
        document.getElementById('network-badge').classList.remove('hidden');
        document.getElementById('network-name').textContent = chainEntry[1].shortName;
    }
}

function handleAccountsChanged(accounts) {
    if (accounts.length === 0) {
        disconnectWallet();
    } else {
        userAddress = accounts[0];
        updateWalletUI();
        updateTokenBalance();
    }
}

function handleChainChanged(chainIdHex) {
    currentChainId = parseInt(chainIdHex, 16);
    updateWalletUI();
    updateTokenBalance();
}

async function switchToChain(chainKey) {
    const chain = CHAINS[chainKey];
    if (!chain) return false;

    const chainIdHex = '0x' + chain.id.toString(16);

    try {
        await window.ethereum.request({
            method: 'wallet_switchEthereumChain',
            params: [{ chainId: chainIdHex }]
        });
        return true;
    } catch (error) {
        if (error.code === 4902) {
            try {
                await window.ethereum.request({
                    method: 'wallet_addEthereumChain',
                    params: [{
                        chainId: chainIdHex,
                        chainName: chain.name,
                        nativeCurrency: { name: chain.shortName, symbol: chain.shortName, decimals: 18 },
                        rpcUrls: [chain.rpc],
                        blockExplorerUrls: [chain.explorer]
                    }]
                });
                return true;
            } catch (e) {
                console.error('Failed to add chain:', e);
            }
        }
        return false;
    }
}

// Shield Functions
function onShieldChainChange() {
    updateTokenBalance();
    updateShieldFees();
}

async function updateTokenBalance() {
    if (!userAddress || !selectedShieldChain) return;

    const chain = CHAINS[selectedShieldChain];
    const token = chain.tokens[selectedToken];
    if (!token) {
        document.getElementById('token-balance').textContent = 'N/A';
        return;
    }

    try {
        const rpcProvider = new ethers.JsonRpcProvider(chain.rpc);
        const tokenContract = new ethers.Contract(token.address, ABIS.erc20, rpcProvider);
        const balance = await tokenContract.balanceOf(userAddress);
        document.getElementById('token-balance').textContent =
            formatAmount(balance, token.decimals) + ' ' + selectedToken.toUpperCase();
    } catch (e) {
        document.getElementById('token-balance').textContent = 'Error';
    }
}

function updateShieldFees() {
    const amount = parseFloat(document.getElementById('shield-amount').value) || 0;
    const fee = amount * FEES.shield / 10000;
    const receive = amount - fee;

    document.getElementById('shield-fee').textContent = fee.toFixed(4) + ' ' + selectedToken.toUpperCase();
    document.getElementById('shield-receive').textContent = receive.toFixed(4) + ' ' + selectedToken.toUpperCase();

    document.getElementById('shield-btn').disabled = amount <= 0;
}

async function setMaxAmount() {
    if (!userAddress) return;

    const chain = CHAINS[selectedShieldChain];
    const token = chain.tokens[selectedToken];

    try {
        const rpcProvider = new ethers.JsonRpcProvider(chain.rpc);
        const tokenContract = new ethers.Contract(token.address, ABIS.erc20, rpcProvider);
        const balance = await tokenContract.balanceOf(userAddress);
        document.getElementById('shield-amount').value = ethers.formatUnits(balance, token.decimals);
        updateShieldFees();
    } catch (e) {
        console.error('Failed to get max:', e);
    }
}

async function approveTokens() {
    const chain = CHAINS[selectedShieldChain];
    const token = chain.tokens[selectedToken];
    const amount = document.getElementById('shield-amount').value;

    if (!amount || parseFloat(amount) <= 0) {
        alert('Enter amount first');
        return;
    }

    if (currentChainId !== chain.id) {
        const switched = await switchToChain(selectedShieldChain);
        if (!switched) {
            alert(`Please switch to ${chain.name}`);
            return;
        }
        await new Promise(r => setTimeout(r, 1000));
        provider = new ethers.BrowserProvider(window.ethereum);
        signer = await provider.getSigner();
    }

    showModal('Approving Tokens', 'Please confirm in your wallet...');

    try {
        const tokenContract = new ethers.Contract(token.address, ABIS.erc20, signer);
        const amountWei = ethers.parseUnits(amount, token.decimals);

        const tx = await tokenContract.approve(chain.bridge, amountWei);
        updateModalMessage('Waiting for confirmation...');
        showTxLink(chain.explorer, tx.hash);

        await tx.wait();
        updateModalMessage('Approved!');
        document.getElementById('approve-btn').textContent = 'Approved ✓';
        document.getElementById('shield-btn').disabled = false;

        setTimeout(closeModal, 1500);
    } catch (error) {
        updateModalMessage('Failed: ' + (error.reason || error.message));
        showModalClose();
    }
}

async function shieldTokens() {
    const chain = CHAINS[selectedShieldChain];
    const token = chain.tokens[selectedToken];
    const amount = document.getElementById('shield-amount').value;

    if (currentChainId !== chain.id) {
        alert(`Please switch to ${chain.name}`);
        return;
    }

    showModal('Shielding Tokens', 'Generating commitment...');

    try {
        // Generate random nullifier and secret
        const nullifier = ethers.randomBytes(31);
        const secret = ethers.randomBytes(31);
        const preimage = ethers.concat([nullifier, secret]);
        const commitment = ethers.keccak256(preimage);

        updateModalMessage('Please confirm deposit...');

        const bridgeContract = new ethers.Contract(chain.bridge, ABIS.bridge, signer);
        const amountWei = ethers.parseUnits(amount, token.decimals);

        const tx = await bridgeContract.deposit(commitment, amountWei, { gasLimit: 500000 });
        updateModalMessage('Waiting for confirmation...');
        showTxLink(chain.explorer, tx.hash);

        const receipt = await tx.wait();

        // Get leaf index from event
        let leafIndex = 0;
        for (const log of receipt.logs) {
            try {
                const parsed = bridgeContract.interface.parseLog(log);
                if (parsed && parsed.name === 'Deposit') {
                    leafIndex = Number(parsed.args.leafIndex);
                    break;
                }
            } catch (e) {}
        }

        // Create note
        const note = {
            version: 1,
            chain: selectedShieldChain,
            chainId: chain.id,
            token: selectedToken,
            tokenAddress: token.address,
            tokenDecimals: token.decimals,
            amount: amount,
            nullifier: ethers.hexlify(nullifier),
            secret: ethers.hexlify(secret),
            commitment: commitment,
            leafIndex: leafIndex,
            depositTx: tx.hash,
            timestamp: Date.now(),
            spent: false
        };

        // Save note
        saveNote(note);
        renderNotes();

        closeModal();
        showNoteCreated(note);

    } catch (error) {
        console.error('Shield failed:', error);
        updateModalMessage('Failed: ' + (error.reason || error.message));
        showModalClose();
    }
}

// Transfer Functions
function parseTransferNote() {
    const noteText = document.getElementById('transfer-note').value.trim();
    parseNoteAndShowInfo(noteText, 'note-info', 'note-chain', 'note-amount', 'note-status', 'transfer-btn');
    updateTransferFees();
}

function loadTransferNoteFile(e) {
    const file = e.target.files[0];
    if (!file) return;
    const reader = new FileReader();
    reader.onload = (ev) => {
        document.getElementById('transfer-note').value = ev.target.result;
        parseTransferNote();
    };
    reader.readAsText(file);
}

function onTransferDestChainChange() {
    loadDestPoolLiquidity();
    updateTransferFees();
}

async function loadDestPoolLiquidity() {
    const chain = CHAINS[selectedTransferDestChain];
    if (!chain || !chain.deployed) {
        document.getElementById('dest-pool-liquidity').textContent = 'Not deployed';
        return;
    }

    try {
        const rpcProvider = new ethers.JsonRpcProvider(chain.rpc);
        const bridgeContract = new ethers.Contract(chain.bridge, ABIS.bridge, rpcProvider);
        const liquidity = await bridgeContract.getLiquidity();
        document.getElementById('dest-pool-liquidity').textContent =
            formatAmount(liquidity, 6) + ' USDT';
    } catch (e) {
        document.getElementById('dest-pool-liquidity').textContent = 'Error loading';
    }
}

function updateTransferFees() {
    const amount = parseFloat(document.getElementById('transfer-amount').value) || 0;
    const fee = amount * FEES.bridge / 10000;
    const receive = amount - fee;

    document.getElementById('transfer-fee').textContent = fee.toFixed(4);
    document.getElementById('transfer-receive').textContent = receive.toFixed(4);
}

async function transferTokens() {
    const noteText = document.getElementById('transfer-note').value.trim();
    const recipient = document.getElementById('transfer-recipient').value.trim();
    const amount = document.getElementById('transfer-amount').value;

    if (!noteText || !recipient || !amount) {
        alert('Please fill all fields');
        return;
    }

    const note = JSON.parse(noteText);
    const destChain = CHAINS[selectedTransferDestChain];

    if (currentChainId !== destChain.id) {
        const switched = await switchToChain(selectedTransferDestChain);
        if (!switched) {
            alert(`Please switch to ${destChain.name}`);
            return;
        }
        await new Promise(r => setTimeout(r, 1000));
        provider = new ethers.BrowserProvider(window.ethereum);
        signer = await provider.getSigner();
    }

    await performWithdrawal(note, destChain, recipient, amount);
}

// Unshield Functions
function parseUnshieldNote() {
    const noteText = document.getElementById('unshield-note').value.trim();
    parseNoteAndShowInfo(noteText, 'unshield-note-info', 'unshield-note-chain', 'unshield-note-amount', 'unshield-note-status', 'unshield-btn');

    if (noteText) {
        try {
            const note = JSON.parse(noteText);
            document.getElementById('unshield-amount').value = note.amount;
            updateUnshieldFees();
        } catch (e) {}
    }
}

function loadUnshieldNoteFile(e) {
    const file = e.target.files[0];
    if (!file) return;
    const reader = new FileReader();
    reader.onload = (ev) => {
        document.getElementById('unshield-note').value = ev.target.result;
        parseUnshieldNote();
    };
    reader.readAsText(file);
}

function onUnshieldChainChange() {
    loadUnshieldPoolLiquidity();
    updateUnshieldFees();
}

async function loadUnshieldPoolLiquidity() {
    const chain = CHAINS[selectedUnshieldChain];
    if (!chain || !chain.deployed) {
        document.getElementById('unshield-pool-liquidity').textContent = 'Not deployed';
        return;
    }

    try {
        const rpcProvider = new ethers.JsonRpcProvider(chain.rpc);
        const bridgeContract = new ethers.Contract(chain.bridge, ABIS.bridge, rpcProvider);
        const liquidity = await bridgeContract.getLiquidity();
        document.getElementById('unshield-pool-liquidity').textContent =
            formatAmount(liquidity, 6) + ' USDT';
    } catch (e) {
        document.getElementById('unshield-pool-liquidity').textContent = 'Error loading';
    }
}

function updateUnshieldFees() {
    const amount = parseFloat(document.getElementById('unshield-amount').value) || 0;
    const fee = amount * FEES.unshield / 10000;
    const receive = amount - fee;

    document.getElementById('unshield-fee').textContent = fee.toFixed(4);
    document.getElementById('unshield-receive').textContent = receive.toFixed(4);
}

async function unshieldTokens() {
    const noteText = document.getElementById('unshield-note').value.trim();
    const recipient = document.getElementById('unshield-recipient').value.trim();
    const amount = document.getElementById('unshield-amount').value;

    if (!noteText || !recipient || !amount) {
        alert('Please fill all fields');
        return;
    }

    const note = JSON.parse(noteText);
    const chain = CHAINS[selectedUnshieldChain];

    if (currentChainId !== chain.id) {
        const switched = await switchToChain(selectedUnshieldChain);
        if (!switched) {
            alert(`Please switch to ${chain.name}`);
            return;
        }
        await new Promise(r => setTimeout(r, 1000));
        provider = new ethers.BrowserProvider(window.ethereum);
        signer = await provider.getSigner();
    }

    await performWithdrawal(note, chain, recipient, amount);
}

// Common Withdrawal Logic
async function performWithdrawal(note, destChain, recipient, amount) {
    showModal('Processing Withdrawal', 'Checking pool liquidity...');

    try {
        const bridgeContract = new ethers.Contract(destChain.bridge, ABIS.bridge, signer);

        // Check liquidity
        const liquidity = await bridgeContract.getLiquidity();
        const amountWei = ethers.parseUnits(amount, note.tokenDecimals || 6);

        if (liquidity < amountWei) {
            updateModalMessage(`Insufficient liquidity. Pool has ${formatAmount(liquidity, 6)} USDT`);
            showModalClose();
            return;
        }

        // Get root
        const root = await bridgeContract.getLastRoot();

        // Generate nullifier hash
        const nullifierHash = ethers.keccak256(note.nullifier);

        // Check if spent
        const isSpent = await bridgeContract.nullifiers(nullifierHash);
        if (isSpent) {
            updateModalMessage('This note has already been spent!');
            showModalClose();
            return;
        }

        updateModalMessage('Please confirm withdrawal...');

        // Dummy proof (verifier disabled)
        const proof = ethers.hexlify(new Uint8Array(256));

        const tx = await bridgeContract.withdraw(
            proof,
            root,
            nullifierHash,
            recipient,
            amountWei,
            { gasLimit: 350000 }
        );

        updateModalMessage('Waiting for confirmation...');
        showTxLink(destChain.explorer, tx.hash);

        await tx.wait();

        // Mark note as spent
        markNoteSpent(note.commitment);
        renderNotes();

        updateModalMessage('Withdrawal successful!');
        showModalClose();

    } catch (error) {
        console.error('Withdrawal failed:', error);
        updateModalMessage('Failed: ' + (error.reason || error.message));
        showModalClose();
    }
}

// Note Management
function loadNotes() {
    const stored = localStorage.getItem('private_bridge_notes');
    notes = stored ? JSON.parse(stored) : [];
    renderNotes();
}

function saveNote(note) {
    notes.push(note);
    localStorage.setItem('private_bridge_notes', JSON.stringify(notes));
}

function markNoteSpent(commitment) {
    const idx = notes.findIndex(n => n.commitment === commitment);
    if (idx >= 0) {
        notes[idx].spent = true;
        localStorage.setItem('private_bridge_notes', JSON.stringify(notes));
    }
}

function renderNotes() {
    const container = document.getElementById('notes-list');

    if (notes.length === 0) {
        container.innerHTML = `
            <div class="empty-state">
                <svg width="48" height="48" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5">
                    <path d="M14 2H6a2 2 0 00-2 2v16a2 2 0 002 2h12a2 2 0 002-2V8z"/>
                    <polyline points="14,2 14,8 20,8"/>
                </svg>
                <p>No notes yet</p>
                <small>Shield some tokens to create your first note</small>
            </div>
        `;
        return;
    }

    container.innerHTML = notes.map((note, idx) => {
        const chain = CHAINS[note.chain];
        return `
            <div class="note-card ${note.spent ? 'spent' : ''}">
                <div class="note-card-icon" style="background: ${chain?.color || '#666'}20; color: ${chain?.color || '#666'}">
                    ${chain?.icon || '?'}
                </div>
                <div class="note-card-info">
                    <h4>${note.amount} ${(note.token || 'USDT').toUpperCase()}</h4>
                    <p>${chain?.name || note.chain} • ${note.spent ? 'Spent' : 'Active'}</p>
                </div>
                <div class="note-card-actions">
                    <button class="btn-icon" onclick="copyNoteAtIndex(${idx})" title="Copy">
                        <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                            <rect x="9" y="9" width="13" height="13" rx="2"/>
                            <path d="M5 15H4a2 2 0 01-2-2V4a2 2 0 012-2h9a2 2 0 012 2v1"/>
                        </svg>
                    </button>
                    <button class="btn-icon" onclick="downloadNoteAtIndex(${idx})" title="Download">
                        <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                            <path d="M21 15v4a2 2 0 01-2 2H5a2 2 0 01-2-2v-4"/>
                            <polyline points="7,10 12,15 17,10"/>
                            <line x1="12" y1="15" x2="12" y2="3"/>
                        </svg>
                    </button>
                </div>
            </div>
        `;
    }).join('');
}

function copyNoteAtIndex(idx) {
    navigator.clipboard.writeText(JSON.stringify(notes[idx], null, 2));
    alert('Note copied!');
}

function downloadNoteAtIndex(idx) {
    const note = notes[idx];
    const blob = new Blob([JSON.stringify(note, null, 2)], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `private-note-${note.chain}-${Date.now()}.json`;
    a.click();
    URL.revokeObjectURL(url);
}

function importNoteFile(e) {
    const file = e.target.files[0];
    if (!file) return;
    const reader = new FileReader();
    reader.onload = (ev) => {
        try {
            const note = JSON.parse(ev.target.result);
            if (note.nullifier && note.secret && note.commitment) {
                if (!notes.find(n => n.commitment === note.commitment)) {
                    notes.push(note);
                    localStorage.setItem('private_bridge_notes', JSON.stringify(notes));
                    renderNotes();
                    alert('Note imported!');
                } else {
                    alert('Note already exists');
                }
            } else {
                alert('Invalid note format');
            }
        } catch (e) {
            alert('Failed to parse note: ' + e.message);
        }
    };
    reader.readAsText(file);
}

function exportAllNotes() {
    if (notes.length === 0) {
        alert('No notes to export');
        return;
    }
    const blob = new Blob([JSON.stringify(notes, null, 2)], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `private-bridge-notes-backup-${Date.now()}.json`;
    a.click();
    URL.revokeObjectURL(url);
}

// Helper: Parse note and show info
function parseNoteAndShowInfo(noteText, infoId, chainId, amountId, statusId, btnId) {
    const infoEl = document.getElementById(infoId);

    if (!noteText) {
        infoEl.classList.add('hidden');
        document.getElementById(btnId).disabled = true;
        return;
    }

    try {
        const note = JSON.parse(noteText);
        if (!note.nullifier || !note.secret) throw new Error('Invalid');

        const chain = CHAINS[note.chain];
        document.getElementById(chainId).textContent = chain?.name || note.chain;
        document.getElementById(amountId).textContent = note.amount + ' ' + (note.token || 'USDT').toUpperCase();
        document.getElementById(statusId).textContent = note.spent ? 'Spent' : 'Active';
        document.getElementById(statusId).style.color = note.spent ? 'var(--error)' : 'var(--success)';

        infoEl.classList.remove('hidden');
        document.getElementById(btnId).disabled = note.spent;

    } catch (e) {
        document.getElementById(chainId).textContent = '--';
        document.getElementById(amountId).textContent = '--';
        document.getElementById(statusId).textContent = 'Invalid';
        document.getElementById(statusId).style.color = 'var(--error)';
        infoEl.classList.remove('hidden');
        document.getElementById(btnId).disabled = true;
    }
}

// Modal Functions
function showModal(title, message) {
    document.getElementById('modal-title').textContent = title;
    document.getElementById('modal-message').textContent = message;
    document.getElementById('modal-spinner').style.display = 'block';
    document.getElementById('modal-tx-link').classList.add('hidden');
    document.getElementById('modal-footer').classList.add('hidden');
    document.getElementById('modal').classList.remove('hidden');
}

function updateModalMessage(message) {
    document.getElementById('modal-message').textContent = message;
}

function showTxLink(explorer, hash) {
    const link = document.getElementById('modal-tx-link');
    link.href = `${explorer}/tx/${hash}`;
    link.classList.remove('hidden');
}

function showModalClose() {
    document.getElementById('modal-spinner').style.display = 'none';
    document.getElementById('modal-footer').classList.remove('hidden');
    document.getElementById('modal-action-btn').onclick = closeModal;
}

function closeModal() {
    document.getElementById('modal').classList.add('hidden');
}

function showNoteCreated(note) {
    document.getElementById('new-note-text').value = JSON.stringify(note, null, 2);
    document.getElementById('note-modal').classList.remove('hidden');
}

function closeNoteModal() {
    document.getElementById('note-modal').classList.add('hidden');
}

function copyNewNote() {
    navigator.clipboard.writeText(document.getElementById('new-note-text').value);
    document.getElementById('copy-note-btn').innerHTML = `
        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
            <path d="M20 6L9 17l-5-5"/>
        </svg>
        Copied!
    `;
}

function downloadNewNote() {
    const noteText = document.getElementById('new-note-text').value;
    const note = JSON.parse(noteText);
    const blob = new Blob([noteText], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `private-note-${note.chain}-${Date.now()}.json`;
    a.click();
    URL.revokeObjectURL(url);
}

// Pool Status
async function showPoolStatus(e) {
    e.preventDefault();
    const container = document.getElementById('pool-status-grid');
    container.innerHTML = '<p>Loading...</p>';
    document.getElementById('pool-modal').classList.remove('hidden');

    let html = '';
    for (const [key, chain] of Object.entries(CHAINS)) {
        let status = 'offline';
        let liquidity = 'N/A';
        let deposits = 'N/A';

        if (chain.deployed) {
            try {
                const rpcProvider = new ethers.JsonRpcProvider(chain.rpc);
                const bridge = new ethers.Contract(chain.bridge, ABIS.bridge, rpcProvider);
                const liq = await bridge.getLiquidity();
                const idx = await bridge.nextIndex();
                liquidity = formatAmount(liq, 6) + ' USDT';
                deposits = idx.toString();
                status = 'online';
            } catch (e) {
                status = 'offline';
            }
        }

        html += `
            <div class="pool-card">
                <h4>
                    <span class="status-dot ${status}"></span>
                    ${chain.name}
                </h4>
                <div class="liquidity">${liquidity}</div>
                <div class="deposits">${deposits !== 'N/A' ? deposits + ' deposits' : 'Not deployed'}</div>
            </div>
        `;
    }

    container.innerHTML = html;
}

// Initial load
onShieldChainChange();
onTransferDestChainChange();
onUnshieldChainChange();
