// Copyright (c) 2024-2026 The WATTx Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef WATTX_QT_CHATBUBBLEWIDGET_H
#define WATTX_QT_CHATBUBBLEWIDGET_H

#include <QWidget>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QString>
#include <QList>
#include <QPainter>
#include <QDateTime>

/**
 * Structure representing a single chat message
 */
struct ChatMessage {
    QString content;
    QString timestamp;
    bool isOutgoing;
};

/**
 * Widget that displays a single chat bubble with rounded corners
 */
class ChatBubble : public QWidget
{
    Q_OBJECT

public:
    explicit ChatBubble(const QString& content, const QString& timestamp, bool isOutgoing, QWidget* parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    QString m_content;
    QString m_timestamp;
    bool m_isOutgoing;
    int m_bubbleRadius{10};
    int m_padding{6};
    int m_maxWidthPercent{80};

    void calculateSize();
    int m_calculatedHeight{60};
};

/**
 * Scrollable widget that contains all chat bubbles
 */
class ChatBubbleWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ChatBubbleWidget(QWidget* parent = nullptr);
    ~ChatBubbleWidget();

    /** Add a single message to the chat */
    void addMessage(const QString& content, const QString& timestamp, bool isOutgoing);

    /** Clear all messages */
    void clearMessages();

    /** Set all messages at once */
    void setMessages(const QList<ChatMessage>& messages);

    /** Scroll to the bottom of the chat */
    void scrollToBottom();

    /** Set background color for the chat area */
    void setBackgroundColor(const QColor& color);

    /** Get current background color */
    QColor backgroundColor() const { return m_backgroundColor; }

private:
    QScrollArea* m_scrollArea;
    QWidget* m_containerWidget;
    QVBoxLayout* m_layout;
    QList<ChatBubble*> m_bubbles;
    QColor m_backgroundColor{Qt::white};

    void rebuildLayout();
    void updateStyleSheet();
};

#endif // WATTX_QT_CHATBUBBLEWIDGET_H
