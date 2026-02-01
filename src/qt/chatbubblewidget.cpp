// Copyright (c) 2024-2026 The WATTx Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/chatbubblewidget.h>

#include <QPainter>
#include <QPainterPath>
#include <QFontMetrics>
#include <QScrollBar>
#include <QTimer>

// ============================================================================
// ChatBubble Implementation
// ============================================================================

ChatBubble::ChatBubble(const QString& content, const QString& timestamp, bool isOutgoing, QWidget* parent)
    : QWidget(parent)
    , m_content(content)
    , m_timestamp(timestamp)
    , m_isOutgoing(isOutgoing)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    calculateSize();
}

void ChatBubble::calculateSize()
{
    QFontMetrics fm(font());
    QFontMetrics fmSmall(QFont(font().family(), font().pointSize() - 2));

    // Calculate available width for text (75% of parent width, or 300px minimum)
    int parentWidth = parentWidget() ? parentWidget()->width() : 400;
    int maxBubbleWidth = qMax(200, (parentWidth * m_maxWidthPercent) / 100);
    int textWidth = maxBubbleWidth - (m_padding * 2) - 4;

    // Calculate text height with word wrap
    QRect textRect = fm.boundingRect(QRect(0, 0, textWidth, 10000),
                                      Qt::TextWordWrap | Qt::AlignLeft, m_content);

    int timestampHeight = fmSmall.height();

    m_calculatedHeight = textRect.height() + timestampHeight + (m_padding * 2) + 2;
}

QSize ChatBubble::sizeHint() const
{
    return QSize(parentWidget() ? parentWidget()->width() : 400, m_calculatedHeight);
}

QSize ChatBubble::minimumSizeHint() const
{
    return QSize(100, m_calculatedHeight);
}

void ChatBubble::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    calculateSize();
    updateGeometry();
}

void ChatBubble::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QFontMetrics fm(font());
    QFont smallFont = font();
    smallFont.setPointSize(font().pointSize() - 2);
    QFontMetrics fmSmall(smallFont);

    // Calculate bubble dimensions
    int maxBubbleWidth = qMax(200, (width() * m_maxWidthPercent) / 100);
    int textWidth = maxBubbleWidth - (m_padding * 2) - 4;

    // Calculate text bounds
    QRect textRect = fm.boundingRect(QRect(0, 0, textWidth, 10000),
                                      Qt::TextWordWrap | Qt::AlignLeft, m_content);

    int timestampWidth = fmSmall.horizontalAdvance(m_timestamp);
    int bubbleWidth = qMin(maxBubbleWidth,
                           qMax(textRect.width(), timestampWidth) + (m_padding * 2) + 4);
    int bubbleHeight = textRect.height() + fmSmall.height() + (m_padding * 2) + 2;

    // Position bubble (right for outgoing, left for incoming)
    int bubbleX = m_isOutgoing ? (width() - bubbleWidth - 6) : 6;
    int bubbleY = 1;

    // Draw bubble background
    QPainterPath path;
    QRectF bubbleRect(bubbleX, bubbleY, bubbleWidth, bubbleHeight);
    path.addRoundedRect(bubbleRect, m_bubbleRadius, m_bubbleRadius);

    // Colors: green for outgoing, light gray for incoming
    QColor bubbleColor = m_isOutgoing ? QColor(220, 248, 198) : QColor(232, 232, 232);
    painter.fillPath(path, bubbleColor);

    // Draw message text
    painter.setPen(Qt::black);
    painter.setFont(font());
    QRect messageRect(bubbleX + m_padding, bubbleY + m_padding,
                      bubbleWidth - (m_padding * 2), textRect.height());
    painter.drawText(messageRect, Qt::TextWordWrap | Qt::AlignLeft, m_content);

    // Draw timestamp
    painter.setFont(smallFont);
    painter.setPen(QColor(136, 136, 136));
    QRect timeRect(bubbleX + m_padding,
                   bubbleY + m_padding + textRect.height() + 4,
                   bubbleWidth - (m_padding * 2), fmSmall.height());
    painter.drawText(timeRect, Qt::AlignRight, m_timestamp);
}

// ============================================================================
// ChatBubbleWidget Implementation
// ============================================================================

ChatBubbleWidget::ChatBubbleWidget(QWidget* parent)
    : QWidget(parent)
{
    // Create scroll area
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setStyleSheet("QScrollArea { background-color: white; border: none; }");

    // Create container widget for bubbles
    m_containerWidget = new QWidget();
    m_containerWidget->setStyleSheet("background-color: white;");

    m_layout = new QVBoxLayout(m_containerWidget);
    m_layout->setSpacing(2);
    m_layout->setContentsMargins(4, 4, 4, 4);
    m_layout->addStretch(); // Push bubbles to top initially

    m_scrollArea->setWidget(m_containerWidget);

    // Main layout
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->addWidget(m_scrollArea);
}

ChatBubbleWidget::~ChatBubbleWidget()
{
    clearMessages();
}

void ChatBubbleWidget::addMessage(const QString& content, const QString& timestamp, bool isOutgoing)
{
    // Remove the stretch item temporarily
    QLayoutItem* stretch = m_layout->takeAt(m_layout->count() - 1);

    ChatBubble* bubble = new ChatBubble(content, timestamp, isOutgoing, m_containerWidget);
    m_bubbles.append(bubble);
    m_layout->addWidget(bubble);

    // Re-add stretch at the end
    m_layout->addStretch();

    // Scroll to bottom after layout updates
    QTimer::singleShot(50, this, &ChatBubbleWidget::scrollToBottom);
}

void ChatBubbleWidget::clearMessages()
{
    for (ChatBubble* bubble : m_bubbles) {
        m_layout->removeWidget(bubble);
        delete bubble;
    }
    m_bubbles.clear();
}

void ChatBubbleWidget::setMessages(const QList<ChatMessage>& messages)
{
    clearMessages();

    // Remove stretch
    while (m_layout->count() > 0) {
        QLayoutItem* item = m_layout->takeAt(0);
        delete item;
    }

    for (const ChatMessage& msg : messages) {
        ChatBubble* bubble = new ChatBubble(msg.content, msg.timestamp, msg.isOutgoing, m_containerWidget);
        m_bubbles.append(bubble);
        m_layout->addWidget(bubble);
    }

    // Add stretch at the end
    m_layout->addStretch();

    // Scroll to bottom
    QTimer::singleShot(50, this, &ChatBubbleWidget::scrollToBottom);
}

void ChatBubbleWidget::scrollToBottom()
{
    QScrollBar* vbar = m_scrollArea->verticalScrollBar();
    if (vbar) {
        vbar->setValue(vbar->maximum());
    }
}

void ChatBubbleWidget::setBackgroundColor(const QColor& color)
{
    m_backgroundColor = color;
    updateStyleSheet();
}

void ChatBubbleWidget::updateStyleSheet()
{
    QString colorStr = m_backgroundColor.name();
    m_scrollArea->setStyleSheet(QString("QScrollArea { background-color: %1; border: none; }").arg(colorStr));
    m_containerWidget->setStyleSheet(QString("background-color: %1;").arg(colorStr));
}
