import QtQuick
import Win95Ui

// Ask a question: a local model answers from passages of the installed
// libraries, and the program links each sentence to the source it matches.
Page95 {
    id: page
    property alias chatId: conversation.chatId
    property alias conversation: conversation
    title: conversation.title !== "" ? Util.pageTitle(conversation.title) : "Ask"
    actions: [
        { text: "&New Chat", enabled: conversation.chatId > 0 || conversation.busy, action: newChat },
        { text: "&Stop", enabled: conversation.busy, action: function () { conversation.stop(); } },
    ]
    function newChat() {
        conversation.chatId = 0;
        chat.focusInput();
    }
    Component.onCompleted: chat.focusInput()

    Conversation { id: conversation }
    ChatView {
        id: chat
        anchors.fill: parent
        conversation: conversation
        onOpenArticle: (libraryId, path, fragment) => window.openArticle(libraryId, path, fragment)
    }
}
