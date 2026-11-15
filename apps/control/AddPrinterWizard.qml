import QtQuick
import QtQuick.Window
import Win95Ui
import Win95Control

// The Add Printer Wizard, like Win95's: a picture on the left, one
// question per page, and Back / Next / Cancel. Pages: welcome, which
// printer (the ones CUPS finds, or an address), its driver (driverless for
// modern printers), its name, and a test page.
Window {
    id: win

    width: 520
    height: 380
    minimumWidth: width
    maximumWidth: width
    minimumHeight: height
    maximumHeight: height
    visible: true
    color: Theme.face
    title: "Add Printer Wizard"

    property int page: 0
    readonly property int lastPage: 4

    // what's been chosen
    property var devices: []
    property bool searched: false
    property string deviceKey: ""     // a device's uri
    property string typedUri: ""
    readonly property var device: {
        for (var i = 0; i < devices.length; ++i)
            if (devices[i].uri === deviceKey)
                return devices[i];
        return null;
    }
    readonly property string uri: typedUri.trim() || (device ? device.uri : "")
    readonly property bool driverlessPossible: /^(ipps?|dnssd|http|https):/.test(uri)

    property var drivers: []
    property bool driversLoaded: false
    property string driverFilter: ""
    property string driver: ""
    readonly property var driverChoices: {
        var list = [];
        if (driverlessPossible)
            list.push({ key: "everywhere", name: "Driverless (IPP Everywhere): recommended", makeModel: "" });
        var words = driverFilter.toLowerCase().split(/\s+/).filter(function (w) { return w; });
        for (var i = 0; i < drivers.length && list.length < 400; ++i) {
            var d = drivers[i];
            var text = d.makeModel.toLowerCase();
            if (words.every(function (w) { return text.indexOf(w) >= 0; }))
                list.push({ key: d.name, name: d.makeModel });
        }
        return list;
    }

    property string printerName: ""
    property string comment: ""
    property bool makeDefault: Printers.printers.length === 0
    property bool testPage: true

    function next() {
        if (page === 1) {
            // into the driver page: pick a likely one
            if (!driversLoaded)
                Printers.findDrivers();
            if (!driver || (driver === "everywhere" && !driverlessPossible))
                driver = driverlessPossible ? "everywhere" : bestDriver();
            if (!driverFilter && device && device.makeModel && driver !== "everywhere")
                driverFilter = device.makeModel.split(" ")[0];
        }
        if (page === 2) {
            var desc = device ? (device.makeModel || device.info) : uri.replace(/^[a-z]+:\/\//, "");
            if (!printerName)
                printerName = Printers.suggestName(desc);
            if (!comment)
                comment = desc;
        }
        if (page === lastPage) {
            finish();
            return;
        }
        page++;
    }
    function bestDriver() {
        if (!device || !device.makeModel)
            return "";
        var want = device.makeModel.toLowerCase();
        for (var i = 0; i < drivers.length; ++i)
            if (drivers[i].makeModel.toLowerCase().indexOf(want) === 0)
                return drivers[i].name;
        return "";
    }
    readonly property bool canGoOn: page === 1 ? uri.length > 0
                                  : page === 2 ? driver.length > 0
                                  : page === 3 ? printerName.trim().length > 0
                                  : true

    function finish() {
        Printers.addPrinter(printerName.trim(), uri, driver, comment, "", makeDefault);
    }

    Component.onCompleted: Printers.findDevices()
    Connections {
        target: Printers
        function onDevicesFound(list) {
            win.devices = list;
            win.searched = true;
            // a real printer, not a bare serial or parallel port
            if (!win.deviceKey && list.length && list[0].kind !== "other")
                win.deviceKey = list[0].uri;
        }
        function onDriversFound(list) {
            win.drivers = list;
            win.driversLoaded = true;
            if (!win.driver)
                win.driver = win.driverlessPossible ? "everywhere" : win.bestDriver();
        }
        function onAddFinished(ok, message) {
            if (!ok) {
                msgBox.show(win.title, "The printer couldn't be added.\n\n" + message, "error", ["OK"]);
                return;
            }
            if (win.testPage)
                Printers.printTestPage(win.printerName.trim());
            win.close();
        }
    }

    Shortcut { sequence: "Esc"; enabled: !msgBox.visible; onActivated: win.close() }

    component Para: Text {
        width: parent ? parent.width : 300
        wrapMode: Text.Wrap
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }

    // ------------------------------------------------------- the picture
    Bevel {
        x: 12
        y: 12
        width: 150
        height: parent.height - 70
        style: "sunken"
        Rectangle {
            anchors.fill: parent
            anchors.margins: 2
            color: "#008080"
            // a desk, a computer and a printer, in Win95's wizard style
            Rectangle { x: 0; y: parent.height * 0.62; width: parent.width; height: parent.height * 0.38; color: "#006060" }
            PrinterIcon {
                x: 50
                y: parent.height * 0.62 - 50
                size: 64
                kind: "add"
            }
        }
    }

    // ------------------------------------------------------------ pages
    Item {
        id: pages
        x: 178
        y: 16
        width: parent.width - x - 14
        height: parent.height - y - 64

        // 0: welcome
        Column {
            visible: win.page === 0
            width: parent.width
            spacing: 14
            Para { text: "This wizard will help you to install your printer quickly and easily." }
            Para { text: "It finds the printers connected to this computer (USB) and on your network, and sets up the one you choose." }
            Para { text: "To begin installing your printer, click Next." }
        }

        // 1: which printer
        Item {
            visible: win.page === 1
            anchors.fill: parent
            Para {
                id: deviceHelp
                text: Printers.busy && !win.searched ? "Looking for printers. This takes a few seconds..."
                    : "Click the printer you want to install."
            }
            ProgramList {
                id: deviceList
                y: deviceHelp.height + 8
                width: parent.width
                height: 150
                model: win.devices.map(function (d) {
                    return { key: d.uri, name: d.makeModel || d.info, summary: d.uri, kind: d.kind };
                })
                selectedKey: win.typedUri ? "" : win.deviceKey
                onSelectedKeyChanged: if (selectedKey) { win.deviceKey = selectedKey; win.typedUri = ""; uriField.text = ""; }
                iconComponent: PrinterIcon { size: parent.size }
                noteFor: function (item) { return item.kind === "network" ? "Network" : item.kind === "usb" ? "USB" : ""; }
                emptyText: win.searched ? "No printers were found. Check that the printer is on and connected, then click Search Again, or type its address below." : ""
                onActivated: win.next()
            }
            Button95 {
                anchors.right: parent.right
                y: deviceList.y + deviceList.height + 6
                width: 96
                text: "<u>S</u>earch Again"
                enabled: !Printers.busy
                onClicked: { win.searched = false; Printers.findDevices(); }
            }
            Para {
                y: deviceList.y + deviceList.height + 10
                width: parent.width - 110
                text: "Or type its address:"
            }
            TextField95 {
                id: uriField
                y: deviceList.y + deviceList.height + 36
                width: parent.width
                height: 21
                Connections {
                    target: uriField.input
                    function onTextEdited() { win.typedUri = uriField.text; }
                }
            }
            Para {
                y: uriField.y + 26
                color: Theme.shadow
                text: "For example ipp://192.168.1.20/ipp/print or socket://192.168.1.20"
            }
        }

        // 2: the driver
        Item {
            visible: win.page === 2
            anchors.fill: parent
            Para {
                id: driverHelp
                text: win.driverlessPossible
                      ? "This printer can print without a driver, which works best for modern printers. Or choose its manufacturer and model:"
                      : "Choose your printer's manufacturer and model. Type part of the name to find it."
            }
            TextField95 {
                id: driverSearch
                y: driverHelp.height + 8
                width: parent.width
                height: 21
                text: win.driverFilter
                Connections {
                    target: driverSearch.input
                    function onTextEdited() { win.driverFilter = driverSearch.text; }
                }
            }
            ProgramList {
                y: driverSearch.y + 28
                width: parent.width
                height: parent.height - y
                model: win.driverChoices
                selectedKey: win.driver
                onSelectedKeyChanged: if (selectedKey) win.driver = selectedKey
                iconComponent: PrinterIcon { size: parent.size }
                emptyText: !win.driversLoaded ? "Loading the list of drivers..." : "No driver matches. Try fewer letters."
                onActivated: win.next()
            }
        }

        // 3: the name
        Column {
            visible: win.page === 3
            width: parent.width
            spacing: 8
            Para { text: "You can type a name for this printer. Programs show it in their Print dialogs. It can't contain spaces." }
            TextField95 {
                id: nameField
                width: parent.width
                height: 21
                text: win.printerName
                Connections {
                    target: nameField.input
                    function onTextEdited() { win.printerName = nameField.text.replace(/[\s\/#\\'"?]/g, "_"); }
                }
            }
            Para { text: "A comment, to tell printers apart (optional):" }
            TextField95 {
                id: commentField
                width: parent.width
                height: 21
                text: win.comment
                Connections {
                    target: commentField.input
                    function onTextEdited() { win.comment = commentField.text; }
                }
            }
            Item { width: 1; height: 6 }
            Para { text: "Do you want your programs to use this printer as the default printer?" }
            RadioButton95 { text: "<u>Y</u>es"; checked: win.makeDefault; onClicked: win.makeDefault = true }
            RadioButton95 { text: "<u>N</u>o"; checked: !win.makeDefault; onClicked: win.makeDefault = false }
        }

        // 4: test page
        Column {
            visible: win.page === 4
            width: parent.width
            spacing: 10
            Para { text: "After your printer is installed, Wayland 95 can print a test page so you can confirm that the printer is set up properly." }
            Para { text: "Would you like to print a test page?" }
            RadioButton95 { text: "<u>Y</u>es (recommended)"; checked: win.testPage; onClicked: win.testPage = true }
            RadioButton95 { text: "<u>N</u>o"; checked: !win.testPage; onClicked: win.testPage = false }
            Item { width: 1; height: 6 }
            Para {
                color: Theme.shadow
                text: "Printer: " + win.printerName + "\nConnected to: " + win.uri
                      + "\nDriver: " + (win.driver === "everywhere" ? "driverless (IPP Everywhere)"
                                        : (win.driverChoices.filter(function (d) { return d.key === win.driver; })[0] || { name: win.driver }).name)
            }
        }
    }

    // ---------------------------------------------------------- buttons
    Rectangle { x: 12; y: parent.height - 48; width: parent.width - 24; height: 1; color: Theme.shadow }
    Rectangle { x: 12; y: parent.height - 47; width: parent.width - 24; height: 1; color: Theme.highlight }
    Row {
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 10
        spacing: 0
        Button95 {
            text: "&lt; <u>B</u>ack"
            enabled: win.page > 0 && !Printers.busy
            onClicked: win.page--
        }
        Button95 {
            text: win.page === win.lastPage ? "Finish" : "<u>N</u>ext &gt;"
            isDefault: true
            enabled: win.canGoOn && !(win.page === win.lastPage && Printers.busy)
            onClicked: win.next()
        }
        Item { width: 8; height: 1 }
        Button95 {
            text: "Cancel"
            onClicked: win.close()
        }
    }
    Text {
        x: 14
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 15
        visible: Printers.busy && win.page === win.lastPage
        text: "Installing the printer..."
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }

    Menu95 { id: popup }
    MessageBox95 { id: msgBox }
}
