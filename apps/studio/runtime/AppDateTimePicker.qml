import QtQuick
import Win95Ui

// DateTimePicker: a date, a date and time, or a time (Mode). Typed in, or
// picked: a date from a month calendar (▼, F4 or Alt+Down), a time with
// ▲▼ (Up/Down step it: a day, or MinuteStep minutes for a time).
//
// `value` is kept as the database keeps it: "yyyy-MM-dd", "HH:mm:ss" or
// "yyyy-MM-ddTHH:mm:ss" ("" for none); `text` is what shows (DateFormat,
// TimeFormat: "Short", "Long", a pattern like "dd/MM/yyyy" or "h:mm AP",
// or Custom with CustomDateFormat / CustomTimeFormat). With a TimeZone the
// time shows and is picked in that zone (the value stays this computer's).
//
// EarliestDate and LatestDate limit the days (whole days; "today",
// "today+30" work), EarliestTime and LatestTime the time of day ("09:00",
// "17:30"). With a DataField it edits that field, and a blank Earliest or
// Latest Date is the field's own.
//
//   picker1.value = "today+7"      picker1.date (a JavaScript Date)
//   picker1.clear()                picker1.openCalendar()
AppElement {
    id: picker

    property string mode: "Date"
    property string value: ""
    property string earliestDate: ""
    property string latestDate: ""
    property string earliestTime: ""
    property string latestTime: ""
    property int minuteStep: 1
    property string dateFormat: "Short"
    property string timeFormat: "Short"
    property string customDateFormat: ""
    property string customTimeFormat: ""
    property string timeZone: ""
    property string firstDayOfWeek: "Sunday"
    property bool allowBlank: true
    signal changed()

    defaultBack: Theme.window
    activeFocusOnTab: !designMode
    resetProperties: ["value"]

    readonly property bool hasDate: mode !== "Time"
    readonly property bool hasTime: mode !== "Date"
    readonly property string kind: mode === "Date" ? "Date" : mode === "Time" ? "Time" : "Date and Time"
    readonly property string zone: hasTime ? timeZone : ""
    readonly property string datePattern: dateFormat === "Custom" ? (customDateFormat || "yyyy-MM-dd") : dateFormat
    readonly property string timePattern: timeFormat === "Custom" ? (customTimeFormat || "HH:mm") : timeFormat
    readonly property string text: value === "" ? "" : AppDates.display(value, kind, datePattern, timePattern, zone)
    // the value as a JavaScript Date (null for none)
    readonly property var date: value === "" ? null : isoDate(AppDates.toZone(value, kind, ""))

    // the limits, as the picker shows things (in its zone): "yyyy-MM-dd" and "HH:mm:ss"
    readonly property string fieldMin: bound && (fieldInfo.type === "date" || fieldInfo.type === "datetime") && fieldInfo.min !== undefined ? String(fieldInfo.min) : ""
    readonly property string fieldMax: bound && (fieldInfo.type === "date" || fieldInfo.type === "datetime") && fieldInfo.max !== undefined ? String(fieldInfo.max) : ""
    readonly property string minDay: AppDates.stored(earliestDate !== "" ? earliestDate : fieldMin, "Date")
    readonly property string maxDay: AppDates.stored(latestDate !== "" ? latestDate : fieldMax, "Date")
    readonly property string minTime: AppDates.stored(earliestTime, "Time")
    readonly property string maxTime: AppDates.stored(latestTime, "Time")

    function clear() { setValue(""); }
    function setToday() { setValue(AppDates.stored("now", kind)); }

    // "yyyy-MM-ddTHH:mm:ss" (as shown) <-> a JavaScript Date of those numbers
    function isoDate(iso) {
        if (!iso)
            return null;
        var m = String(iso).match(/^(\d{4})-(\d\d)-(\d\d)(?:T(\d\d):(\d\d)(?::(\d\d))?)?$/);
        return m ? new Date(+m[1], +m[2] - 1, +m[3], +(m[4] || 0), +(m[5] || 0), +(m[6] || 0)) : null;
    }
    function pad(n) { return (n < 10 ? "0" : "") + n; }
    function isoOf(d) {
        return d.getFullYear() + "-" + pad(d.getMonth() + 1) + "-" + pad(d.getDate())
               + "T" + pad(d.getHours()) + ":" + pad(d.getMinutes()) + ":" + pad(d.getSeconds());
    }
    // the value as shown (in its zone), as a Date; now if there's none
    function shownDate() {
        var d = value === "" ? null : isoDate(AppDates.toZone(value, kind, zone));
        if (d)
            return d;
        var now = isoDate(AppDates.toZone(AppDates.stored("now", "Date and Time"), "Date and Time", zone));
        if (!hasTime)
            now.setHours(0, 0, 0, 0);
        else
            now.setSeconds(0);
        return now;
    }
    function dayAllowed(d) {
        var day = isoOf(d).substring(0, 10);
        return (minDay === "" || day >= minDay) && (maxDay === "" || day <= maxDay);
    }
    // inside the limits: the nearest day and time that are
    function clamped(d) {
        var out = new Date(d.getTime());
        if (hasDate) {
            var day = isoOf(out).substring(0, 10);
            if (minDay !== "" && day < minDay)
                out = withDay(out, isoDate(minDay));
            else if (maxDay !== "" && day > maxDay)
                out = withDay(out, isoDate(maxDay));
        }
        if (hasTime) {
            var t = isoOf(out).substring(11);
            if (minTime !== "" && t < minTime)
                out = withTime(out, minTime);
            else if (maxTime !== "" && t > maxTime)
                out = withTime(out, maxTime);
        }
        return out;
    }
    function withDay(d, day) { return new Date(day.getFullYear(), day.getMonth(), day.getDate(), d.getHours(), d.getMinutes(), d.getSeconds()); }
    function withTime(d, t) {
        var p = t.split(":");
        return new Date(d.getFullYear(), d.getMonth(), d.getDate(), +p[0], +p[1], +(p[2] || 0));
    }
    // a Date as shown -> the value
    function setShown(d) {
        var c = clamped(d);
        setValue(AppDates.fromZone(isoOf(c), kind, zone));
    }
    function setValue(v) {
        v = v === null || v === undefined ? "" : String(v);
        if (v === value)
            return;
        value = v;
    }
    // a step: a day (Date, Date and Time) or MinuteStep minutes (Time)
    function step(n, minutes) {
        var d = shownDate();
        if (minutes || !hasDate)
            d = new Date(d.getFullYear(), d.getMonth(), d.getDate(), d.getHours(), d.getMinutes() + n * Math.max(1, minuteStep), d.getSeconds());
        else
            d = new Date(d.getFullYear(), d.getMonth(), d.getDate() + n, d.getHours(), d.getMinutes(), d.getSeconds());
        setShown(d);
    }

    // value <-> the field, and what code set it to ("today+7" is read so)
    property bool loading: false
    onValueChanged: {
        // ("today+7", or anything else that can be read: as it's kept; what can't be: none)
        var kept = value === "" ? "" : AppDates.stored(value, kind);
        if (kept !== value) {
            value = kept;
            return;
        }
        if (!editor.activeFocus)
            editor.text = text;
        if (loading || designMode)
            return;
        if (bound)
            store(value === "" ? null : value);
        changed();
    }
    onFieldValueChanged: showField()
    onKindChanged: showField()
    function showField() {
        if (!bound)
            return;
        loading = true;
        value = fieldValue === undefined || fieldValue === null ? "" : AppDates.stored(fieldValue, kind);
        loading = false;
        editor.text = text;
    }
    onTextChanged: if (!editor.activeFocus) editor.text = text
    Component.onCompleted: {
        loading = true;
        if (value !== "")
            value = AppDates.stored(value, kind);
        loading = false;
        editor.text = text;
        showField();
    }

    // what was typed, read when Enter is pressed or the keyboard leaves
    function commitTyped() {
        // (what shows, untouched, isn't read again: it may have lost something,
        // like a two-digit year's century)
        if (editor.text === text)
            return;
        var typed = editor.text.trim();
        if (typed === "") {
            if (allowBlank)
                setValue("");
            editor.text = text;
            return;
        }
        var read = AppDates.read(typed, kind, datePattern, timePattern, zone);
        if (read !== "")
            setShown(isoDate(AppDates.toZone(read, kind, zone)));
        editor.text = text;
    }

    Bevel {
        anchors.fill: parent
        style: "sunken"
        face: picker.fillColor
    }

    TextInput {
        id: editor
        anchors.fill: parent
        anchors.leftMargin: 4
        anchors.rightMargin: buttons.width + 4
        verticalAlignment: TextInput.AlignVCenter
        clip: true
        enabled: !picker.designMode && picker.enabled
        selectByMouse: true
        focus: true
        color: picker.designMode && picker.value === "" ? Theme.shadow : picker.textColor
        selectionColor: Theme.selection
        selectedTextColor: Theme.selectionText
        font.family: picker.family
        font.pixelSize: picker.pixelSize
        font.bold: picker.fontBold
        font.italic: picker.fontItalic
        font.underline: picker.fontUnderline
        onAccepted: picker.commitTyped()
        onActiveFocusChanged: {
            if (!activeFocus)
                picker.commitTyped();
        }
    }
    // (on the canvas: what it'll look like, with today when there's no Value)
    Text {
        visible: picker.designMode
        anchors.fill: editor
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        text: picker.value !== "" ? picker.text : AppDates.display(AppDates.stored("now", picker.kind), picker.kind,
                                                                    picker.datePattern, picker.timePattern, picker.zone)
        color: picker.value !== "" ? picker.textColor : Theme.shadow
        font: editor.font
    }

    Row {
        id: buttons
        anchors.right: parent.right
        anchors.rightMargin: 2
        anchors.verticalCenter: parent.verticalCenter
        height: parent.height - 4
        // ▼: the calendar
        Item {
            visible: picker.hasDate
            width: visible ? 16 : 0
            height: parent.height
            Bevel { anchors.fill: parent; style: dropMouse.pressed ? "thinSunken" : "raised" }
            Text { anchors.centerIn: parent; text: "▼"; font.pixelSize: 7; color: picker.enabled ? Theme.text : Theme.disabledText }
            MouseArea {
                id: dropMouse
                anchors.fill: parent
                enabled: !picker.designMode && picker.enabled
                onPressed: picker.openCalendar()
            }
        }
        // ▲▼: a time
        Column {
            visible: !picker.hasDate
            width: visible ? 16 : 0
            height: parent.height
            Repeater {
                model: [1, -1]
                delegate: Item {
                    required property int modelData
                    width: 16
                    height: buttons.height / 2
                    Bevel { anchors.fill: parent; style: spinMouse.pressed ? "thinSunken" : "raised" }
                    Text {
                        anchors.centerIn: parent
                        text: parent.modelData > 0 ? "▲" : "▼"
                        font.pixelSize: 6
                        color: picker.enabled ? Theme.text : Theme.disabledText
                    }
                    MouseArea {
                        id: spinMouse
                        anchors.fill: parent
                        enabled: !picker.designMode && picker.enabled
                        onPressed: {
                            picker.forceActiveFocus();
                            picker.step(parent.modelData, true);
                        }
                    }
                }
            }
        }
    }

    Keys.onPressed: (event) => {
        if (picker.designMode)
            return;
        if (picker.hasDate && (event.key === Qt.Key_F4 || (event.key === Qt.Key_Down && (event.modifiers & Qt.AltModifier)))) {
            picker.openCalendar();
        } else if (event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
            picker.commitTyped();
            picker.step(event.key === Qt.Key_Up ? 1 : -1, (event.modifiers & Qt.ControlModifier) !== 0);
            editor.selectAll();
        } else {
            return;
        }
        event.accepted = true;
    }

    // --- the calendar ------------------------------------------------------------

    function openCalendar() {
        if (!form || designMode || !hasDate)
            return;
        commitTyped();
        var d = shownDate();
        calendar.pick = d;
        calendar.month = new Date(d.getFullYear(), d.getMonth(), 1);
        calendar.timeText = AppDates.display(AppDates.stored(isoOf(d).substring(11), "Time"), "Time", "", timePattern, "");
        var p = picker.mapToItem(overlay, 0, picker.height);
        panel.x = Math.max(0, Math.min(p.x, overlay.width - panel.width));
        panel.y = p.y + panel.height > overlay.height ? Math.max(0, p.y - picker.height - panel.height) : p.y;
        overlay.visible = true;
        calendar.forceActiveFocus();
    }
    function closeCalendar(keep) {
        if (!overlay.visible)
            return;
        overlay.visible = false;
        if (keep)
            setShown(calendar.pickWithTime());
        picker.forceActiveFocus();
    }

    // over the whole form while it's open (a click outside closes it, keeping the day)
    Item {
        id: overlay
        parent: picker.form && picker.form.contentItem ? picker.form.contentItem : picker
        anchors.fill: parent
        visible: false
        z: 2500

        MouseArea {
            anchors.fill: parent
            onPressed: picker.closeCalendar(picker.hasTime)
        }

        Item {
            id: panel
            width: 7 * 26 + 12
            height: calendar.height + 6

            Bevel { anchors.fill: parent; style: "raised" }
            MouseArea { anchors.fill: parent } // (the panel itself isn't outside)

            FocusScope {
                id: calendar
                x: 3
                y: 3
                width: parent.width - 6
                height: footer.y + footer.height + 4
                property var month: new Date()
                property var pick: new Date()
                property string timeText: ""
                readonly property int weekStart: picker.firstDayOfWeek === "Monday" ? 1 : picker.firstDayOfWeek === "Saturday" ? 6 : 0
                readonly property var names: ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"]
                // the 42 days shown: from the week the month starts in
                readonly property var days: {
                    var first = new Date(month.getFullYear(), month.getMonth(), 1);
                    var back = (first.getDay() - weekStart + 7) % 7;
                    var out = [];
                    for (var i = 0; i < 42; ++i)
                        out.push(new Date(month.getFullYear(), month.getMonth(), 1 - back + i));
                    return out;
                }
                function sameDay(a, b) { return a && b && a.getFullYear() === b.getFullYear() && a.getMonth() === b.getMonth() && a.getDate() === b.getDate(); }
                function moveMonth(n) { month = new Date(month.getFullYear(), month.getMonth() + n, 1); }
                function choose(d) {
                    if (!picker.dayAllowed(d))
                        return;
                    pick = new Date(d.getFullYear(), d.getMonth(), d.getDate(), pick.getHours(), pick.getMinutes(), pick.getSeconds());
                    if (d.getMonth() !== month.getMonth())
                        month = new Date(d.getFullYear(), d.getMonth(), 1);
                    if (!picker.hasTime)
                        picker.closeCalendar(true);
                }
                // the day picked, at the time typed under it
                function pickWithTime() {
                    if (!picker.hasTime)
                        return pick;
                    var t = AppDates.read(timeText, "Time", "", picker.timePattern, "");
                    return t !== "" ? picker.withTime(pick, t) : pick;
                }
                function stepTime(n) {
                    var d = pickWithTime();
                    d = new Date(d.getFullYear(), d.getMonth(), d.getDate(), d.getHours(), d.getMinutes() + n * Math.max(1, picker.minuteStep), 0);
                    pick = new Date(pick.getFullYear(), pick.getMonth(), pick.getDate(), d.getHours(), d.getMinutes(), 0);
                    timeText = AppDates.display(AppDates.stored(picker.isoOf(d).substring(11), "Time"), "Time", "", picker.timePattern, "");
                }

                // Enter and Escape are the calendar's while it's open, not the form's
                // default button's (a Shortcut would have them first)
                Keys.onShortcutOverride: (event) => {
                    event.accepted = [Qt.Key_Return, Qt.Key_Enter, Qt.Key_Escape, Qt.Key_Left, Qt.Key_Right,
                                      Qt.Key_Up, Qt.Key_Down, Qt.Key_PageUp, Qt.Key_PageDown].indexOf(event.key) >= 0;
                }
                Keys.onPressed: (event) => {
                    var d = calendar.pick, by = 0;
                    if (event.key === Qt.Key_Left) by = -1;
                    else if (event.key === Qt.Key_Right) by = 1;
                    else if (event.key === Qt.Key_Up) by = -7;
                    else if (event.key === Qt.Key_Down) by = 7;
                    if (by !== 0) {
                        var n = new Date(d.getFullYear(), d.getMonth(), d.getDate() + by, d.getHours(), d.getMinutes(), d.getSeconds());
                        if (picker.dayAllowed(n)) {
                            calendar.pick = n;
                            if (n.getMonth() !== calendar.month.getMonth() || n.getFullYear() !== calendar.month.getFullYear())
                                calendar.month = new Date(n.getFullYear(), n.getMonth(), 1);
                        }
                    } else if (event.key === Qt.Key_PageUp || event.key === Qt.Key_PageDown) {
                        calendar.moveMonth(event.key === Qt.Key_PageUp ? -1 : 1);
                    } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                        picker.closeCalendar(true);
                    } else if (event.key === Qt.Key_Escape) {
                        picker.closeCalendar(false);
                    } else {
                        return;
                    }
                    event.accepted = true;
                }

                // the title: ◄ November 2026 ►
                Rectangle {
                    id: title
                    width: parent.width
                    height: 22
                    color: Theme.selection
                    Repeater {
                        model: [-1, 1]
                        delegate: Item {
                            required property int modelData
                            x: modelData < 0 ? 3 : title.width - width - 3
                            y: 3
                            width: 18
                            height: 16
                            Bevel { anchors.fill: parent; style: monthMouse.pressed ? "thinSunken" : "raised" }
                            Text { anchors.centerIn: parent; text: parent.modelData < 0 ? "◄" : "►"; font.pixelSize: 8; color: Theme.text }
                            MouseArea { id: monthMouse; anchors.fill: parent; onClicked: calendar.moveMonth(parent.modelData) }
                        }
                    }
                    Text {
                        anchors.centerIn: parent
                        text: Qt.locale().standaloneMonthName(calendar.month.getMonth()) + " " + calendar.month.getFullYear()
                        color: Theme.selectionText
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                        font.bold: true
                    }
                }
                // the days of the week
                Row {
                    id: weekNames
                    y: title.height + 3
                    Repeater {
                        model: 7
                        delegate: Text {
                            required property int index
                            width: 26
                            horizontalAlignment: Text.AlignHCenter
                            text: calendar.names[(calendar.weekStart + index) % 7]
                            color: Theme.text
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSize - 1
                        }
                    }
                }
                Rectangle { y: weekNames.y + weekNames.height + 1; x: 2; width: parent.width - 4; height: 1; color: Theme.shadow }
                Grid {
                    id: dayGrid
                    y: weekNames.y + weekNames.height + 4
                    columns: 7
                    Repeater {
                        model: calendar.days
                        delegate: Item {
                            id: dayCell
                            required property var modelData
                            readonly property bool inMonth: modelData.getMonth() === calendar.month.getMonth()
                            readonly property bool allowed: picker.dayAllowed(modelData)
                            readonly property bool picked: calendar.sameDay(modelData, calendar.pick)
                            readonly property bool today: calendar.sameDay(modelData, new Date())
                            width: 26
                            height: 18
                            Rectangle {
                                anchors.fill: parent
                                anchors.margins: 1
                                color: dayCell.picked ? Theme.selection : "transparent"
                            }
                            Rectangle {
                                visible: dayCell.today
                                anchors.fill: parent
                                color: "transparent"
                                border.color: "#c00000"
                                border.width: 1
                                radius: 6
                            }
                            Text {
                                anchors.centerIn: parent
                                text: dayCell.modelData.getDate()
                                color: dayCell.picked ? Theme.selectionText
                                     : !dayCell.allowed ? Theme.disabledText
                                     : dayCell.inMonth ? Theme.text : Theme.shadow
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSize
                            }
                            MouseArea {
                                anchors.fill: parent
                                enabled: dayCell.allowed
                                onClicked: calendar.choose(dayCell.modelData)
                            }
                        }
                    }
                }
                // Date and Time: the time, under the days
                Row {
                    id: timeRow
                    visible: picker.hasTime
                    y: dayGrid.y + dayGrid.height + 4
                    x: 4
                    height: visible ? 21 : 0
                    spacing: 4
                    Text {
                        height: 21
                        verticalAlignment: Text.AlignVCenter
                        text: "Time:"
                        color: Theme.text
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                    }
                    TextField95 {
                        id: timeField
                        width: 80
                        height: 21
                        text: calendar.timeText
                        onTextChanged: calendar.timeText = text
                        onAccepted: picker.closeCalendar(true)
                        Keys.onShortcutOverride: (event) => {
                            event.accepted = event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Escape;
                        }
                    }
                    Column {
                        Repeater {
                            model: [1, -1]
                            delegate: Item {
                                required property int modelData
                                width: 16
                                height: 10
                                Bevel { anchors.fill: parent; style: timeMouse.pressed ? "thinSunken" : "raised" }
                                Text { anchors.centerIn: parent; text: parent.modelData > 0 ? "▲" : "▼"; font.pixelSize: 6; color: Theme.text }
                                MouseArea { id: timeMouse; anchors.fill: parent; onPressed: calendar.stepTime(parent.modelData) }
                            }
                        }
                    }
                    Button95 { width: 40; height: 21; text: "OK"; onClicked: picker.closeCalendar(true) }
                }
                // Today: and (for a blank-able one) None
                Item {
                    id: footer
                    y: (timeRow.visible ? timeRow.y + timeRow.height : dayGrid.y + dayGrid.height) + 3
                    width: parent.width
                    height: 18
                    Text {
                        id: todayText
                        x: 4
                        height: parent.height
                        verticalAlignment: Text.AlignVCenter
                        text: "Today: " + AppDates.display(AppDates.stored("today", "Date"), "Date", picker.datePattern, "", "")
                        color: picker.dayAllowed(new Date()) ? Theme.text : Theme.disabledText
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                        font.bold: true
                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                var now = new Date();
                                calendar.month = new Date(now.getFullYear(), now.getMonth(), 1);
                                calendar.choose(now);
                            }
                        }
                    }
                    Text {
                        visible: picker.allowBlank
                        anchors.right: parent.right
                        anchors.rightMargin: 4
                        height: parent.height
                        verticalAlignment: Text.AlignVCenter
                        text: "None"
                        color: Theme.text
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                        font.underline: true
                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                overlay.visible = false;
                                picker.clear();
                                picker.forceActiveFocus();
                            }
                        }
                    }
                }
            }
        }
    }
}
