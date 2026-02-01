// Copyright (c) 2024-2026 The WATTx Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <messaging/encryptedmsg.h>
#include <crypto/sha256.h>
#include <hash.h>
#include <logging.h>
#include <util/time.h>

namespace messaging {

std::unique_ptr<MessageManager> g_message_manager;

uint256 EncryptedMessage::GetHash() const
{
    // Simple hash computation using SHA256
    CSHA256 sha;
    sha.Write(recipientHash.begin(), 32);
    sha.Write(senderHash.begin(), 32);
    sha.Write(reinterpret_cast<const unsigned char*>(&timestamp), sizeof(timestamp));
    if (!encryptedData.empty()) {
        sha.Write(encryptedData.data(), encryptedData.size());
    }
    uint256 result;
    sha.Finalize(result.begin());
    return result;
}

bool EncryptedMessage::IsExpired() const
{
    int64_t now = GetTime();
    return timestamp < now - MessageManager::MESSAGE_EXPIRY_SECONDS ||
           timestamp > now + 300;  // Also reject future messages
}

void MessageManager::RegisterAddress(const uint256& addressHash)
{
    LOCK(m_mutex);
    m_our_addresses.insert(addressHash);
    LogDebug(BCLog::NET, "Registered address for messaging: %s\n",
             addressHash.ToString().substr(0, 16));
}

void MessageManager::UnregisterAddress(const uint256& addressHash)
{
    LOCK(m_mutex);
    m_our_addresses.erase(addressHash);
}

bool MessageManager::IsForUs(const uint256& recipientHash) const
{
    LOCK(m_mutex);
    return m_our_addresses.count(recipientHash) > 0;
}

bool MessageManager::ProcessMessage(const EncryptedMessage& msg, int64_t fromPeer)
{
    LOCK(m_mutex);

    // Check if expired
    if (msg.IsExpired()) {
        LogDebug(BCLog::NET, "Rejected expired message %s\n",
                 msg.msgHash.ToString().substr(0, 16));
        return false;
    }

    // Check size
    if (msg.encryptedData.size() > MAX_MESSAGE_SIZE) {
        LogDebug(BCLog::NET, "Rejected oversized message %s\n",
                 msg.msgHash.ToString().substr(0, 16));
        return false;
    }

    // Check if already seen
    if (m_seen_messages.count(msg.msgHash)) {
        return true;  // Already processed, not an error
    }

    m_seen_messages.insert(msg.msgHash);
    m_peer_known_messages[fromPeer].insert(msg.msgHash);

    // Check if it's for us
    if (m_our_addresses.count(msg.recipientHash)) {
        m_received_messages.push_back(msg);
        LogDebug(BCLog::NET, "Received encrypted message for us: %s\n",
                 msg.msgHash.ToString().substr(0, 16));

        // Notify callback
        if (m_callback) {
            m_callback(msg);
        }
    } else {
        // Not for us, add to relay queue
        m_relay_queue.push_back(msg);
        LogDebug(BCLog::NET, "Queued message for relay: %s\n",
                 msg.msgHash.ToString().substr(0, 16));
    }

    // Cleanup if queues are too large
    while (m_relay_queue.size() > MAX_PENDING_MESSAGES) {
        m_relay_queue.pop_front();
    }
    while (m_received_messages.size() > MAX_PENDING_MESSAGES) {
        m_received_messages.pop_front();
    }

    return true;
}

bool MessageManager::QueueOutgoingMessage(const EncryptedMessage& msg)
{
    LOCK(m_mutex);

    if (msg.encryptedData.size() > MAX_MESSAGE_SIZE) {
        return false;
    }

    m_seen_messages.insert(msg.msgHash);
    m_relay_queue.push_back(msg);

    // Also check if this message is for one of our own addresses (self-message)
    if (m_our_addresses.count(msg.recipientHash)) {
        m_received_messages.push_back(msg);
        LogPrintf("Received own message: %s\n", msg.msgHash.ToString().substr(0, 16));
        if (m_callback) {
            m_callback(msg);
        }
    }

    LogDebug(BCLog::NET, "Queued outgoing message: %s to %s\n",
             msg.msgHash.ToString().substr(0, 16),
             msg.recipientHash.ToString().substr(0, 16));

    return true;
}

std::vector<EncryptedMessage> MessageManager::GetMessagesToRelay(int64_t peerNodeId, size_t maxCount)
{
    LOCK(m_mutex);

    std::vector<EncryptedMessage> result;
    auto& peerKnown = m_peer_known_messages[peerNodeId];

    for (const auto& msg : m_relay_queue) {
        if (result.size() >= maxCount) break;

        if (!peerKnown.count(msg.msgHash) && !msg.IsExpired()) {
            result.push_back(msg);
            peerKnown.insert(msg.msgHash);
        }
    }

    return result;
}

std::vector<EncryptedMessage> MessageManager::GetReceivedMessages()
{
    LOCK(m_mutex);
    return std::vector<EncryptedMessage>(m_received_messages.begin(), m_received_messages.end());
}

void MessageManager::MarkDelivered(const uint256& msgHash)
{
    LOCK(m_mutex);
    for (auto it = m_received_messages.begin(); it != m_received_messages.end(); ++it) {
        if (it->msgHash == msgHash) {
            m_received_messages.erase(it);
            break;
        }
    }
}

void MessageManager::SetMessageCallback(MessageCallback callback)
{
    LOCK(m_mutex);
    m_callback = std::move(callback);
}

void MessageManager::CleanupExpired()
{
    LOCK(m_mutex);

    // Clean relay queue
    auto it = m_relay_queue.begin();
    while (it != m_relay_queue.end()) {
        if (it->IsExpired()) {
            m_seen_messages.erase(it->msgHash);
            it = m_relay_queue.erase(it);
        } else {
            ++it;
        }
    }

    // Clean received messages
    auto it2 = m_received_messages.begin();
    while (it2 != m_received_messages.end()) {
        if (it2->IsExpired()) {
            it2 = m_received_messages.erase(it2);
        } else {
            ++it2;
        }
    }
}

bool MessageManager::HaveSeen(const uint256& msgHash) const
{
    LOCK(m_mutex);
    return m_seen_messages.count(msgHash) > 0;
}

void InitMessageManager()
{
    g_message_manager = std::make_unique<MessageManager>();
    LogPrintf("Encrypted P2P messaging initialized\n");
}

void ShutdownMessageManager()
{
    g_message_manager.reset();
    LogPrintf("Encrypted P2P messaging shutdown\n");
}

} // namespace messaging
