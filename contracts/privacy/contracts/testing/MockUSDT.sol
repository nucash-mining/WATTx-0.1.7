// SPDX-License-Identifier: MIT
pragma solidity ^0.8.20;

import "@openzeppelin/contracts/token/ERC20/ERC20.sol";

/**
 * @title MockUSDT
 * @notice Mock USDT token for testing privacy pools
 * @dev 6 decimals like real USDT
 */
contract MockUSDT is ERC20 {

    constructor() ERC20("Mock USDT", "USDT") {}

    function decimals() public pure override returns (uint8) {
        return 6;
    }

    /**
     * @notice Mint tokens for testing
     */
    function mint(address to, uint256 amount) external {
        _mint(to, amount);
    }

    /**
     * @notice Faucet - mint 10,000 USDT to caller
     */
    function faucet() external {
        _mint(msg.sender, 10000 * 1e6);
    }
}
