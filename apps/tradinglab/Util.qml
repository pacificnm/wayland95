pragma Singleton

import QtQuick
import Win95Ui

// Formatting shared by the screens (upstream renderer/src/format.ts and display.ts), and the guards for text that comes
// from outside (news, filings, Claude's answers) before it is drawn.
QtObject {
    // the user's display settings (File > Settings > Display): time zone and 12/24-hour clock
    property string timeZone: "America/New_York" // "" = this computer's zone
    property bool hour12: true

    function usd(n) { return n === null || n === undefined || !isFinite(n) ? "—" : core.money(Number(n), 2); }
    function money(n, d) { return n === null || n === undefined || !isFinite(n) ? "—" : core.money(Number(n), d === undefined ? 2 : d); }
    function num(n, d) { return n === null || n === undefined || !isFinite(n) ? "—" : core.grouped(Number(n), d === undefined ? 2 : d); }
    function pct(n, d) { return n === null || n === undefined || !isFinite(n) ? "—" : (n > 0 ? "+" : "") + Number(n).toFixed(d === undefined ? 2 : d) + "%"; }
    function signed(n, d) { return n === null || n === undefined || !isFinite(n) ? "—" : (n > 0 ? "+" : "") + core.grouped(Number(n), d === undefined ? 2 : d); }
    function signedMoney(n) { return n === null || n === undefined || !isFinite(n) ? "—" : (n > 0 ? "+" : "") + core.money(Number(n), 2); }
    // 1.2K, 3.4M, 5.6B, 7.8T (Intl compact notation)
    function compact(n) {
        if (n === null || n === undefined || !isFinite(n)) return "—";
        const a = Math.abs(n), s = n < 0 ? "-" : "";
        const f = (v) => String(Math.round(v * 100) / 100);
        if (a >= 1e12) return s + f(a / 1e12) + "T";
        if (a >= 1e9) return s + f(a / 1e9) + "B";
        if (a >= 1e6) return s + f(a / 1e6) + "M";
        if (a >= 1e3) return s + f(a / 1e3) + "K";
        return s + f(a);
    }
    // green for gains, red for losses (dark enough to read on the window colour)
    function tone(n) { return n > 0 ? "#007000" : n < 0 ? "#b00000" : Theme.text; }

    // UTC epoch seconds as a date/time in the display zone
    function dateTime(epoch) {
        if (!epoch) return "—";
        return date(epoch) + " " + clock(epoch);
    }
    function date(epoch) { return epoch ? core.formatInZone(epoch, timeZone, "d MMM yyyy") : "—"; }
    function clock(epoch) { return epoch ? core.formatInZone(epoch, timeZone, hour12 ? "h:mm AP" : "HH:mm") : "—"; }
    function ago(epoch) {
        const s = Math.max(0, Date.now() / 1000 - epoch);
        if (s < 60) return "just now";
        if (s < 3600) return Math.floor(s / 60) + " min ago";
        if (s < 86400) return Math.floor(s / 3600) + " h ago";
        return Math.floor(s / 86400) + " d ago";
    }

    // Text from outside, for places that would otherwise read a string as rich text: "<" becomes a look-alike.
    function plain(text) { return String(text ?? "").replace(/</g, "‹"); }
    // Markdown from Claude, a lesson or a news feed: no images or raw HTML (Qt's Markdown importer would load or draw
    // them), link reference definitions dropped; the text itself is unchanged.
    function markdown(text) {
        return String(text ?? "")
            .replace(/!\[([^\]\n]*)\]\([^)\n]*\)/g, "$1")
            .replace(/!\[([^\]\n]*)\]\[[^\]\n]*\]/g, "$1")
            .replace(/^[ \t]{0,3}\[[^\]\n]+\]:[ \t]*\S.*$/gm, "")
            .replace(/</g, "&lt;");
    }
    function localPath(url) {
        const s = String(url ?? "");
        return s.startsWith("file://") ? decodeURIComponent(s.slice(7)) : s;
    }
}
