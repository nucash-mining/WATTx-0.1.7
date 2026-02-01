// Copyright (c) 2024-2026 The WATTx Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef WATTX_QT_MESSAGINGPAGE_H
#define WATTX_QT_MESSAGINGPAGE_H

#include <interfaces/wallet.h>
#include <uint256.h>
#include <qt/clientmodel.h>
#include <messaging/encryptedmsg.h>

#include <QWidget>
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QColor>
#include <QMenu>
#include <memory>

class ClientModel;
class ChatBubbleWidget;
class WalletModel;
class PlatformStyle;

namespace Ui {
    class MessagingPage;
}

QT_BEGIN_NAMESPACE
class QListWidgetItem;
class QTableWidgetItem;
QT_END_NAMESPACE

/**
 * Message types for the messaging system
 */
enum class MessageType {
    OpReturn,       // On-chain OP_RETURN message
    P2PEncrypted,   // Encrypted P2P message
    LocalMemo       // Local transaction memo
};

/**
 * Structure for a stored message
 */
struct StoredMessage {
    int64_t id{0};
    MessageType type;
    QString fromAddress;
    QString toAddress;
    QString content;
    int64_t timestamp{0};
    uint256 txid;           // For OP_RETURN messages
    bool isOutgoing{false};
    bool isRead{false};
};

/**
 * Structure for a P2P chat conversation
 */
struct ChatConversation {
    QString peerAddress;
    QString peerLabel;
    int unreadCount{0};
    int64_t lastMessageTime{0};
    QString lastMessagePreview;
};

/**
 * Structure for a pending chat request
 */
struct PendingChatRequest {
    QString fromAddress;
    QString toAddress;  // Our address that received the request
    int64_t timestamp{0};
    bool isGroupInvite{false};
    QString groupId;  // For group invites

    bool operator==(const PendingChatRequest& other) const {
        return fromAddress == other.fromAddress && toAddress == other.toAddress;
    }
};

/**
 * Structure for group chat membership
 */
struct GroupMember {
    QString address;
    QString pubkeyHex;
    int64_t joinedTime{0};
    bool isRevoked{false};
    int64_t revokedTime{0};
    QString revokedBy;  // Address that revoked this member
};

/**
 * Structure for group chat
 */
struct GroupChat {
    QString groupId;
    QString groupName;
    QString creatorAddress;
    int64_t createdTime{0};
    QList<GroupMember> members;
    QStringList revokedAddresses;  // Addresses we have revoked
};

/**
 * Messaging page widget - Full messaging suite for WATTx wallet
 *
 * Features:
 * 1. OP_RETURN Messages - Permanent on-chain messages (up to 80 bytes)
 * 2. Encrypted P2P Chat - Direct encrypted messaging between addresses
 * 3. Transaction Memos - Local notes attached to transactions
 */
class MessagingPage : public QWidget
{
    Q_OBJECT

public:
    explicit MessagingPage(const PlatformStyle *platformStyle, QWidget *parent = nullptr);
    ~MessagingPage();

    void setClientModel(ClientModel *clientModel);
    void setWalletModel(WalletModel *walletModel);

    /** Register wallet addresses for P2P message receiving */
    void registerWalletAddresses();

    /** Refresh identity list for sending */
    void refreshIdentities();

    /** Set up callback for incoming P2P messages */
    void setupMessageCallback();

    /** Handle an incoming encrypted message */
    void handleIncomingMessage(const messaging::EncryptedMessage& msg);

public Q_SLOTS:
    /** Refresh message lists */
    void refreshMessages();

    /** Handle new block (check for incoming OP_RETURN messages) */
    void numBlocksChanged(int count, const QDateTime& blockDate, double nVerificationProgress, SyncType header, SynchronizationState sync_state);

Q_SIGNALS:
    /** Signal to request wallet unlock */
    void requireUnlock(bool fromMenu);

    /** Signal when a new message is received */
    void newMessageReceived(const QString& from, const QString& preview);

private Q_SLOTS:
    // Tab switching
    void onTabChanged(int index);

    // OP_RETURN tab
    void onSendOpReturnClicked();
    void onOpReturnMessageSelected(QTableWidgetItem* item);
    void onOpReturnCharCountChanged();
    void onEncryptToggled(bool checked);

    // P2P Chat tab
    void onConversationSelected(QListWidgetItem* item);
    void onSendP2PMessageClicked();
    void onNewConversationClicked();
    void onRefreshP2PClicked();
    void onKeyExchangeClicked();

    // Context menu and new features
    void onConversationContextMenu(const QPoint& pos);
    void onPendingRequestSelected(QListWidgetItem* item);
    void onChatBackgroundClicked();
    void onInviteUserClicked();
    void onManageGroupClicked();

    // Transaction Memos tab
    void onTxMemoSaveClicked();
    void onTxMemoSearchClicked();
    void onTxMemoSelected(QTableWidgetItem* item);
    void onTxMemoDeleteClicked();

