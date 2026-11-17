import QtQuick
import QtWebEngine
import QtWebChannel
import Win95Web

// One tab's page: a WebEngineView on the shared Profile, whose requests
// (new windows, menus, dialogs, permissions, certificate problems) go to
// the window that has it (`browser`, Main.qml), which asks in its own style.
WebEngineView {
    id: view

    property var browser
    // what the address bar shows while it's still loading
    property string pending: ""
    property bool crashed: false
    property string lastSearch: ""
    property int matchCount: 0
    property int matchIndex: 0

    // a login offered for saving: { origin, username, password, update }
    property var loginOffer: null

    // (a private window's tabs are made with its in-memory profile instead)
    profile: Profile

    // the saved-password script's way in (passwords.js): in its own world,
    // out of the page's reach
    webChannel: WebChannel { registeredObjects: [passwords] }
    webChannelWorld: WebEngineScript.ApplicationWorld
    QtObject {
        id: passwords
        WebChannel.id: "w95passwords"
        signal loginReady(string username, string password)
        function submitted(username, password) { view.browser.offerToSave(view, String(username), String(password)); }
        function fill(username) { view.browser.fillLogin(view, String(username), passwords); }
    }
    settings.fullScreenSupportEnabled: true
    settings.pdfViewerEnabled: true
    settings.javascriptCanOpenWindows: true
    // (the window gives it the keyboard: a page loading mustn't take it from
    // a dialog, like the one asking for a password as it starts)
    settings.focusOnNavigationEnabled: false
    settings.dnsPrefetchEnabled: false
    settings.scrollAnimatorEnabled: true

    onLoadingChanged: (info) => {
        if (info.status === WebEngineView.LoadStartedStatus) {
            crashed = false;
            return;
        }
        pending = "";
        if (info.status === WebEngineView.LoadSucceededStatus) {
            if (!browser.privateMode)
                Browser.visited(view.url.toString(), view.title);
            browser.log("loaded " + view.url + " " + view.title);
        } else if (info.status === WebEngineView.LoadFailedStatus) {
            browser.log("failed " + view.url + " " + info.errorString);
        }
    }
    onTitleChanged: if (browser) { browser.refreshTabs(); browser.log("title " + title); }
    onIconChanged: if (browser) browser.refreshTabs()
    onLinkHovered: (hoveredUrl) => { if (browser) browser.hovered = hoveredUrl.toString(); }
    onNewWindowRequested: (request) => browser.newWindowRequest(view, request)
    onContextMenuRequested: (request) => {
        request.accepted = true;
        browser.contextMenu(view, request);
    }
    onJavaScriptDialogRequested: (request) => {
        request.accepted = true;
        browser.jsDialog(view, request);
    }
    onPermissionRequested: (permission) => browser.askPermission(view, permission)
    onCertificateError: (error) => {
        error.defer();
        browser.certificateProblem(view, error);
    }
    onFullScreenRequested: (request) => browser.fullScreen(request)
    onAuthenticationDialogRequested: (request) => {
        request.accepted = true;
        browser.authenticate(view, request);
    }
    onFileDialogRequested: (request) => {
        request.accepted = true;
        browser.chooseFiles(request);
    }
    onWindowCloseRequested: browser.closeTab(view)
    onRenderProcessTerminated: (status, code) => {
        if (status !== WebEngineView.NormalTerminationStatus) {
            crashed = true;
            browser.log("crashed " + view.url);
        }
    }
    onFindTextFinished: (result) => {
        matchCount = result.numberOfMatches;
        matchIndex = result.activeMatch;
        browser.log("find " + result.numberOfMatches);
    }
    onZoomFactorChanged: if (browser) browser.refreshTabs()

    // a page's process gone: say so, with Reload
    Rectangle {
        anchors.fill: parent
        visible: view.crashed
        color: "#ffffff"
        Column {
            anchors.centerIn: parent
            spacing: 10
            Text {
                text: "This page stopped working."
                font.pixelSize: 18
                font.bold: true
            }
            Text { text: "The program showing it closed unexpectedly. Reload to try again." }
            Text {
                text: "<a href=\"#\">Reload</a>"
                textFormat: Text.StyledText
                onLinkActivated: { view.crashed = false; view.reload(); }
            }
        }
    }
}
