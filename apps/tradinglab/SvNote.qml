import QtQuick
import Win95Ui

// A grey explanatory note under a section (upstream "muted note"): wraps to the width it is given.
Label95 {
    width: parent ? parent.width : 200
    wrapMode: Text.Wrap
    dim: true
}
