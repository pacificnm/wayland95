pragma Singleton
import QtQuick
import QtWebEngine
import Win95Web

// The browser's profile: cookies, cache and site permissions kept on disk
// (WebEngine's, under ~/.local/share/win95-wayland/win95web).
WebProfile {
    storageName: "win95web"
    offTheRecord: false
    httpCacheType: WebEngineProfile.DiskHttpCache
    persistentCookiesPolicy: WebEngineProfile.AllowPersistentCookies
}
