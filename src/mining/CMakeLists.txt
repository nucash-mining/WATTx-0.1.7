# Copyright (c) 2024-2026 The WATTx Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

# WATTx Mining library
# Contains RandomX miner and other mining-related code
# This is a separate library to avoid circular dependencies between
# bitcoin_crypto (which needs RandomX hashing) and bitcoin_node (which manages mining)

add_library(wattx_mining STATIC EXCLUDE_FROM_ALL
  ../node/randomx_miner.cpp
  ../node/x25x_miner.cpp
)

target_link_libraries(wattx_mining
  PRIVATE
    core_interface
    randomx
)
