import QtQuick
import Win95Ui
import Win95Control

// Regional Settings, like Win95's: the region, then Number, Currency,
// Time and Date, each showing samples. On Linux formats come from locales,
// so each tab picks which region's format to use rather than individual
// symbols. Programs started from Wayland 95 get them (LANG, LC_NUMERIC,
// LC_MONETARY, LC_TIME); "Use for the whole computer" sets the system's.
PropertySheet {
    id: sheet

    title: "Regional Settings Properties"
    titles: ["Regional Settings", "Number", "Currency", "Time", "Date"]

    // edited copy, written on Apply
    property var settings: Wayland95Settings.region
    function set(key, value) {
        var copy = Object.assign({}, settings);
        copy[key] = value;
        settings = copy;
        dirty = true;
    }
    property bool clock24: Wayland95Settings.clock24Hour
    onApplyRequested: {
        Wayland95Settings.setRegion(settings);
        Wayland95Settings.clock24Hour = clock24;
    }

    // the region in effect: chosen, else this session's
    readonly property string region: settings.region || Locales.session || "en_US.UTF-8"
    function effective(key) { return settings[key] || region; }
    function loc(name) { return Qt.locale(String(name).split(".")[0]); }

    // the regions a tab can use: "same as Regional Settings", the
    // installed ones, and the chosen one
    function choices(key) {
        var list = [{ name: "", display: "Same as Regional Settings (" + Locales.displayName(region) + ")" }];
        var all = Locales.all;
        for (var i = 0; i < all.length; ++i)
            if (all[i].installed || all[i].name === settings[key] || all[i].name === settings.region)
                list.push(all[i]);
        return list;
    }

    component Para: Text {
        width: parent ? parent.width : 300
        wrapMode: Text.Wrap
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }
    // "Format like:" a region, for one tab
    component FormatChoice: Row {
        id: choice
        property string key
        readonly property var list: sheet.choices(key)
        spacing: 8
        Text {
            height: 21
            verticalAlignment: Text.AlignVCenter
            text: "<u>F</u>ormat like:"
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
        ComboBox95 {
            width: 280
            popupMenu: sheet.popupMenu
            model: choice.list.map(function (l) { return l.display; })
            currentIndex: Math.max(0, choice.list.findIndex(function (l) { return l.name === (sheet.settings[choice.key] || ""); }))
            onActivated: (i) => sheet.set(choice.key, choice.list[i].name)
        }
    }
    // a group of "label: sample" lines
    component Samples: GroupBox95 {
        id: samples
        property var rows: []   // [[label, value]]
        width: parent ? parent.width : 300
        height: 26 + rows.length * 20
        Grid {
            columns: 2
            columnSpacing: 12
            rowSpacing: 4
            Repeater {
                model: samples.rows.reduce(function (all, r) { return all.concat(r); }, [])
                Text {
                    required property string modelData
                    required property int index
                    text: modelData
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                    font.bold: index % 2 === 1
                }
            }
        }
    }

    // ----------------------------------------------------- Regional Settings
    Item {
        AppletIcon {
            id: globe
            anchors.horizontalCenter: parent.horizontalCenter
            size: 96
            kind: "regional"
        }
        Para {
            id: intro
            y: globe.height + 6
            text: "Many programs support international settings for numbers, currencies, times and dates. "
                  + "Choose your region, then change the formats on the other tabs if you like."
        }
        TextField95 {
            id: find
            y: intro.y + intro.height + 6
            width: parent.width
            height: 21
        }
        ProgramList {
            id: regions
            y: find.y + 26
            width: parent.width
            height: parent.height - y - foot.height - 6
            readonly property var filtered: {
                var words = find.text.toLowerCase().split(/\s+/).filter(function (w) { return w; });
                return Locales.all.filter(function (l) {
                    var t = (l.display + " " + l.name).toLowerCase();
                    return words.every(function (w) { return t.indexOf(w) >= 0; });
                }).map(function (l) { return { key: l.name, name: l.display, installed: l.installed }; });
            }
            model: filtered
            selectedKey: sheet.region
            onSelectedKeyChanged: if (selectedKey && selectedKey !== sheet.region) sheet.set("region", selectedKey)
            iconComponent: AppletIcon { size: parent.size; kind: "regional" }
            noteFor: function (item) { return item.installed ? "" : "Wayland 95 programs only"; }
            emptyText: "No region matches."
            Component.onCompleted: { var i = selectedIndex(); if (i >= 0) select(i); }
        }
        Column {
            id: foot
            anchors.bottom: parent.bottom
            width: parent.width
            spacing: 4
            Para {
                color: Theme.shadow
                text: Locales.isInstalled(sheet.region)
                      ? "Programs you start from now on use these settings. The computer's default is "
                        + (Locales.system ? Locales.displayName(Locales.system) : "not set") + "."
                      : "Only Wayland 95's programs can use this region until it's installed on the computer "
                        + "(on Debian: sudo dpkg-reconfigure locales)."
            }
            Button95 {
                width: 210
                text: "Use for the <u>w</u>hole computer..."
                enabled: Locales.isInstalled(sheet.region) && sheet.region !== Locales.system
                onClicked: Locales.setSystem(sheet.region)
            }
        }
        Connections {
            target: Locales
            function onSystemSet(ok, message) {
                sheet.messageBox.show(sheet.title, ok ? "The computer's default region is now "
                                      + Locales.displayName(Locales.system) + ". It applies to the login screen and new users."
                                      : "The computer's default couldn't be changed.\n\n" + message, ok ? "info" : "error", ["OK"]);
            }
        }
    }

    // ---------------------------------------------------------------- Number
    Column {
        spacing: 10
        readonly property var l: sheet.loc(sheet.effective("numbers"))
        FormatChoice { key: "numbers" }
        Samples {
            title: "Appearance samples"
            rows: [["Positive:", Number(123456789).toLocaleString(parent.l, "f", 2)],
                   ["Negative:", Number(-123456789).toLocaleString(parent.l, "f", 2)]]
        }
        Samples {
            title: "Details"
            rows: [["Decimal symbol:", "“" + parent.l.decimalPoint + "”"],
                   ["Digit grouping symbol:", "“" + (parent.l.groupSeparator === " " || parent.l.groupSeparator === " " ? "space" : parent.l.groupSeparator) + "”"],
                   ["Negative sign symbol:", parent.l.negativeSign],
                   ["Measurement system:", parent.l.measurementSystem === Locale.MetricSystem ? "Metric" : "U.S."]]
        }
    }

    // -------------------------------------------------------------- Currency
    Column {
        spacing: 10
        readonly property var l: sheet.loc(sheet.effective("currency"))
        FormatChoice { key: "currency" }
        Samples {
            title: "Appearance samples"
            rows: [["Positive:", Number(123456789).toLocaleCurrencyString(parent.l)],
                   ["Negative:", Number(-123456789).toLocaleCurrencyString(parent.l)]]
        }
        Samples {
            title: "Details"
            rows: [["Currency symbol:", parent.l.currencySymbol(Locale.CurrencySymbol)],
                   ["Currency name:", parent.l.currencySymbol(Locale.CurrencyDisplayName)],
                   ["International code:", parent.l.currencySymbol(Locale.CurrencyIsoCode)]]
        }
    }

    // ------------------------------------------------------------------ Time
    Column {
        spacing: 10
        readonly property var l: sheet.loc(sheet.effective("time"))
        property date now: new Date()
        Timer { interval: 1000; running: sheet.visible; repeat: true; onTriggered: parent.now = new Date() }
        FormatChoice { key: "time" }
        Samples {
            title: "Appearance samples"
            rows: [["Time:", parent.now.toLocaleTimeString(parent.l, Locale.LongFormat)],
                   ["Short:", parent.now.toLocaleTimeString(parent.l, Locale.ShortFormat)],
                   ["AM symbol:", parent.l.amText || "(none)"],
                   ["PM symbol:", parent.l.pmText || "(none)"]]
        }
        GroupBox95 {
            width: parent.width
            height: 50
            title: "Taskbar clock"
            CheckBox95 {
                text: "Use a <u>2</u>4-hour clock"
                checked: sheet.clock24
                onToggled: (on) => { sheet.clock24 = on; sheet.dirty = true; }
            }
        }
        Para {
            color: Theme.shadow
            text: "On Linux, times and dates share one format setting: what you choose here also applies to dates, unless the Date tab chooses differently."
        }
    }

    // ------------------------------------------------------------------ Date
    Column {
        spacing: 10
        readonly property var l: sheet.loc(sheet.settings.date || sheet.effective("time"))
        FormatChoice { key: "date" }
        Samples {
            title: "Appearance samples"
            rows: [["Short date:", new Date().toLocaleDateString(parent.l, Locale.ShortFormat)],
                   ["Long date:", new Date().toLocaleDateString(parent.l, Locale.LongFormat)]]
        }
        GroupBox95 {
            id: weekGroup
            width: parent.width
            height: 54
            title: "Calendar"
            readonly property var days: [-1, 0, 1, 6]
            Row {
                spacing: 8
                Text {
                    height: 21
                    verticalAlignment: Text.AlignVCenter
                    text: "First day of the <u>w</u>eek:"
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                }
                ComboBox95 {
                    width: 200
                    popupMenu: sheet.popupMenu
                    model: weekGroup.days.map(function (d) {
                        var en = Qt.locale("en_US");
                        return d < 0 ? "Same as the region (" + en.dayName(weekGroup.parent.l.firstDayOfWeek % 7) + ")"
                                     : en.dayName(d);
                    })
                    currentIndex: Math.max(0, weekGroup.days.indexOf(sheet.settings.firstDayOfWeek))
                    onActivated: (i) => sheet.set("firstDayOfWeek", weekGroup.days[i])
                }
            }
        }
    }
}
