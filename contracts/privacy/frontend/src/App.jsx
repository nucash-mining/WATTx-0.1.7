import React, { useState, useEffect } from 'react';
import { ethers } from 'ethers';
import { Toaster, toast } from 'react-hot-toast';
import { FiZap, FiLogOut, FiLock, FiUnlock, FiArrowRight } from 'react-icons/fi';

// Contract addresses
const CHAINS = {
  sepolia: {
    id: 11155111,
    name: 'Ethereum Sepolia',
    shortName: 'Sepolia',
    color: '#627EEA',
    rpcUrl: 'https://ethereum-sepolia-rpc.publicnode.com',
    explorer: 'https://sepolia.etherscan.io',
    contracts: {
      mockUSDT: '0x31D69920F5b500bc54103288C5E6aB88bfA3675c',
      privacyPoolBridge: '0xEcf50a033B4c104b9F1938bac54cA59fcC819606',
    },
  },
  altcoinchain: {
    id: 2330,
    name: 'Altcoinchain',
    shortName: 'ALT',
    color: '#F7931A',
    rpcUrl: 'https://alt-rpc2.minethepla.net',
    explorer: 'https://altcoinchain.org/explorer',
    contracts: {
      mockUSDT: '0xB538B48C1BC3A6C32e12Af29B5894B0f904f8991',
      privacyPoolBridge: '0x0E6632A37099C11113Bd31Aa187B69b1729d2AB3',
    },
  },
};

const ERC20_ABI = [
  'function balanceOf(address owner) view returns (uint256)',
  'function approve(address spender, uint256 amount) returns (bool)',
  'function allowance(address owner, address spender) view returns (uint256)',
];

const POOL_ABI = [
  'function deposit(bytes32 commitment, uint256 amount)',
  'function getStats() view returns (uint256 deposited, uint256 withdrawn, uint256 bridgedIn, uint256 commitmentCount, bytes32 currentRoot, uint256 liquidity)',
];

const DENOMINATIONS = [
  { label: '100 USDT', value: '100000000' },
  { label: '1,000 USDT', value: '1000000000' },
  { label: '10,000 USDT', value: '10000000000' },
  { label: '100,000 USDT', value: '100000000000' },
];

function shortenAddress(address) {
  if (!address) return '';
  return `${address.slice(0, 6)}...${address.slice(-4)}`;
}

function formatUSDT(amount) {
  return (Number(amount) / 1e6).toLocaleString();
}

function generateCommitment() {
  const secret = ethers.randomBytes(32);
  const nullifier = ethers.randomBytes(32);
  const commitment = ethers.keccak256(ethers.concat([secret, nullifier]));
  return {
    secret: ethers.hexlify(secret),
    nullifier: ethers.hexlify(nullifier),
    nullifierHash: ethers.keccak256(nullifier),
    commitment,
  };
}

