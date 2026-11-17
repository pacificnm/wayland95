import QtQuick
import QtWebEngine
import Win95Web

// What both kinds of profile have (the usual one, Profile, and a private
// window's, PrivateProfile): downloads to the Downloads folder, a page's
// notifications on the desktop, and the saved-password script.
WebEngineProfile {
    id: profile

    downloadPath: Browser.downloadsDir
    property var downloads: []
    signal downloadStarted(var download)

    onDownloadRequested: (download) => {
        download.accept();
        profile.downloads = profile.downloads.concat([download]);
        profile.downloadStarted(download);
    }
    onDownloadFinished: (download) => profile.downloads = profile.downloads.slice()
    onPresentNotification: (notification) => Browser.notify(notification.title, notification.message, notification.origin)

    // (in a world of their own: the page's scripts can't see or call them)
    Component.onCompleted: userScripts.collection = [
        { name: "webchannel", sourceUrl: "qrc:///qtwebchannel/qwebchannel.js",
          injectionPoint: WebEngineScript.DocumentCreation, worldId: WebEngineScript.ApplicationWorld },
        { name: "passwords", sourceUrl: Qt.resolvedUrl("passwords.js"),
          injectionPoint: WebEngineScript.DocumentReady, worldId: WebEngineScript.ApplicationWorld },
    ]
}
