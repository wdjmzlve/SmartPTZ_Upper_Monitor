import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material
import QtCharts 2.4

ApplicationWindow {
    id: root

    property int maxChartPoints: 100
    property int chartSampleIndex: 0

    function appendChartPoint(targetAngle, actualAngle) {
        const x = chartSampleIndex
        chartSampleIndex += 1

        targetSeries.append(x, targetAngle)
        actualSeries.append(x, actualAngle)

        while (targetSeries.count > maxChartPoints)
            targetSeries.remove(0)

        while (actualSeries.count > maxChartPoints)
            actualSeries.remove(0)

        axisX.min = Math.max(0, chartSampleIndex - maxChartPoints)
        axisX.max = Math.max(maxChartPoints - 1, chartSampleIndex - 1)
    }

    width: 1280
    height: 720
    visible: true
    title: "gimbal_host"
    color: "#16181d"

    Material.theme: Material.Dark
    Material.accent: Material.Teal
    Material.primary: Material.BlueGrey
    Material.background: "#16181d"
    Material.foreground: "#f1f3f5"

    Connections {
        target: controller

        function onChartDataUpdated(target, actual) {
            root.appendChartPoint(target, actual)
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "#16181d"
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 20

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 7
            radius: 12
            color: "#23272f"
            border.width: 1
            border.color: "#2f3642"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 20

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredHeight: 5
                    radius: 11
                    color: "#1b1f27"
                    border.width: 1
                    border.color: "#2a303a"

                    Image {
                        anchors.fill: parent
                        anchors.margins: 24
                        fillMode: Image.PreserveAspectFit
                        source: ""
                        visible: source !== ""
                    }

                    Column {
                        anchors.centerIn: parent
                        spacing: 10

                        Label {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "Video Preview"
                            color: "#f1f3f5"
                            font.pixelSize: 28
                        }

                        Label {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "Waiting for camera stream"
                            color: "#8c96a5"
                            font.pixelSize: 16
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredHeight: 3
                    radius: 11
                    color: "#1b1f27"
                    border.width: 1
                    border.color: "#2a303a"

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 12

                        RowLayout {
                            Layout.fillWidth: true

                            Label {
                                text: "Yaw Response"
                                color: "#f1f3f5"
                                font.pixelSize: 22
                                font.bold: true
                            }

                            Item {
                                Layout.fillWidth: true
                            }

                            Label {
                                text: "Last " + root.maxChartPoints + " samples"
                                color: "#8c96a5"
                                font.pixelSize: 13
                            }
                        }

                        ChartView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            antialiasing: false
                            backgroundColor: "#171b23"
                            plotAreaColor: "#171b23"
                            animationOptions: ChartView.NoAnimation
                            legend.visible: true
                            legend.alignment: Qt.AlignBottom
                            theme: ChartView.ChartThemeDark

                            ValueAxis {
                                id: axisX
                                min: 0
                                max: root.maxChartPoints - 1
                                tickCount: 6
                                labelFormat: "%d"
                                titleText: "Samples"
                            }

                            ValueAxis {
                                id: axisY
                                min: -180
                                max: 180
                                tickCount: 7
                                labelFormat: "%.0f"
                                titleText: "Yaw (deg)"
                            }

                            LineSeries {
                                id: targetSeries
                                name: "Target Yaw"
                                axisX: axisX
                                axisY: axisY
                                color: "#4dd0e1"
                                width: 2.5
                                useOpenGL: true
                            }

                            LineSeries {
                                id: actualSeries
                                name: "Actual Yaw"
                                axisX: axisX
                                axisY: axisY
                                color: "#ffb74d"
                                width: 2.5
                                useOpenGL: true
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 3
            radius: 12
            color: "#20242c"
            border.width: 1
            border.color: "#2f3642"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 14

                Label {
                    text: "Control Panel"
                    color: "#f1f3f5"
                    font.pixelSize: 26
                    font.bold: true
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 78
                    radius: 10
                    color: "#2a303a"

                    Column {
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 6

                        Label {
                            text: "Controller Status"
                            color: "#8c96a5"
                            font.pixelSize: 14
                        }

                        Label {
                            text: controller.status
                            color: "#f1f3f5"
                            font.pixelSize: 22
                            font.bold: true
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 78
                    radius: 10
                    color: "#2a303a"

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 18

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            Label {
                                text: "Target Yaw"
                                color: "#8c96a5"
                                font.pixelSize: 14
                            }

                            Label {
                                text: controller.targetYaw.toFixed(2) + " deg"
                                color: "#4dd0e1"
                                font.pixelSize: 20
                                font.bold: true
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            Label {
                                text: "Actual Yaw"
                                color: "#8c96a5"
                                font.pixelSize: 14
                            }

                            Label {
                                text: controller.actualYaw.toFixed(2) + " deg"
                                color: "#ffb74d"
                                font.pixelSize: 20
                                font.bold: true
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 78
                    radius: 10
                    color: "#2a303a"

                    Column {
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 6

                        Label {
                            text: "Actual Pitch"
                            color: "#8c96a5"
                            font.pixelSize: 14
                        }

                        Label {
                            text: controller.actualPitch.toFixed(2) + " deg"
                            color: "#f1f3f5"
                            font.pixelSize: 22
                            font.bold: true
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 10
                    color: "#2a303a"

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 12

                        Label {
                            text: "PID Tuning"
                            color: "#f1f3f5"
                            font.pixelSize: 20
                            font.bold: true
                        }

                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: "Drag each slider to immediately push updated gains to the MCU over serial."
                            color: "#8c96a5"
                            font.pixelSize: 13
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            radius: 9
                            color: "#232933"
                            implicitHeight: 90

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 6

                                RowLayout {
                                    Layout.fillWidth: true

                                    Label {
                                        text: "Kp"
                                        color: "#f1f3f5"
                                        font.pixelSize: 18
                                        font.bold: true
                                    }

                                    Item {
                                        Layout.fillWidth: true
                                    }

                                    Label {
                                        text: controller.pidKp.toFixed(2)
                                        color: "#4dd0e1"
                                        font.pixelSize: 16
                                        font.bold: true
                                    }
                                }

                                Slider {
                                    Layout.fillWidth: true
                                    from: 0.0
                                    to: 5.0
                                    stepSize: 0.01
                                    value: controller.pidKp

                                    onValueChanged: {
                                        if (Math.abs(controller.pidKp - value) > 0.0001)
                                            controller.pidKp = value
                                    }
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            radius: 9
                            color: "#232933"
                            implicitHeight: 90

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 6

                                RowLayout {
                                    Layout.fillWidth: true

                                    Label {
                                        text: "Ki"
                                        color: "#f1f3f5"
                                        font.pixelSize: 18
                                        font.bold: true
                                    }

                                    Item {
                                        Layout.fillWidth: true
                                    }

                                    Label {
                                        text: controller.pidKi.toFixed(3)
                                        color: "#81c784"
                                        font.pixelSize: 16
                                        font.bold: true
                                    }
                                }

                                Slider {
                                    Layout.fillWidth: true
                                    from: 0.0
                                    to: 1.0
                                    stepSize: 0.001
                                    value: controller.pidKi

                                    onValueChanged: {
                                        if (Math.abs(controller.pidKi - value) > 0.0001)
                                            controller.pidKi = value
                                    }
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            radius: 9
                            color: "#232933"
                            implicitHeight: 90

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 6

                                RowLayout {
                                    Layout.fillWidth: true

                                    Label {
                                        text: "Kd"
                                        color: "#f1f3f5"
                                        font.pixelSize: 18
                                        font.bold: true
                                    }

                                    Item {
                                        Layout.fillWidth: true
                                    }

                                    Label {
                                        text: controller.pidKd.toFixed(3)
                                        color: "#ffb74d"
                                        font.pixelSize: 16
                                        font.bold: true
                                    }
                                }

                                Slider {
                                    Layout.fillWidth: true
                                    from: 0.0
                                    to: 1.0
                                    stepSize: 0.001
                                    value: controller.pidKd

                                    onValueChanged: {
                                        if (Math.abs(controller.pidKd - value) > 0.0001)
                                            controller.pidKd = value
                                    }
                                }
                            }
                        }

                        Item {
                            Layout.fillHeight: true
                        }
                    }
                }
            }
        }
    }
}
