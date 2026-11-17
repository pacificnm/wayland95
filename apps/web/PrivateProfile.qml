import QtQuick
import QtWebEngine
import Win95Web

// Private windows' profile: cookies, cache and permissions in memory only,
// gone when the last private window closes (Browser.privateWindowClosed).
WebProfile {
    offTheRecord: true
    httpCacheType: WebEngineProfile.MemoryHttpCache
    persistentCookiesPolicy: WebEngineProfile.NoPersistentCookies
}
