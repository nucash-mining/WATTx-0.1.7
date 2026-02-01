# Copyright (c) 2024 The WATTx Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

add_library(wattx_validators STATIC EXCLUDE_FROM_ALL
  validatordb.cpp
  delegation.cpp
  ../dbwrapper.cpp
)

target_link_libraries(wattx_validators
  PUBLIC
    core_interface
    bitcoin_crypto
    leveldb
    Boost::headers
)

# Include directories for dbwrapper
target_include_directories(wattx_validators PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