function App() {
  const [address, setAddress] = useState('');
  const [chainId, setChainId] = useState(null);
  const [signer, setSigner] = useState(null);
  const [sourceChain, setSourceChain] = useState('sepolia');
  const [destChain, setDestChain] = useState('altcoinchain');
  const [amount, setAmount] = useState(DENOMINATIONS[0].value);
  const [loading, setLoading] = useState(false);
  const [stats, setStats] = useState(null);
  const [usdtBalance, setUsdtBalance] = useState('0');

  const chain = CHAINS[sourceChain];
  const isCorrectChain = chainId === chain?.id;

  // Connect wallet
  const connectWallet = async () => {
    if (!window.ethereum) {
      toast.error('Please install MetaMask!');
      return;
    }
    try {
      const provider = new ethers.BrowserProvider(window.ethereum);
      await provider.send('eth_requestAccounts', []);
      const signer = await provider.getSigner();
      const address = await signer.getAddress();
      const network = await provider.getNetwork();
      setSigner(signer);
      setAddress(address);
      setChainId(Number(network.chainId));
      toast.success('Wallet connected!');
    } catch (error) {
      console.error(error);
      toast.error('Failed to connect');
    }
  };

  // Switch network
  const switchNetwork = async (targetChainId) => {
    try {
      await window.ethereum.request({
        method: 'wallet_switchEthereumChain',
        params: [{ chainId: `0x${targetChainId.toString(16)}` }],
      });
    } catch (e) {
      if (e.code === 4902) {
        const c = Object.values(CHAINS).find(x => x.id === targetChainId);
        await window.ethereum.request({
          method: 'wallet_addEthereumChain',
          params: [{
            chainId: `0x${targetChainId.toString(16)}`,
            chainName: c.name,
            rpcUrls: [c.rpcUrl],
            nativeCurrency: { name: c.shortName, symbol: c.shortName, decimals: 18 },
          }],
        });
      }
    }
  };

  // Fetch balances and stats
  useEffect(() => {
    const fetchData = async () => {
      if (!chain?.contracts?.mockUSDT) return;
      try {
        const rpc = new ethers.JsonRpcProvider(chain.rpcUrl);
        const pool = new ethers.Contract(chain.contracts.privacyPoolBridge, POOL_ABI, rpc);
        const s = await pool.getStats();
        setStats({
          deposited: s.deposited.toString(),
          liquidity: s.liquidity.toString(),
          commitments: s.commitmentCount.toString(),
        });

        if (signer && isCorrectChain) {
          const usdt = new ethers.Contract(chain.contracts.mockUSDT, ERC20_ABI, signer);
          const bal = await usdt.balanceOf(address);
          setUsdtBalance(bal.toString());
        }
      } catch (e) {
        console.error('Fetch error:', e);
      }
    };
    fetchData();
  }, [sourceChain, signer, chainId, address]);

  // Handle chain change
  useEffect(() => {
    if (!window.ethereum) return;
    const handleChainChanged = (id) => setChainId(parseInt(id, 16));
    window.ethereum.on('chainChanged', handleChainChanged);
    return () => window.ethereum.removeListener('chainChanged', handleChainChanged);
  }, []);

  // Deposit
  const handleDeposit = async () => {
    if (!signer) return;
    setLoading(true);
    try {
      const { commitment, secret, nullifier, nullifierHash } = generateCommitment();
      const usdt = new ethers.Contract(chain.contracts.mockUSDT, ERC20_ABI, signer);
      const pool = new ethers.Contract(chain.contracts.privacyPoolBridge, POOL_ABI, signer);

      toast.loading('Approving...', { id: 'tx' });
      const approveTx = await usdt.approve(chain.contracts.privacyPoolBridge, amount);
      await approveTx.wait();

      toast.loading('Depositing...', { id: 'tx' });
      const depositTx = await pool.deposit(commitment, amount);
      const receipt = await depositTx.wait();

      // Save note
      const note = { commitment, secret, nullifier, nullifierHash, amount, chainId: chain.id, chainName: sourceChain, timestamp: Date.now() };
      const notes = JSON.parse(localStorage.getItem('wattx_notes') || '[]');
      notes.push(note);
      localStorage.setItem('wattx_notes', JSON.stringify(notes));

      // Download note
      const blob = new Blob([JSON.stringify(note, null, 2)], { type: 'application/json' });
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      a.download = `wattx-note-${Date.now()}.json`;
      a.click();

      toast.success('Deposit successful! Note downloaded.', { id: 'tx' });
    } catch (e) {
      console.error(e);
      toast.error(e.reason || 'Deposit failed', { id: 'tx' });
    }
    setLoading(false);
  };

  return (
    <div className="min-h-screen bg-gradient-to-br from-gray-900 via-gray-800 to-gray-900 text-white">
      <Toaster position="top-right" toastOptions={{ style: { background: '#1f2937', color: '#fff' } }} />

      {/* Header */}
      <header className="border-b border-gray-700 p-4">
        <div className="max-w-6xl mx-auto flex justify-between items-center">
          <div className="flex items-center gap-2">
            <div className="w-10 h-10 bg-green-500 rounded-lg flex items-center justify-center">
              <FiZap className="w-6 h-6" />
            </div>
            <div>
              <h1 className="font-bold text-xl">WATTx Bridge</h1>
              <p className="text-xs text-gray-400">Privacy Protocol</p>
            </div>
          </div>

          {address ? (
            <div className="flex items-center gap-3">
              <span className="px-3 py-1 bg-gray-800 rounded-lg text-sm" style={{ borderLeft: `3px solid ${CHAINS[sourceChain]?.color}` }}>
                {CHAINS[sourceChain]?.shortName}
              </span>
              <span className="px-3 py-2 bg-gray-800 rounded-lg font-mono text-sm">
                {shortenAddress(address)}
              </span>
              <button onClick={() => { setAddress(''); setSigner(null); }} className="p-2 hover:bg-gray-700 rounded-lg">
                <FiLogOut />
              </button>
            </div>
          ) : (
            <button onClick={connectWallet} className="bg-green-500 hover:bg-green-600 px-4 py-2 rounded-lg font-semibold flex items-center gap-2">
              <FiZap /> Connect Wallet
            </button>
          )}
        </div>
      </header>

      {/* Main */}
      <main className="max-w-6xl mx-auto p-6">
        {/* Title */}
        <div className="text-center mb-10">
          <h1 className="text-4xl font-bold mb-3 bg-gradient-to-r from-green-400 to-emerald-500 bg-clip-text text-transparent">
            WATTx Privacy Bridge
          </h1>
          <p className="text-gray-400">Anonymous cross-chain transfers with zero-knowledge proofs</p>
        </div>

        {/* Chain Selector */}
        <div className="bg-gray-800/50 backdrop-blur rounded-xl p-6 mb-8 border border-gray-700">
          <div className="flex items-center justify-center gap-4 flex-wrap">
            <div className="flex-1 min-w-[180px]">
              <label className="text-sm text-gray-400 mb-1 block">From</label>
              <select
                value={sourceChain}
                onChange={(e) => setSourceChain(e.target.value)}
                className="w-full bg-gray-700 border border-gray-600 rounded-lg px-4 py-3"
              >
                <option value="sepolia">Ethereum Sepolia</option>
                <option value="altcoinchain">Altcoinchain</option>
              </select>
              {!isCorrectChain && address && (
                <button onClick={() => switchNetwork(chain.id)} className="mt-2 text-sm text-green-400 hover:underline">
                  Switch to {chain.shortName}
                </button>
              )}
            </div>

            <FiArrowRight className="w-6 h-6 text-gray-500 mt-6" />

            <div className="flex-1 min-w-[180px]">
              <label className="text-sm text-gray-400 mb-1 block">To</label>
              <select
                value={destChain}
                onChange={(e) => setDestChain(e.target.value)}
                className="w-full bg-gray-700 border border-gray-600 rounded-lg px-4 py-3"
              >
                <option value="sepolia">Ethereum Sepolia</option>
                <option value="altcoinchain">Altcoinchain</option>
              </select>
            </div>
          </div>
        </div>

        <div className="grid grid-cols-1 lg:grid-cols-3 gap-6">
          {/* Deposit Card */}
          <div className="lg:col-span-2 bg-gray-800/50 backdrop-blur rounded-xl p-6 border border-gray-700">
            <div className="flex items-center gap-3 mb-6">
              <div className="p-3 bg-green-500/20 rounded-lg">
                <FiLock className="w-6 h-6 text-green-400" />
              </div>
              <div>
                <h2 className="text-xl font-semibold">Deposit</h2>
                <p className="text-sm text-gray-400">Shield your USDT</p>
              </div>
            </div>

            {/* Amount Selection */}
            <div className="grid grid-cols-2 gap-3 mb-6">
              {DENOMINATIONS.map((d) => (
                <button
                  key={d.value}
                  onClick={() => setAmount(d.value)}
                  className={`p-4 rounded-lg border transition-all ${
                    amount === d.value
                      ? 'border-green-500 bg-green-500/20 text-green-400'
                      : 'border-gray-600 bg-gray-700 hover:border-gray-500'
                  }`}
                >
                  {d.label}
                </button>
              ))}
            </div>

            {/* Balance */}
            <div className="bg-gray-700/50 rounded-lg p-4 mb-6">
              <div className="flex justify-between text-sm">
                <span className="text-gray-400">Your Balance</span>
                <span>{formatUSDT(usdtBalance)} USDT</span>
              </div>
              <div className="flex justify-between text-sm mt-2">
                <span className="text-gray-400">Fee</span>
                <span>0.1%</span>
              </div>
            </div>

            {/* Warning */}
            <div className="bg-yellow-500/10 border border-yellow-500/30 rounded-lg p-4 mb-6">
              <p className="text-sm text-yellow-200">
                <strong>Important:</strong> Save your secret note after deposit. You need it to withdraw!
              </p>
            </div>

            {/* Button */}
            {!address ? (
              <button onClick={connectWallet} className="w-full bg-green-500 hover:bg-green-600 py-3 rounded-lg font-semibold">
                Connect Wallet
              </button>
            ) : !isCorrectChain ? (
              <button onClick={() => switchNetwork(chain.id)} className="w-full bg-green-500 hover:bg-green-600 py-3 rounded-lg font-semibold">
                Switch to {chain.shortName}
              </button>
            ) : (
              <button
                onClick={handleDeposit}
                disabled={loading || BigInt(usdtBalance) < BigInt(amount)}
                className="w-full bg-green-500 hover:bg-green-600 disabled:opacity-50 disabled:cursor-not-allowed py-3 rounded-lg font-semibold"
              >
                {loading ? 'Processing...' : BigInt(usdtBalance) < BigInt(amount) ? 'Insufficient Balance' : 'Deposit'}
              </button>
            )}
          </div>

          {/* Stats */}
          <div className="bg-gray-800/50 backdrop-blur rounded-xl p-6 border border-gray-700">
            <h3 className="font-semibold mb-4 flex items-center gap-2">
              <span className="w-3 h-3 rounded-full" style={{ backgroundColor: chain?.color }}></span>
              {chain?.shortName} Pool
            </h3>

            {stats ? (
              <div className="space-y-4">
                <div className="bg-gray-700/50 rounded-lg p-3">
                  <div className="text-sm text-gray-400">Liquidity</div>
                  <div className="text-xl font-semibold text-green-400">{formatUSDT(stats.liquidity)} USDT</div>
                </div>
                <div className="bg-gray-700/50 rounded-lg p-3">
                  <div className="text-sm text-gray-400">Total Deposited</div>
                  <div className="font-mono">{formatUSDT(stats.deposited)} USDT</div>
                </div>
                <div className="bg-gray-700/50 rounded-lg p-3">
                  <div className="text-sm text-gray-400">Deposits</div>
                  <div className="font-mono">{stats.commitments}</div>
                </div>
              </div>
            ) : (
              <div className="text-gray-500">Loading...</div>
            )}

            {chain?.explorer && (
              <a
                href={`${chain.explorer}/address/${chain.contracts.privacyPoolBridge}`}
                target="_blank"
                rel="noopener noreferrer"
                className="block mt-4 text-xs text-gray-500 hover:text-green-400 text-center"
              >
                View Contract
              </a>
            )}
          </div>
        </div>

        {/* Instructions */}
        <div className="mt-8 bg-gray-800/30 rounded-xl p-6 border border-gray-700">
          <h3 className="font-semibold mb-4">How to Withdraw</h3>
          <ol className="list-decimal list-inside space-y-2 text-gray-400 text-sm">
            <li>After depositing, a secret note file will be downloaded automatically</li>
            <li>Keep this note safe - it's the only way to withdraw your funds</li>
            <li>To withdraw, use the note on any supported chain (cross-chain supported)</li>
            <li>Your withdrawal address has no link to your deposit address</li>
          </ol>
        </div>
      </main>

      {/* Footer */}
      <footer className="border-t border-gray-700 mt-16 p-6 text-center text-gray-500 text-sm">
        <p>2026 WATTx Core | Testnet Only</p>
      </footer>
    </div>
  );
}

export default App;
