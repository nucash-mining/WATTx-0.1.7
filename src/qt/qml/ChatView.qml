import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root
    color: "#ffffff"

    property var messages: []

    Component.onCompleted: {
        console.log("ChatView QML loaded successfully");
    }

    function addMessage(content, timestamp, isOutgoing) {
        console.log("addMessage called: content=" + content + ", time=" + timestamp + ", out=" + isOutgoing);
        messagesModel.append({
            "content": String(content),
            "timestamp": String(timestamp),
            "isOutgoing": Boolean(isOutgoing)
        });
        listView.positionViewAtEnd();
    }

    function clearMessages() {
        console.log("clearMessages called, had " + messagesModel.count + " messages");
        messagesModel.clear();
    }

    function setMessages(msgArray) {
        console.log("setMessages called with " + (msgArray ? msgArray.length : 0) + " messages");
        messagesModel.clear();
        if (msgArray && msgArray.length) {
            for (var i = 0; i < msgArray.length; i++) {
                var msg = msgArray[i];
                messagesModel.append({
                    "content": msg.content || "",
                    "timestamp": msg.timestamp || "",
                    "isOutgoing": msg.isOutgoing || false
                });
            }
        }
        // Scroll to end after a short delay to ensure layout is complete
        Qt.callLater(function() { listView.positionViewAtEnd(); });
    }

    ListModel {
        id: messagesModel
    }

    ListView {
        id: listView
        anchors.fill: parent
        anchors.margins: 10
        model: messagesModel
        spacing: 8
        clip: true

        delegate: Item {
            width: listView.width
            height: bubble.height + 8

            Rectangle {
                id: bubble
                width: Math.min(messageText.implicitWidth + 28, listView.width * 0.75)
                height: messageText.implicitHeight + timeText.implicitHeight + 24
                radius: 18
                color: model.isOutgoing ? "#dcf8c6" : "#e8e8e8"
                anchors.right: model.isOutgoing ? parent.right : undefined
                anchors.left: model.isOutgoing ? undefined : parent.left

                Column {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 4

                    Text {
                        id: messageText
                        text: model.content
                        width: parent.width
                        wrapMode: Text.Wrap
                        color: "#000000"
                        font.pixelSize: 14
                    }

                    Text {
                        id: timeText
                        text: model.timestamp
                        color: "#888888"
                        font.pixelSize: 10
                        anchors.right: parent.right
                    }
                }
            }
        }

        ScrollBar.vertical: ScrollBar {
            active: true
        }
    }
}
