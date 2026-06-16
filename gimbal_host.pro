QT += core gui quick qml serialport charts
QT += quickcontrols2

CONFIG += c++14
CONFIG += qmltypes

TARGET = gimbal_host
TEMPLATE = app

greaterThan(QT_MAJOR_VERSION, 5) {
    DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060500
}

SOURCES += \
    src/CameraCapture.cpp \
    src/SerialComm.cpp \
    src/RKNNInference.cpp \
    src/VisualServo.cpp \
    src/main.cpp \
    src/gimbalcontroller.cpp

HEADERS += \
    src/CameraCapture.h \
    src/SerialComm.h \
    src/RKNNInference.h \
    src/VisualServo.h \
    src/gimbalcontroller.h

RESOURCES += \
    qml.qrc

unix:!macx {
    INCLUDEPATH += /usr/include/rknn
    LIBS += -L/usr/lib/aarch64-linux-gnu -lrknn_api
}
