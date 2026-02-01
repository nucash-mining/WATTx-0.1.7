# Copyright (c) 2024-2026 The WATTx Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

add_library(wattx_messaging STATIC EXCLUDE_FROM_ALL
  encryptedmsg.cpp
)

target_link_libraries(wattx_messaging
  PRIVATE
    core_interface
    bitcoin_crypto
    Boost::headers
)
