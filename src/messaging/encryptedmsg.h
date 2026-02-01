// Copyright (c) 2024-2026 The WATTx Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef WATTX_MESSAGING_ENCRYPTEDMSG_H
#define WATTX_MESSAGING_ENCRYPTEDMSG_H

#include <serialize.h>
#include <uint256.h>
#include <sync.h>

#include <vector>
#include <deque>
#include <map>
#include <memory>
#include <functional>

namespace messaging {

/**
 * Encrypted P2P message structure
 * Messages are relayed through the network until they reach the recipient
 */
struct EncryptedMessage {
    uint256 msgHash{};                  // Unique message ID (hash of content)
    uint256 recipientHash{};            // Hash of recipient address (for privacy)
    uint256 senderHash{};               // Hash of sender address
    int64_t timestamp{0};               // Unix timestamp
    std::vector<unsigned char> encryptedData{};  // Encrypted message content
    std::vector<unsigned char> signature{};      // Sender's signature (empty for now)

    EncryptedMessage() = default;

    SERIALIZE_METHODS(EncryptedMessage, obj) {
        READWRITE(obj.msgHash, obj.recipientHash, obj.senderHash,
                  obj.timestamp, obj.encryptedData, obj.signature);
    }

    uint256 GetHash() const;
    bool IsExpired() const;

    // Check if message is valid for sending
    bool IsValid() const {
        return !recipientHash.IsNull() && timestamp > 0 &&
               encryptedData.size() > 0 && encryptedData.size() <= 4096;
    }
};

/**
 * Callback for when a new message is received for our addresses
 */
using MessageCallback = std::function<void(const EncryptedMessage&)>;

/**
 * Global message manager for P2P encrypted messaging
 */
class MessageManager {
public:
    static constexpr int64_t MESSAGE_EXPIRY_SECONDS = 7 * 24 * 3600;  // 7 days
    static constexpr size_t MAX_MESSAGE_SIZE = 4096;  // 4KB max
    static constexpr size_t MAX_PENDING_MESSAGES = 10000;

    MessageManager() = default;

    // Register an address hash that we control (to receive messages)
    void RegisterAddress(const uint256& addressHash);
    void UnregisterAddress(const uint256& addressHash);

    // Check if a message is for us
    bool IsForUs(const uint256& recipientHash) const;

    // Process incoming message from network
    bool ProcessMessage(const EncryptedMessage& msg, int64_t fromPeer);

    // Queue a message for sending
    bool QueueOutgoingMessage(const EncryptedMessage& msg);

    // Get messages to relay to a peer
    std::vector<EncryptedMessage> GetMessagesToRelay(int64_t peerNodeId, size_t maxCount = 100);

    // Get received messages for our addresses
    std::vector<EncryptedMessage> GetReceivedMessages();

    // Mark message as delivered
    void MarkDelivered(const uint256& msgHash);

    // Set callback for new messages
    void SetMessageCallback(MessageCallback callback);

    // Cleanup expired messages
    void CleanupExpired();

    // Check if we've seen this message
    bool HaveSeen(const uint256& msgHash) const;

private:
    mutable Mutex m_mutex;

    // Addresses we control (hashed)
    std::set<uint256> m_our_addresses GUARDED_BY(m_mutex);

    // Messages we've received for our addresses
    std::deque<EncryptedMessage> m_received_messages GUARDED_BY(m_mutex);

    // Messages pending relay (not for us, need to forward)
    std::deque<EncryptedMessage> m_relay_queue GUARDED_BY(m_mutex);

    // Messages we've seen (to avoid duplicates)
    std::set<uint256> m_seen_messages GUARDED_BY(m_mutex);

    // Track which peers have which messages
    std::map<int64_t, std::set<uint256>> m_peer_known_messages GUARDED_BY(m_mutex);

    // Callback for new messages
    MessageCallback m_callback GUARDED_BY(m_mutex);
};

// Global message manager instance
extern std::unique_ptr<MessageManager> g_message_manager;

// Initialize/shutdown
void InitMessageManager();
void ShutdownMessageManager();

// Broadcast a message to all connected peers
// This is implemented in net_processing.cpp
void BroadcastEncryptedMessage(const EncryptedMessage& msg);

} // namespace messaging

#endif // WATTX_MESSAGING_ENCRYPTEDMSG_H
