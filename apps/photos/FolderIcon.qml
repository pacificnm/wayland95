import QtQuick

// A folder, drawn as the file manager draws its own (apps/files/FileIcon.qml):
// on a 32x32 grid, scaled to `size`.
Item {
    id: icon

    property int size: 32
    width: size
    height: size

    Item {
        width: 32
        height: 32
        scale: icon.size / 32
        transformOrigin: Item.TopLeft

        Rectangle { x: 2; y: 6; width: 12; height: 5; color: "#e8c048"; border.color: "#806000" }
        Rectangle { x: 2; y: 9; width: 28; height: 19; color: "#f8d878"; border.color: "#806000" }
        Rectangle { x: 3; y: 10; width: 26; height: 1; color: "#fff0b0" }
    }
}
