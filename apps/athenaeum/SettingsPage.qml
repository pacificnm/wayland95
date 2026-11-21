import QtQuick
import Win95Ui

// Settings, as a property sheet: the models (Ollama, the answer and
// embedding models), reading (width, citation style), the internet and
// library updates, and where things are kept.
Page95 {
    id: page
    title: "Settings"
    Component.onCompleted: Models.refresh()

    function choices(current) {
        var list = Models.installed.slice();
        if (current !== "" && !list.some(function (m) { return m === current || m === current + ":latest"; }))
            list.unshift(current);
        return list;
    }
    function indexOf(list, current) {
        return Math.max(0, list.findIndex(function (m) { return m === current || m === current + ":latest"; }));
    }

    TabWidget95 {
        anchors.fill: parent
        titles: ["Models", "Reading", "Internet", "Storage"]

        // ---- models
        Item {
            Column {
                width: Math.min(parent.width, 560)
                spacing: 10
                GroupBox95 {
                    width: parent.width
                    height: 108
                    title: "Ollama"
                    Label95 {
                        id: ollamaState
                        x: 10
                        y: 18
                        width: parent.width - 120
                        wrapMode: Text.Wrap
                        color: Models.checked && !Models.reachable ? "#800000" : Theme.text
                        text: Models.checking ? "Checking..."
                            : !Models.checked ? ""
                            : Models.reachable ? "Running at " + Models.url + " · " + Models.installed.length + (Models.installed.length === 1 ? " model installed" : " models installed")
                            : Models.error
                    }
                    Button95 { anchors.right: parent.right; anchors.rightMargin: 10; y: 16; width: 96; text: "Check Again"; enabled: !Models.checking; onClicked: Models.refresh() }
                    Label95 { x: 10; y: 54; height: 22; verticalAlignment: Text.AlignVCenter; text: "Address:" }
                    TextField95 {
                        id: urlField
                        x: 70
                        y: 54
                        width: parent.width - 80
                        height: 22
                        text: Models.url
                        onAccepted: Models.url = text
                        input.onEditingFinished: if (text !== Models.url) Models.url = text
                    }
                }
                GroupBox95 {
                    width: parent.width
                    height: 158
                    title: "Models"
                    Label95 { x: 10; y: 22; width: 120; height: 22; verticalAlignment: Text.AlignVCenter; text: "Answer model:" }
                    ComboBox95 {
                        id: answerBox
                        x: 130
                        y: 22
                        width: parent.width - 140
                        height: 22
                        popupMenu: popup
                        model: page.choices(Models.answerModel)
                        currentIndex: page.indexOf(model, Models.answerModel)
                        onActivated: (i) => Models.answerModel = model[i]
                    }
                    Label95 {
                        x: 130
                        y: 46
                        width: parent.width - 140
                        small: true
                        dim: true
                        wrapMode: Text.Wrap
                        text: Models.answerModel === Models.defaultAnswerModel
                              ? "Writes the answers. " + Models.defaultAnswerModel + " is the measured default for laptops."
                              : "Writes the answers. The measured default is " + Models.defaultAnswerModel + "."
                    }
                    Label95 { x: 10; y: 78; width: 120; height: 22; verticalAlignment: Text.AlignVCenter; text: "Embedding model:" }
                    ComboBox95 {
                        x: 130
                        y: 78
                        width: parent.width - 140
                        height: 22
                        popupMenu: popup
                        model: page.choices(Models.embeddingModel)
                        currentIndex: page.indexOf(model, Models.embeddingModel)
                        onActivated: (i) => Models.embeddingModel = model[i]
                    }
                    Label95 {
                        x: 130
                        y: 102
                        width: parent.width - 140
                        small: true
                        dim: true
                        wrapMode: Text.Wrap
                        text: Models.embeddingModel === Models.defaultEmbeddingModel
                              ? "Ranks passages by meaning and links sentences to sources."
                              : "Ranks passages by meaning and links sentences to sources. The thresholds for flagging sentences were measured with "
                                + Models.defaultEmbeddingModel + "; another model may flag too many or too few."
                    }
                }
                Notice {
                    width: parent.width
                    kind: "warning"
                    text: Models.reachable && (!Models.answerModelInstalled || !Models.embeddingModelInstalled)
                          ? "Not installed in Ollama. Install in a terminal with: <b>"
                            + [Models.answerModelInstalled ? "" : "ollama pull " + Util.plain(Models.answerModel),
                               Models.embeddingModelInstalled ? "" : "ollama pull " + Util.plain(Models.embeddingModel)].filter(function (s) { return s !== ""; }).join("</b> and <b>") + "</b>"
                          : ""
                }
                Notice {
                    width: parent.width
                    kind: "info"
                    text: Models.swapping
                          ? "Ollama keeps only one model loaded, so it unloads the answer model to check citations and loads it again for the next question "
                            + "(about 2 seconds more for every answer). To keep both loaded, run <b>sudo systemctl edit ollama</b>, add<br>"
                            + "<b>[Service]<br>Environment=\"OLLAMA_MAX_LOADED_MODELS=2\"</b><br>then <b>sudo systemctl restart ollama</b>." : ""
                }
                Notice {
                    width: parent.width
                    kind: "info"
                    text: Models.checked && !Models.reachable
                          ? "Install Ollama and its models in Control Panel > Services > Ollama (AI Models): check it, then get <b>"
                            + Util.plain(Models.defaultAnswerModel) + "</b> and <b>" + Util.plain(Models.defaultEmbeddingModel)
                            + "</b> in its Settings. Reading and searching work without it." : ""
                    buttons: [{ text: "Services...", action: function () { controlPanel.openServices(); } }]
                }
            }
        }

        // ---- reading
        Item {
            Column {
                width: Math.min(parent.width, 560)
                spacing: 10
                GroupBox95 {
                    width: parent.width
                    height: 80
                    title: "Articles"
                    CheckBox95 {
                        x: 10
                        y: 20
                        text: "Use the <u>w</u>hole window for articles"
                        checked: LibraryBrowser.fullWidth
                        onToggled: (c) => LibraryBrowser.fullWidth = c
                    }
                    Label95 { x: 30; y: 42; width: parent.width - 40; small: true; dim: true; wrapMode: Text.Wrap
                              text: "Off: articles are set in a column of comfortable reading width, centred in the window." }
                }
                GroupBox95 {
                    width: parent.width
                    height: 86
                    title: "Citation style"
                    ComboBox95 {
                        x: 10
                        y: 22
                        width: parent.width - 20
                        height: 22
                        popupMenu: popup
                        model: LibraryBrowser.citationStyles.map(function (s) { return s.label; })
                        currentIndex: Math.max(0, LibraryBrowser.citationStyles.findIndex(function (s) { return s.style === LibraryBrowser.citationStyle; }))
                        onActivated: (i) => LibraryBrowser.citationStyle = LibraryBrowser.citationStyles[i].style
                    }
                    Label95 { x: 10; y: 48; width: parent.width - 20; small: true; dim: true; wrapMode: Text.Wrap
                              text: "How Cite, a project's Bibliography and exported documents cite articles." }
                }
            }
        }

        // ---- the internet and library updates
        Item {
            Column {
                width: Math.min(parent.width, 560)
                spacing: 10
                GroupBox95 {
                    width: parent.width
                    height: 116
                    title: "Network"
                    CheckBox95 {
                        x: 10
                        y: 20
                        text: "Use the <u>i</u>nternet"
                        checked: Connectivity.allowed
                        onToggled: (c) => Connectivity.allowed = c
                    }
                    Label95 { x: 30; y: 42; width: parent.width - 40; small: true; dim: true; wrapMode: Text.Wrap
                              text: "For the library catalog and downloads, and update checks. Reading, search, Ask, the Classroom, notes and projects never need it." }
                    Label95 {
                        x: 10
                        y: 76
                        text: !Connectivity.allowed ? "Connection: not used, Athenaeum is working offline"
                            : Connectivity.reachable ? "Connection: online" : "Connection: none, Athenaeum is working offline until there is one"
                    }
                }
                GroupBox95 {
                    width: parent.width
                    height: 122
                    title: "Library updates"
                    CheckBox95 {
                        x: 10
                        y: 20
                        text: "Check for newer versions once a <u>w</u>eek"
                        checked: LibraryManager.autoCheckUpdates
                        onToggled: (c) => LibraryManager.autoCheckUpdates = c
                    }
                    Label95 { x: 30; y: 42; width: parent.width - 40; small: true; dim: true; wrapMode: Text.Wrap
                              text: "Kiwix publishes new versions of its libraries every few months. Athenaeum looks in the catalog and tells you; it downloads nothing unless you choose to." }
                    Label95 {
                        x: 10
                        y: 80
                        width: parent.width - 130
                        text: (LibraryManager.updateCount > 0 ? LibraryManager.updateCount + (LibraryManager.updateCount === 1 ? " update available" : " updates available") : "Up to date")
                              + " · " + (LibraryManager.checkingUpdates ? "checking..."
                              : isNaN(LibraryManager.lastUpdateCheck.getTime()) ? "not checked yet"
                              : "last checked " + LibraryManager.lastUpdateCheck.toLocaleString(Qt.locale(), Locale.ShortFormat))
                    }
                    Button95 {
                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        y: 76
                        width: 96
                        text: "Check Now"
                        enabled: !LibraryManager.checkingUpdates && Connectivity.online
                        onClicked: LibraryManager.checkForUpdates(false)
                    }
                }
            }
        }

        // ---- storage
        Item {
            Column {
                width: Math.min(parent.width, 560)
                spacing: 10
                GroupBox95 {
                    width: parent.width
                    height: 86
                    title: "Downloaded libraries"
                    Label95 { x: 10; y: 22; width: parent.width - 120; elide: Text.ElideMiddle; text: LibraryManager.folder }
                    Label95 { x: 10; y: 42; dim: true; text: Util.byteSize(LibraryManager.freeSpace) + " free" }
                    Button95 {
                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        y: 20
                        width: 96
                        text: "Change..."
                        onClicked: window.chooseFile("folder", [{ label: "Folders", patterns: ["*"] }], "", "", function (path) {
                            if (!LibraryManager.setFolder("file://" + path))
                                window.notify("That folder cannot be used for downloads.", true);
                        })
                    }
                }
                GroupBox95 {
                    width: parent.width
                    height: 86
                    title: "Database"
                    Label95 { x: 10; y: 22; width: parent.width - 20; elide: Text.ElideMiddle; text: AppState.databasePath }
                    Label95 { x: 10; y: 42; width: parent.width - 20; dim: true; wrapMode: Text.Wrap
                              text: AppState.databaseError !== "" ? AppState.databaseError : "Chats, projects, notes, the classroom and settings are kept here." }
                }
            }
        }
    }
}