    // Address book integration
    void onChooseFromAddressBook();
    void onChooseToAddressBook();

private:
    Ui::MessagingPage *ui;
    ClientModel *clientModel{nullptr};
    WalletModel *walletModel{nullptr};
    const PlatformStyle* const platformStyle;

    // JSON file storage
    QString m_dataDir;
    bool m_storageInitialized{false};

    // Current conversation state
    QString m_currentConversationPeer;
    std::vector<StoredMessage> m_currentChatMessages;
    std::vector<ChatConversation> m_conversations;

    // Contact labels (address -> label)
    QMap<QString, QString> m_contactLabels;

    // Exchanged public keys for ECDH (address -> compressed pubkey hex)
    QMap<QString, QString> m_exchangedKeys;

    // Handshake status: 0=none, 1=requested, 2=accepted
    QMap<QString, int> m_handshakeStatus;

    // Auto-refresh timer for chat
    QTimer* m_refreshTimer{nullptr};

    // Custom chat bubble widget (pure Qt, no QML dependencies)
    ChatBubbleWidget* m_chatView{nullptr};

    // Chat background color
    QColor m_chatBackgroundColor{Qt::white};

    // Pending chat requests
    QList<PendingChatRequest> m_pendingRequests;

    // Group chats (groupId -> GroupChat)
    QMap<QString, GroupChat> m_groups;

    // Current group context (if in group chat)
    QString m_currentGroupId;

    // P2P Message type markers
    static constexpr unsigned char MSG_HANDSHAKE_REQUEST = 0x10;
    static constexpr unsigned char MSG_HANDSHAKE_ACCEPT = 0x11;
    static constexpr unsigned char MSG_ENCRYPTED = 0x12;
    static constexpr unsigned char MSG_GROUP_INVITE = 0x13;

    // OP_RETURN constants
    static constexpr size_t MAX_OP_RETURN_SIZE = 80;
    static constexpr size_t OP_RETURN_PREFIX_SIZE = 4; // "WTX:" prefix

    // Storage methods (JSON-based)
    bool initStorage();
    void saveData();

    // OP_RETURN methods
    bool sendOpReturnMessage(const QString& message);
    std::vector<StoredMessage> getOpReturnMessages();
    void parseOpReturnFromTransaction(const uint256& txid);
    void scanBlockForOpReturn(int blockHeight);

    // P2P Encryption methods
    QByteArray encryptMessage(const QString& message, const QString& recipientAddress);
    QString decryptMessage(const QByteArray& encrypted, const QString& senderAddress);
    bool sendP2PMessage(const QString& toAddress, const QString& message);
    std::vector<StoredMessage> getP2PMessages(const QString& peerAddress);
    std::vector<ChatConversation> getConversations();
    void storeP2PMessage(const StoredMessage& msg);

    // Key exchange handshake methods
    bool sendHandshakeRequest(const QString& toAddress);
    bool sendHandshakeAccept(const QString& toAddress, const QString& ourAddress = QString());
    void handleHandshakeMessage(const QByteArray& data, const QString& fromAddress);
    bool hasExchangedKey(const QString& address);
    void saveExchangedKeys();
    void loadExchangedKeys();

    // Transaction memo methods
    bool saveTxMemo(const uint256& txid, const QString& memo);
    QString getTxMemo(const uint256& txid);
    bool deleteTxMemo(const uint256& txid);
    std::vector<std::pair<uint256, QString>> searchTxMemos(const QString& query);
    std::vector<std::pair<uint256, QString>> getAllTxMemos();

    // UI helpers
    void setupOpReturnTab();
    void setupP2PTab();
    void setupMemoTab();
    void updateConversationList();
    void updateChatDisplay();
    void updateOpReturnList();
    void updateMemoList();
    void showMessageNotification(const QString& title, const QString& message);

    // Address utilities
    QString getAddressLabel(const QString& address);
    bool validateAddress(const QString& address);

    // Pending requests management
    void loadPendingRequests();
    void savePendingRequests();
    void updatePendingRequestsList();
    void addPendingRequest(const PendingChatRequest& request);
    void acceptChatRequest(const PendingChatRequest& request);
    void rejectChatRequest(const PendingChatRequest& request);

    // Group chat methods
    void loadGroups();
    void saveGroups();
    QString createGroup(const QString& name, const QStringList& initialMembers);
    bool inviteToGroup(const QString& groupId, const QString& address);
    bool revokeFromGroup(const QString& groupId, const QString& address);
    bool leaveGroup(const QString& groupId);
    QList<GroupMember> getGroupMembers(const QString& groupId);
    bool isAddressRevokedInGroup(const QString& groupId, const QString& address);
    void sendGroupInvite(const QString& groupId, const QString& toAddress);

    // Chat background
    void loadChatBackgroundColor();
    void saveChatBackgroundColor();
    void applyChatBackgroundColor();

    // Edit label (now via context menu)
    void editConversationLabel(const QString& address);
};

#endif // WATTX_QT_MESSAGINGPAGE_H
