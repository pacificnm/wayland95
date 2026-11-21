import QtQuick
import Win95Ui
import Win95Control

// Ollama's settings: its models (the ones on this computer, getting more
// with progress, removing them; the two Athenaeum needs a click away), and
// how it runs: who may connect, keeping two models loaded, how long a model
// stays in memory, updating Ollama, AMD graphics support, removing it.
// Models go through Ollama's own API as you (Ollama), the rest through
// wayland95-services (an administrator's password).
PropertySheet {
    id: sheet

    title: "Ollama Properties"
    titles: ["Models", "Settings"]
    height: 470

    readonly property var service: Services.services.filter(function (s) { return s.id === "ollama"; })[0] || ({})
    // the models Athenaeum uses (its defaults): answers, and linking sentences to sources
    readonly property var athenaeumModels: ["qwen3.5:2b", "nomic-embed-text"]
    readonly property var suggestions: [
        { name: "qwen3.5:2b", what: "Small and quick: Athenaeum's answers" },
        { name: "nomic-embed-text", what: "Athenaeum's embedding model (ranks passages, cites)" },
        { name: "llama3.2:3b", what: "Meta's small Llama, for chatting" },
        { name: "gemma3:4b", what: "Google's Gemma 3, a little larger" },
        { name: "mistral", what: "Mistral 7B, for a computer with more memory" },
    ]
    property var queue: []          // models waiting to be got, after this one
    property bool startedHere: false

    // ---- settings (wayland95-services ollama-config), as the drop-in file has them
    property string listen: "local"
    property bool keepModels: false
    property int keepAlive: 5
    function loadConfig() {
        var text = Services.readFile("/etc/systemd/system/ollama.service.d/wayland95.conf");
        listen = /OLLAMA_HOST=0\.0\.0\.0/.test(text) ? "all" : "local";
        keepModels = /OLLAMA_MAX_LOADED_MODELS=2/.test(text);
        var m = text.match(/OLLAMA_KEEP_ALIVE=(\d+)m/);
        keepAlive = m ? parseInt(m[1]) : 5;
        dirty = false;
    }
    Component.onCompleted: { loadConfig(); Ollama.refresh(); }
    onApplyRequested: {
        if (!sheet.service.installed)
            return;
        Services.admin(["ollama-config"], "listen=" + listen + "\nkeep_models=" + (keepModels ? "yes" : "no") + "\nkeep_alive=" + keepAlive,
                       function (ok, out) {
                           if (!ok)
                               sheet.messageBox.show("Ollama", out, "error", ["OK"]);
                           sheet.loadConfig();
                           Ollama.refresh();
                       });
    }

    // ---- models
    function has(name) {
        return Ollama.models.some(function (m) { return m.name === name || m.name === name + ":latest"; });
    }
    readonly property var missingForAthenaeum: { Ollama.models; return athenaeumModels.filter(function (n) { return !has(n); }); }
    function getModels(names) {
        names = names.filter(function (n) { return n !== "" && !has(n); });
        if (!names.length)
            return;
        if (Ollama.pulling) {
            queue = queue.concat(names);
            return;
        }
        queue = names.slice(1);
        Ollama.pull(names[0]);
    }
    function getTyped() {
        var name = nameField.text.trim();
        if (name === "")
            return;
        if (!Ollama.validName(name)) {
            sheet.messageBox.show("Ollama", "“" + name + "” isn't a model's name. Names are like qwen3.5:2b or llama3.2 "
                                  + "(see ollama.com/library).", "warning", ["OK"]);
            return;
        }
        nameField.text = "";
        getModels([name]);
    }
    function removeModel(m) {
        sheet.messageBox.show("Ollama", "Remove " + m.name + " (" + sizeText(m.size) + ") from this computer?\n\n"
                              + "You can get it again any time; it's downloaded again.",
                              "question", ["Yes", "No"], function (b) { if (b === 0) Ollama.remove(m.name); });
    }
    Connections {
        target: Ollama
        function onPullFinished(ok, message) {
            if (!ok) {
                sheet.queue = [];
                sheet.messageBox.show("Ollama", message, "error", ["OK"]);
            } else if (sheet.queue.length) {
                var next = sheet.queue[0];
                sheet.queue = sheet.queue.slice(1);
                Ollama.pull(next);
            }
        }
        function onRemoved(ok, message) {
            if (!ok)
                sheet.messageBox.show("Ollama", message, "error", ["OK"]);
        }
    }
    Connections {
        target: Services
        function onTaskFinished(ok, message) {
            if (!sheet.startedHere)
                return;
            sheet.startedHere = false;
            sheet.messageBox.show("Ollama", ok ? (message || "Done.") : message, ok ? "info" : "error", ["OK"]);
            sheet.loadConfig();
            Ollama.refresh();
        }
    }
    // the models in memory change as programs use them
    Timer { interval: 4000; repeat: true; running: sheet.visible && !Ollama.pulling; onTriggered: Ollama.refresh() }

    function sizeText(n) {
        if (n < 1e6) return Math.round(n / 1e3) + " KB";
        if (n < 1e9) return Math.round(n / 1e6) + " MB";
        return (n / 1e9).toFixed(1) + " GB";
    }
    function loadedNote(name) {
        for (var i = 0; i < Ollama.loaded.length; ++i)
            if (Ollama.loaded[i].name === name)
                return Ollama.loaded[i].vram > 0 ? "In memory (graphics card)" : "In memory";
        return "";
    }

    component Label: Text {
        wrapMode: Text.Wrap
        textFormat: Text.StyledText
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }

    // ------------------------------------------------------------ Models
    Item {
        AppletIcon { x: 4; y: 2; kind: "ollama" }
        Label {
            x: 46
            y: 2
            width: parent.width - x
            text: !sheet.service.installed ? "Ollama isn't installed. Turn it on in Services to install it."
                : Ollama.reachable ? "Ollama " + Ollama.version + " is running. Programs on this computer can use these models."
                : Ollama.checked ? "Ollama is installed but isn't running. Start it in Services to get or use models."
                : "Looking for Ollama..."
        }

        Label {
            y: 40
            text: "<u>M</u>odels on this computer:"
        }
        ProgramList {
            id: modelList
            y: 56
            width: parent.width
            height: 126
            model: Ollama.models.map(function (m) {
                return { key: m.name, name: m.name + (m.parameters ? "  (" + m.parameters + ")" : ""), model: m };
            })
            iconComponent: AppletIcon { size: parent.size; kind: "ollama" }
            noteFor: function (item) {
                var l = sheet.loadedNote(item.key);
                return (l ? l + ", " : "") + sheet.sizeText(item.model.size);
            }
            emptyText: Ollama.reachable ? "No models yet. Get one below." : ""
            onModelChanged: if (!selected && model.length) selectedKey = model[0].key
        }
        Row {
            y: modelList.y + modelList.height + 4
            anchors.right: parent.right
            spacing: 6
            Button95 {
                text: "<u>R</u>emove"
                enabled: Ollama.reachable && !!modelList.selected && !(Ollama.pulling && Ollama.pullModel === modelList.selectedKey)
                onClicked: sheet.removeModel(modelList.selected.model)
            }
        }

        GroupBox95 {
            id: getBox
            y: modelList.y + modelList.height + 32
            width: parent.width
            height: 112
            title: "Get a model"
            Label {
                width: parent.width
                text: "Name (from ollama.com/library):"
            }
            TextField95 {
                id: nameField
                y: 18
                width: parent.width - 150
                enabled: Ollama.reachable
                onAccepted: sheet.getTyped()
            }
            Button95 {
                x: nameField.width + 6
                y: 17
                width: 70
                text: "Suggest"
                enabled: Ollama.reachable
                onClicked: {
                    var p = mapToItem(sheet.popupMenu, 0, height);
                    sheet.popupMenu.popup(p.x, p.y, sheet.suggestions.map(function (s) {
                        return { text: s.name + "  –  " + s.what, enabled: !sheet.has(s.name),
                                 action: function () { nameField.text = s.name; } };
                    }));
                }
            }
            Button95 {
                x: nameField.width + 82
                y: 17
                width: 62
                text: "<u>G</u>et"
                enabled: Ollama.reachable && nameField.text.trim() !== ""
                onClicked: sheet.getTyped()
            }
            // the download under way
            Label {
                y: 46
                width: parent.width - 70
                elide: Text.ElideRight
                wrapMode: Text.NoWrap
                visible: Ollama.pulling
                text: Ollama.pullModel + ": " + Ollama.pullStatus
                      + (Ollama.pullTotal > 0 ? "  " + sheet.sizeText(Ollama.pullDone) + " of " + sheet.sizeText(Ollama.pullTotal) : "")
                      + (sheet.queue.length ? "  (then " + sheet.queue.join(", ") + ")" : "")
            }
            Button95 {
                anchors.right: parent.right
                y: 44
                width: 60
                visible: Ollama.pulling
                text: "Stop"
                onClicked: { sheet.queue = []; Ollama.cancelPull(); }
            }
            ProgressBar95 {
                y: 68
                width: parent.width
                height: 16
                visible: Ollama.pulling
                value: Ollama.pullPercent
            }
            Label {
                y: 46
                width: parent.width
                visible: !Ollama.pulling
                color: Theme.shadow
                text: "Models are large (most are 1 to 5 GB) and download from ollama.com."
            }
        }

        // what Athenaeum needs
        Label {
            id: athenaeumNote
            y: getBox.y + getBox.height + 8
            width: parent.width - 110
            visible: Ollama.reachable && sheet.missingForAthenaeum.length > 0
            text: "Athenaeum needs " + sheet.missingForAthenaeum.map(function (n) { return "<b>" + n + "</b>"; }).join(" and ") + "."
        }
        Button95 {
            anchors.right: parent.right
            y: athenaeumNote.y - 3
            width: 100
            visible: athenaeumNote.visible
            text: "Get " + (sheet.missingForAthenaeum.length > 1 ? "Them" : "It")
            enabled: !Ollama.pulling
            onClicked: sheet.getModels(sheet.missingForAthenaeum)
        }
    }

    // ------------------------------------------------------------ Settings
    Item {
        AppletIcon { x: 4; y: 2; kind: "ollama" }
        Label {
            x: 46
            y: 2
            width: parent.width - x
            text: sheet.service.installed
                  ? "Ollama " + (sheet.service.version || "") + " is installed in /usr/local, with its models in /usr/share/ollama."
                  : "Ollama isn't installed. Turn it on in Services to install it."
        }

        GroupBox95 {
            id: connectBox
            y: 40
            width: parent.width
            height: 86
            title: "Who can use it"
            enabled: !!sheet.service.installed
            RadioButton95 {
                text: "Only programs on <u>t</u>his computer"
                checked: sheet.listen === "local"
                onClicked: { sheet.listen = "local"; sheet.dirty = true; }
            }
            RadioButton95 {
                y: 20
                text: "Other computers on the <u>n</u>etwork too (port 11434)"
                checked: sheet.listen === "all"
                onClicked: { sheet.listen = "all"; sheet.dirty = true; }
            }
            Label {
                y: 40
                width: parent.width
                color: Theme.shadow
                text: "Ollama asks no password: anyone who can reach this computer could use it."
            }
        }

        GroupBox95 {
            id: memoryBox
            y: connectBox.y + connectBox.height + 8
            width: parent.width
            height: 82
            title: "Memory"
            enabled: !!sheet.service.installed
            CheckBox95 {
                text: "<u>K</u>eep two models loaded at once"
                checked: sheet.keepModels
                onToggled: (on) => { sheet.keepModels = on; sheet.dirty = true; }
            }
            Label {
                x: 20
                y: 18
                width: parent.width - 20
                color: Theme.shadow
                text: "Athenaeum's answers come about 2 seconds sooner; it needs more memory."
            }
            Label { y: 42; text: "Unload a model after" }
            SpinBox95 {
                x: 122
                y: 40
                width: 56
                from: 0
                to: 1440
                value: sheet.keepAlive
                onValueModified: (v) => { sheet.keepAlive = v; sheet.dirty = true; }
            }
            Label { x: 184; y: 42; text: "minutes unused" }
        }

        GroupBox95 {
            id: programBox
            y: memoryBox.y + memoryBox.height + 8
            width: parent.width
            height: 112
            title: "Ollama"
            Label {
                width: parent.width
                color: Theme.shadow
                text: "NVIDIA graphics cards are used when NVIDIA's driver is installed; AMD cards need Ollama's ROCm libraries (about 1 GB more)."
            }
            Row {
                y: 50
                spacing: 6
                Button95 {
                    width: 90
                    text: "<u>U</u>pdate"
                    enabled: !!sheet.service.installed && !Services.busy
                    onClicked: {
                        sheet.startedHere = true;
                        Services.adminTask("Updating Ollama", ["ollama-install"], "Downloading Ollama...",
                                           "Ollama is up to date. Your models are kept.");
                    }
                }
                Button95 {
                    width: 120
                    text: "<u>A</u>MD Graphics..."
                    enabled: !!sheet.service.installed && !Services.busy
                    onClicked: sheet.messageBox.show("Ollama",
                        "Add Ollama's libraries for AMD graphics cards (ROCm)? They're downloaded from ollama.com (about 1 GB), "
                        + "and Ollama is downloaded again with them.\n\nOnly worth it with a recent AMD Radeon card.",
                        "question", ["Yes", "No"], function (b) {
                            if (b !== 0)
                                return;
                            sheet.startedHere = true;
                            Services.adminTask("Adding AMD graphics support to Ollama", ["ollama-install", "rocm"], "Downloading Ollama...",
                                               "Ollama can use AMD graphics cards now.");
                        })
                }
                Button95 {
                    width: 116
                    text: "R<u>e</u>move Ollama..."
                    enabled: !!sheet.service.installed && !Services.busy
                    onClicked: sheet.messageBox.show("Ollama",
                        "Remove Ollama from this computer?\n\nKeep your models (in /usr/share/ollama) for when it's installed again, "
                        + "or remove them too?",
                        "warning", ["Keep Models", "Remove Them", "Cancel"], function (b) {
                            if (b === 2)
                                return;
                            sheet.startedHere = true;
                            Services.adminTask("Removing Ollama", b === 1 ? ["ollama-remove", "models"] : ["ollama-remove"], "Removing Ollama...",
                                               b === 1 ? "Ollama and its models have been removed." : "Ollama has been removed. Its models are kept.");
                        })
                }
            }
            Label {
                y: 78
                width: parent.width
                elide: Text.ElideRight
                wrapMode: Text.NoWrap
                visible: Services.busy && sheet.startedHere
                text: Services.status + (Services.percent >= 0 ? " " + Services.percent + "%" : "")
            }
        }
    }
}
