// SPDX-License-Identifier: MIT
pragma solidity ^0.8.19;

/**
 * @title AtomicSwap
 * @notice Hash Time-Locked Contract (HTLC) for WATTx-Monero atomic swaps
 *
 * Enables trustless cross-chain swaps between WATTx (WTX) and Monero (XMR)
 * using cryptographic hash locks and time locks.
 *
 * Swap Flow:
 * 1. Alice (WTX holder) creates HTLC on WATTx with secret hash
 * 2. Bob (XMR holder) creates corresponding HTLC on Monero
 * 3. Alice reveals secret to claim XMR from Bob
 * 4. Bob uses revealed secret to claim WTX from Alice
 * 5. If timeout, funds are refunded to original owners
 */
contract AtomicSwap {

    // ============================================================================
    // Events
    // ============================================================================

    event SwapCreated(
        bytes32 indexed swapId,
        address indexed sender,
        address indexed recipient,
        uint256 amount,
        bytes32 hashLock,
        uint256 timelock
    );

    event SwapClaimed(
        bytes32 indexed swapId,
        bytes32 preimage
    );

    event SwapRefunded(
        bytes32 indexed swapId
    );

    // ============================================================================
    // Enums
    // ============================================================================

    enum SwapState {
        Invalid,   // 0 - Does not exist
        Active,    // 1 - Created and waiting
        Claimed,   // 2 - Successfully claimed
        Refunded   // 3 - Timed out and refunded
    }

    // ============================================================================
    // Structs
    // ============================================================================

    struct Swap {
        address sender;           // WTX sender (Alice)
        address recipient;        // WTX recipient (Bob)
        uint256 amount;           // Amount of WTX locked
        bytes32 hashLock;         // SHA256 hash of the secret
        uint256 timelock;         // Expiration timestamp
        SwapState state;          // Current state
        bytes32 preimage;         // Secret (revealed on claim)
    }

    // ============================================================================
    // State Variables
    // ============================================================================

    mapping(bytes32 => Swap) public swaps;

    // Minimum and maximum timelock durations
    uint256 public constant MIN_TIMELOCK = 1 hours;
    uint256 public constant MAX_TIMELOCK = 48 hours;

    // ============================================================================
    // Main Functions
    // ============================================================================

    /**
     * @notice Create a new hash time-locked swap
     * @param recipient Address that can claim the funds with the secret
     * @param hashLock SHA256 hash of the secret preimage
     * @param timelock Unix timestamp when the swap expires
     * @return swapId Unique identifier for this swap
     */
    function createSwap(
        address recipient,
        bytes32 hashLock,
        uint256 timelock
    ) external payable returns (bytes32 swapId) {
        require(msg.value > 0, "Zero amount");
        require(recipient != address(0), "Invalid recipient");
        require(hashLock != bytes32(0), "Invalid hash lock");
        require(timelock > block.timestamp + MIN_TIMELOCK, "Timelock too short");
        require(timelock < block.timestamp + MAX_TIMELOCK, "Timelock too long");

        swapId = keccak256(abi.encodePacked(
            msg.sender,
            recipient,
            msg.value,
            hashLock,
            timelock,
            block.timestamp
        ));

        require(swaps[swapId].state == SwapState.Invalid, "Swap already exists");

        swaps[swapId] = Swap({
            sender: msg.sender,
            recipient: recipient,
            amount: msg.value,
            hashLock: hashLock,
            timelock: timelock,
            state: SwapState.Active,
            preimage: bytes32(0)
        });

        emit SwapCreated(swapId, msg.sender, recipient, msg.value, hashLock, timelock);

        return swapId;
    }

    /**
     * @notice Create a swap with a specific ID (for cross-chain coordination)
     * @param swapId The predetermined swap ID
     * @param recipient Address that can claim the funds with the secret
     * @param hashLock SHA256 hash of the secret preimage
     * @param timelock Unix timestamp when the swap expires
     */
    function createSwapWithId(
        bytes32 swapId,
        address recipient,
        bytes32 hashLock,
        uint256 timelock
    ) external payable {
        require(msg.value > 0, "Zero amount");
        require(recipient != address(0), "Invalid recipient");
        require(hashLock != bytes32(0), "Invalid hash lock");
        require(timelock > block.timestamp + MIN_TIMELOCK, "Timelock too short");
        require(timelock < block.timestamp + MAX_TIMELOCK, "Timelock too long");
        require(swaps[swapId].state == SwapState.Invalid, "Swap already exists");

        swaps[swapId] = Swap({
            sender: msg.sender,
            recipient: recipient,
            amount: msg.value,
            hashLock: hashLock,
            timelock: timelock,
            state: SwapState.Active,
            preimage: bytes32(0)
        });

        emit SwapCreated(swapId, msg.sender, recipient, msg.value, hashLock, timelock);
    }

    /**
     * @notice Claim the locked funds by revealing the secret preimage
     * @param swapId The swap to claim
     * @param preimage The secret that hashes to hashLock
     */
    function claim(bytes32 swapId, bytes32 preimage) external {
        Swap storage swap = swaps[swapId];

        require(swap.state == SwapState.Active, "Swap not active");
        require(sha256(abi.encodePacked(preimage)) == swap.hashLock, "Invalid preimage");
        require(block.timestamp < swap.timelock, "Swap expired");

        swap.state = SwapState.Claimed;
        swap.preimage = preimage;

        payable(swap.recipient).transfer(swap.amount);

        emit SwapClaimed(swapId, preimage);
    }

    /**
     * @notice Claim by anyone providing the correct preimage (for atomic swap chains)
     * @param swapId The swap to claim
     * @param preimage The secret that hashes to hashLock
     * @param claimant Address to receive the funds
     */
    function claimFor(bytes32 swapId, bytes32 preimage, address claimant) external {
        Swap storage swap = swaps[swapId];

        require(swap.state == SwapState.Active, "Swap not active");
        require(sha256(abi.encodePacked(preimage)) == swap.hashLock, "Invalid preimage");
        require(block.timestamp < swap.timelock, "Swap expired");
        require(claimant == swap.recipient, "Not authorized recipient");

        swap.state = SwapState.Claimed;
        swap.preimage = preimage;

        payable(claimant).transfer(swap.amount);

        emit SwapClaimed(swapId, preimage);
    }

    /**
     * @notice Refund the locked funds after timelock expires
     * @param swapId The swap to refund
     */
    function refund(bytes32 swapId) external {
        Swap storage swap = swaps[swapId];

        require(swap.state == SwapState.Active, "Swap not active");
        require(block.timestamp >= swap.timelock, "Swap not expired");

        swap.state = SwapState.Refunded;

        payable(swap.sender).transfer(swap.amount);

        emit SwapRefunded(swapId);
    }

    // ============================================================================
    // View Functions
    // ============================================================================

    /**
     * @notice Get full swap details
     */
    function getSwap(bytes32 swapId) external view returns (
        address sender,
        address recipient,
        uint256 amount,
        bytes32 hashLock,
        uint256 timelock,
        SwapState state,
        bytes32 preimage
    ) {
        Swap storage swap = swaps[swapId];
        return (
            swap.sender,
            swap.recipient,
            swap.amount,
            swap.hashLock,
            swap.timelock,
            swap.state,
            swap.preimage
        );
    }

    /**
     * @notice Check if a swap can be claimed (valid preimage provided)
     */
    function canClaim(bytes32 swapId, bytes32 preimage) external view returns (bool) {
        Swap storage swap = swaps[swapId];
        if (swap.state != SwapState.Active) return false;
        if (block.timestamp >= swap.timelock) return false;
        return sha256(abi.encodePacked(preimage)) == swap.hashLock;
    }

    /**
     * @notice Check if a swap can be refunded
     */
    function canRefund(bytes32 swapId) external view returns (bool) {
        Swap storage swap = swaps[swapId];
        return swap.state == SwapState.Active && block.timestamp >= swap.timelock;
    }

    /**
     * @notice Get time remaining until swap expires
     */
    function getTimeRemaining(bytes32 swapId) external view returns (uint256) {
        Swap storage swap = swaps[swapId];
        if (swap.state != SwapState.Active) return 0;
        if (block.timestamp >= swap.timelock) return 0;
        return swap.timelock - block.timestamp;
    }

    /**
     * @notice Verify a preimage against a hash
     */
    function verifyPreimage(bytes32 hashLock, bytes32 preimage) external pure returns (bool) {
        return sha256(abi.encodePacked(preimage)) == hashLock;
    }

    /**
     * @notice Generate a hash lock from a preimage
     */
    function generateHashLock(bytes32 preimage) external pure returns (bytes32) {
        return sha256(abi.encodePacked(preimage));
    }
}

/**
 * @title AtomicSwapFactory
 * @notice Factory for creating and tracking atomic swap instances
 */
contract AtomicSwapFactory {

    event SwapContractCreated(
        address indexed swapContract,
        address indexed creator
    );

    address[] public allSwaps;
    mapping(address => address[]) public userSwaps;

    /**
     * @notice Deploy a new AtomicSwap contract
     */
    function createSwapContract() external returns (address) {
        AtomicSwap swap = new AtomicSwap();
        allSwaps.push(address(swap));
        userSwaps[msg.sender].push(address(swap));

        emit SwapContractCreated(address(swap), msg.sender);

        return address(swap);
    }

    /**
     * @notice Get total number of swap contracts created
     */
    function getSwapCount() external view returns (uint256) {
        return allSwaps.length;
    }

    /**
     * @notice Get user's swap contracts
     */
    function getUserSwaps(address user) external view returns (address[] memory) {
        return userSwaps[user];
    }
}
