import QtQuick
Image {
    property string name: "home"
    property bool darkMode: false
    property bool accent: false
    source: "icons/" + name + (accent ? "-accent" : darkMode ? "-dark" : "-light") + ".svg"
    sourceSize.width: width * Screen.devicePixelRatio
    sourceSize.height: height * Screen.devicePixelRatio
    fillMode: Image.PreserveAspectFit
    smooth: true
}
