# SmartPTZ Upper Monitor

`SmartPTZ_Upper_Monitor` is a Qt/QML upper-monitor application for an RK3576-based smart PTZ platform. It combines camera capture, RKNN inference, serial communication, and a simple control UI for host-side monitoring and visual-servo experiments.

## Features

- Qt Quick / QML desktop UI
- USB camera frame capture
- RKNN model loading and one-shot detection test mode
- Serial communication with the lower controller
- Basic yaw target vs. actual response chart

## Project Structure

```text
.
|-- qml/                 # QML user interface
|-- src/                 # C++ source code
|-- gimbal_host.pro      # qmake project file
`-- qml.qrc              # Qt resource file
```

## Dependencies

- Qt 5 or Qt 6 with:
  - `QtCore`
  - `QtGui`
  - `QtQuick`
  - `QtQml`
  - `QtQuickControls2`
  - `QtSerialPort`
  - `QtCharts`
- RKNN runtime and headers
- Linux target environment with V4L2 camera support

The `.pro` file currently links RKNN headers from `/usr/include/rknn` and the runtime library from `/usr/lib/aarch64-linux-gnu`.

## Build

```bash
qmake gimbal_host.pro
make
```

## Run

Start the GUI application:

```bash
./gimbal_host --model /path/to/model.rknn --serial /dev/ttyUSB0
```

Run a one-shot detection test:

```bash
./gimbal_host --detect /path/to/model.rknn /path/to/image.jpg
```

## Notes

- The actual hardware device names may differ from `/dev/video0` and `/dev/ttyUSB0`.
- RKNN model files are not tracked by default and should be managed separately.
