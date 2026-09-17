import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import FioMark

ApplicationWindow {
    id: win
    width: 760
    height: 560
    minimumWidth: 640
    minimumHeight: 480
    visible: true
    title: "fiomark"

    readonly property string mono: "IBM Plex Mono"
    readonly property real best: {
        let m = 1
        for (const r of runner.results) m = Math.max(m, r.readMB, r.writeMB)
        return m
    }

    FioRunner { id: runner }
    SmartInfo { id: smart; targetDir: runner.targetDir; Component.onCompleted: refresh() }

    // fiomark [--dir PATH] [--size GiB] [--runtime S] [--start]
    Component.onCompleted: {
        const a = Qt.application.arguments
        const opt = name => { const i = a.indexOf(name); return i >= 0 && i + 1 < a.length ? a[i + 1] : null }
        if (opt("--dir")) runner.targetDir = opt("--dir")
        if (opt("--size")) { runner.sizeGiB = parseInt(opt("--size")); sizeBox.currentIndex = sizeBox.indexOfValue(runner.sizeGiB) }
        if (opt("--runtime")) runner.runtimeSec = parseInt(opt("--runtime"))
        if (a.indexOf("--start") >= 0) runner.start()
        if (a.indexOf("--health") >= 0) tabs.currentIndex = 1
    }

    FolderDialog {
        id: folderDialog
        currentFolder: "file://" + runner.targetDir
        onAccepted: runner.targetDir = selectedFolder
    }

    function fmtMB(v)  { return v < 0 ? "—" : (v >= 100 ? v.toFixed(0) : v.toFixed(1)) }
    function fmtSub(iops, latUs) {
        if (iops < 0) return " "
        const k = iops >= 1000 ? (iops / 1000).toFixed(1) + "k" : iops.toFixed(0)
        const l = latUs >= 1000 ? (latUs / 1000).toFixed(2) + " ms" : latUs.toFixed(0) + " µs"
        return k + " IOPS · " + l
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        Label { text: runner.deviceInfo; font.family: win.mono; elide: Text.ElideRight; Layout.fillWidth: true }

        TabBar {
            id: tabs
            Layout.fillWidth: true
            TabButton { text: "Benchmark" }
            TabButton { text: "Health" + (smart.health === "Bad" ? "  ⚠" : "") }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            // ---------------- Benchmark ----------------
            ColumnLayout {
                spacing: 12
        Label {
            visible: runner.warning.length > 0
            text: "⚠ " + runner.warning
            color: palette.highlight
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            TextField {
                Layout.fillWidth: true
                text: runner.targetDir
                enabled: !runner.running
                font.family: win.mono
                onEditingFinished: runner.targetDir = text
            }
            Button { text: "Browse…"; enabled: !runner.running; onClicked: folderDialog.open() }
            ComboBox {
                id: sizeBox
                enabled: !runner.running
                model: [1, 2, 4, 8, 16, 32]
                currentIndex: 2
                displayText: currentValue + " GiB"
                delegate: ItemDelegate { text: modelData + " GiB"; width: sizeBox.width }
                onActivated: runner.sizeGiB = currentValue
                implicitWidth: 100
            }
            SpinBox {
                enabled: !runner.running
                from: 1; to: 120; stepSize: 5
                value: runner.runtimeSec
                textFromValue: v => v + " s"
                valueFromText: t => parseInt(t)
                onValueModified: runner.runtimeSec = value
                implicitWidth: 110
            }
            Button {
                text: runner.running ? "Stop" : "Start"
                highlighted: !runner.running
                implicitWidth: 90
                onClicked: runner.running ? runner.stop() : runner.start()
            }
        }

        GridLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            columns: 3
            rowSpacing: 8
            columnSpacing: 8

            Item { Layout.preferredWidth: 120 }
            Label { text: "Read  MB/s";  font.bold: true; horizontalAlignment: Text.AlignHCenter; Layout.fillWidth: true }
            Label { text: "Write  MB/s"; font.bold: true; horizontalAlignment: Text.AlignHCenter; Layout.fillWidth: true }

            Repeater {
                model: runner.results.length * 3
                delegate: Loader {
                    required property int index
                    readonly property var row: runner.results[Math.floor(index / 3)]
                    readonly property int col: index % 3
                    Layout.fillWidth: col > 0
                    Layout.fillHeight: true
                    Layout.preferredWidth: col === 0 ? 120 : -1
                    sourceComponent: col === 0 ? nameCell : valueCell
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            ProgressBar { Layout.fillWidth: true; value: runner.progress }
            Label {
                text: runner.error.length > 0 ? runner.error : runner.currentTest
                color: runner.error.length > 0 ? palette.highlight : palette.windowText
                font.family: win.mono
                Layout.preferredWidth: 240
                elide: Text.ElideRight
            }
        }
            } // Benchmark

            // ---------------- Health (SMART via udisks2) ----------------
            ColumnLayout {
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    ColumnLayout {
                        spacing: 2
                        Layout.fillWidth: true
                        Label {
                            text: smart.error.length > 0 ? "SMART unavailable" : (smart.drive.model || "")
                            font.bold: true; font.pixelSize: 18; elide: Text.ElideRight; Layout.fillWidth: true
                        }
                        Label {
                            text: smart.error.length > 0 ? smart.error
                                : [smart.drive.device, smart.drive.kind, smart.drive.size, "fw " + smart.drive.firmware, "S/N " + smart.drive.serial].join("  ·  ")
                            font.family: win.mono; opacity: 0.8; elide: Text.ElideRight; Layout.fillWidth: true
                        }
                    }
                    Button { text: smart.busy ? "Reading…" : "Refresh"; enabled: !smart.busy; onClicked: smart.refresh() }
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: healthRow.implicitHeight + 20
                    radius: 6
                    color: palette.base
                    border.color: smart.health === "Bad" || smart.health === "Caution" ? palette.highlight : palette.mid
                    border.width: smart.health === "Good" ? 1 : 2
                    RowLayout {
                        id: healthRow
                        anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 12 }
                        spacing: 12
                        Label {
                            text: smart.health
                            font.family: win.mono; font.pixelSize: 26; font.bold: true
                            color: smart.health === "Good" ? palette.text : palette.highlight
                        }
                        Label { text: smart.summary; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        Label { text: smart.updated.length ? "as of " + smart.updated : ""; font.family: win.mono; opacity: 0.7 }
                    }
                }

                ListView {
                    id: attrList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: smart.attributes
                    spacing: 2
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        width: attrList.width
                        height: 30
                        radius: 4
                        color: index % 2 ? palette.base : "transparent"
                        RowLayout {
                            anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
                            spacing: 10
                            Label {
                                text: modelData.level === "crit" ? "●" : modelData.level === "warn" ? "●" : modelData.level === "ok" ? "●" : "○"
                                color: modelData.level === "crit" || modelData.level === "warn" ? palette.highlight : palette.text
                                opacity: modelData.level === "info" ? 0.4 : 1
                                Layout.preferredWidth: 14
                            }
                            Label { text: modelData.name; Layout.preferredWidth: 300; elide: Text.ElideRight }
                            Label { text: modelData.value; font.family: win.mono; Layout.fillWidth: true; elide: Text.ElideRight }
                        }
                    }
                }
            } // Health
        }
    }

    Component {
        id: nameCell
        ColumnLayout {
            spacing: 0
            Item { Layout.fillHeight: true }
            Label { text: parent.parent.row.name; font.bold: true; font.pixelSize: 18 }
            Label { text: parent.parent.row.detail; opacity: 0.7; font.family: win.mono }
            Item { Layout.fillHeight: true }
        }
    }

    Component {
        id: valueCell
        Rectangle {
            readonly property var row: parent.row
            readonly property bool isWrite: parent.col === 2
            readonly property real mb: isWrite ? row.writeMB : row.readMB
            color: palette.base
            border.color: palette.mid
            radius: 6
            clip: true
            Rectangle {   // bar, scaled against the fastest result
                anchors { left: parent.left; top: parent.top; bottom: parent.bottom; margins: 1 }
                width: parent.mb > 0 ? Math.max(4, (parent.width - 2) * Math.sqrt(parent.mb / win.best)) : 0
                radius: 5
                color: palette.highlight
                opacity: 0.28
                Behavior on width { NumberAnimation { duration: 400; easing.type: Easing.OutCubic } }
            }
            ColumnLayout {
                anchors.centerIn: parent
                spacing: 2
                Label {
                    text: win.fmtMB(parent.parent.mb)
                    font.family: win.mono; font.pixelSize: 34; font.bold: true
                    color: palette.text
                    Layout.alignment: Qt.AlignHCenter
                }
                Label {
                    text: parent.parent.isWrite ? win.fmtSub(parent.parent.row.writeIops, parent.parent.row.writeLatUs)
                                                : win.fmtSub(parent.parent.row.readIops, parent.parent.row.readLatUs)
                    font.family: win.mono; opacity: 0.75
                    color: palette.text
                    Layout.alignment: Qt.AlignHCenter
                }
            }
        }
    }
}
