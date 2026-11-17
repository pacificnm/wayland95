pragma Singleton
import QtQuick
import QtWebEngine
import Win95Web

// The browser's web data, shared by its windows: cookies, cache, site
// permissions and logins kept between sessions (under
// ~/.local/share/win95-wayland/win95web), and the downloads.
WebEngineProfile {
    id: profile

    storageName: "win95web"
    offTheRecord: false
    httpCacheType: WebEngineProfile.DiskHttpCache
    persistentCookiesPolicy: WebEngineProfile.AllowPersistentCookies
    downloadPath: Browser.downloadsDir

    // every download this time: WebEngineDownloadRequest objects, newest last
    property var downloads: []
    signal downloadStarted(var download)

    onDownloadRequested: (download) => {
        // to Downloads, under a name not taken yet
        download.accept();
        profile.downloads = profile.downloads.concat([download]);
        profile.downloadStarted(download);
    }
    onDownloadFinished: (download) => profile.downloads = profile.downloads.slice()
    onPresentNotification: (notification) => Browser.notify(notification.title, notification.message, notification.origin)
}
