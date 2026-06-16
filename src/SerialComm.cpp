#include "SerialComm.h"

#include <QDebug>
#include <QtEndian>

#include <cmath>
#include <cstring>
#include <algorithm>

namespace {

constexpr quint8 kFrameHeader0 = 0xAA;
constexpr quint8 kFrameHeader1 = 0x55;
constexpr int kHeaderSize = 2;
constexpr int kLengthSize = 1;
constexpr int kCommandSize = 1;
constexpr int kCrcSize = 2;
constexpr int kMinFrameSize = kHeaderSize + kLengthSize + kCommandSize + kCrcSize;
constexpr int kMaxPayloadSize = 250;

bool isFiniteAngle(float value)
{
    return std::isfinite(value) && std::fabs(value) <= 3600.0f;
}

void appendLittleEndianFloat(QByteArray *payload, float value)
{
    if (!payload) {
        return;
    }

    quint32 bits = 0;
    std::memcpy(&bits, &value, sizeof(quint32));
    bits = qToLittleEndian(bits);
    payload->append(reinterpret_cast<const char *>(&bits), static_cast<int>(sizeof(quint32)));
}

}

SerialComm::SerialComm(QObject *parent)
    : QObject(parent)
{
    connect(&m_serialPort, &QSerialPort::readyRead, this, &SerialComm::onReadyRead);
    connect(&m_serialPort,
            &QSerialPort::errorOccurred,
            this,
            &SerialComm::onErrorOccurred);
}

bool SerialComm::open(const QString &portName,
                      qint32 baudRate,
                      QSerialPort::DataBits dataBits,
                      QSerialPort::Parity parity,
                      QSerialPort::StopBits stopBits)
{
    if (portName.isEmpty()) {
        qWarning() << "Serial port name is empty.";
        emit errorOccurred(QStringLiteral("Serial port name is empty"));
        return false;
    }

    if (m_serialPort.isOpen()) {
        if (m_serialPort.portName() == portName && m_serialPort.baudRate() == baudRate) {
            return true;
        }
        close();
    }

    m_serialPort.setPortName(portName);
    m_serialPort.setBaudRate(baudRate);
    m_serialPort.setDataBits(dataBits);
    m_serialPort.setParity(parity);
    m_serialPort.setStopBits(stopBits);
    m_serialPort.setFlowControl(QSerialPort::NoFlowControl);

    if (!m_serialPort.open(QIODevice::ReadWrite)) {
        const QString message =
            QStringLiteral("Failed to open serial port %1: %2")
                .arg(portName, m_serialPort.errorString());
        qWarning().noquote() << message;
        emit errorOccurred(message);
        return false;
    }

    m_rxBuffer.clear();
    qInfo() << "Serial port opened:" << portName << "baud" << baudRate;
    emit connectedChanged();
    return true;
}

void SerialComm::close()
{
    if (!m_serialPort.isOpen()) {
        return;
    }

    const QString closedPortName = m_serialPort.portName();
    m_serialPort.close();
    m_rxBuffer.clear();
    qInfo() << "Serial port closed:" << closedPortName;
    emit connectedChanged();
}

bool SerialComm::isConnected() const
{
    return m_serialPort.isOpen();
}

QString SerialComm::portName() const
{
    return m_serialPort.portName();
}

void SerialComm::setLengthMode(LengthMode mode)
{
    m_lengthMode = mode;
}

SerialComm::LengthMode SerialComm::lengthMode() const
{
    return m_lengthMode;
}

bool SerialComm::writeFrame(quint8 command, const QByteArray &payload)
{
    if (!m_serialPort.isOpen()) {
        const QString message = QStringLiteral("Serial port is not open, unable to send frame");
        qWarning().noquote() << message;
        emit errorOccurred(message);
        return false;
    }

    const QByteArray frame = buildFrame(command, payload);
    if (frame.isEmpty()) {
        return false;
    }

    const qint64 written = m_serialPort.write(frame);
    if (written != frame.size()) {
        const QString message =
            QStringLiteral("Serial write failed on %1: wrote %2 of %3 bytes")
                .arg(m_serialPort.portName())
                .arg(written)
                .arg(frame.size());
        qWarning().noquote() << message;
        emit errorOccurred(message);
        return false;
    }

    qDebug().noquote()
        << QStringLiteral("TX CMD=0x%1 LEN=%2 PAYLOAD=%3")
               .arg(command, 2, 16, QLatin1Char('0'))
               .arg(payload.size())
               .arg(QString::fromLatin1(payload.toHex(' ')));
    return true;
}

bool SerialComm::sendAngularVelocity(float yawSpeedDegPerSec, float pitchSpeedDegPerSec)
{
    QByteArray payload;
    payload.resize(static_cast<int>(sizeof(float) * 2));

    quint32 yawBits = 0;
    quint32 pitchBits = 0;
    std::memcpy(&yawBits, &yawSpeedDegPerSec, sizeof(quint32));
    std::memcpy(&pitchBits, &pitchSpeedDegPerSec, sizeof(quint32));
    yawBits = qToLittleEndian(yawBits);
    pitchBits = qToLittleEndian(pitchBits);

    std::memcpy(payload.data(), &yawBits, sizeof(quint32));
    std::memcpy(payload.data() + sizeof(quint32), &pitchBits, sizeof(quint32));

    qDebug() << "Command angular velocity yaw/pitch(deg/s):"
             << yawSpeedDegPerSec
             << pitchSpeedDegPerSec;
    return writeFrame(CommandSetAngularVelocity, payload);
}

bool SerialComm::sendPidParameters(float kp, float ki, float kd)
{
    QByteArray payload;
    payload.reserve(static_cast<int>(sizeof(float) * 3));
    appendLittleEndianFloat(&payload, kp);
    appendLittleEndianFloat(&payload, ki);
    appendLittleEndianFloat(&payload, kd);

    qDebug() << "Command PID gains kp/ki/kd:" << kp << ki << kd;
    return writeFrame(CommandSetPid, payload);
}

void SerialComm::onReadyRead()
{
    m_rxBuffer.append(m_serialPort.readAll());
    parseIncomingBuffer();
}

void SerialComm::onErrorOccurred(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError) {
        return;
    }

    const QString message =
        QStringLiteral("Serial error on %1: %2")
            .arg(m_serialPort.portName(), m_serialPort.errorString());
    qWarning().noquote() << message;
    emit errorOccurred(message);

    if (error == QSerialPort::ResourceError) {
        close();
    }
}

void SerialComm::parseIncomingBuffer()
{
    while (m_rxBuffer.size() >= kMinFrameSize) {
        const int headerIndex =
            m_rxBuffer.indexOf(QByteArray::fromRawData("\xAA\x55", kHeaderSize));
        if (headerIndex < 0) {
            m_rxBuffer.clear();
            return;
        }

        if (headerIndex > 0) {
            m_rxBuffer.remove(0, headerIndex);
        }

        ParsedFrame frame;
        if (!tryParseFrameAt(0, &frame)) {
            if (m_rxBuffer.size() >= 2
                && static_cast<quint8>(m_rxBuffer.at(0)) == kFrameHeader0
                && static_cast<quint8>(m_rxBuffer.at(1)) == kFrameHeader1
                && m_rxBuffer.size() >= kMinFrameSize) {
                const quint8 lengthField = static_cast<quint8>(m_rxBuffer.at(kHeaderSize));
                if (lengthField == 0 || lengthField > (kMaxPayloadSize + kCommandSize)) {
                    qWarning() << "Discarding malformed frame length:" << lengthField;
                    m_rxBuffer.remove(0, 2);
                    continue;
                }
            }
            return;
        }

        const QByteArray frameBytes = m_rxBuffer.left(frame.totalSize);
        qDebug().noquote()
            << QStringLiteral("RX CMD=0x%1 LEN=%2 PAYLOAD=%3")
                   .arg(frame.command, 2, 16, QLatin1Char('0'))
                   .arg(frame.payload.size())
                   .arg(QString::fromLatin1(frame.payload.toHex(' ')));
        Q_UNUSED(frameBytes);

        m_rxBuffer.remove(0, frame.totalSize);
        handleFrame(frame.command, frame.payload);
    }
}

bool SerialComm::tryParseFrameAt(int offset, ParsedFrame *frame)
{
    if (!frame) {
        return false;
    }

    if (offset < 0 || (m_rxBuffer.size() - offset) < kMinFrameSize) {
        return false;
    }

    if (static_cast<quint8>(m_rxBuffer.at(offset)) != kFrameHeader0
        || static_cast<quint8>(m_rxBuffer.at(offset + 1)) != kFrameHeader1) {
        return false;
    }

    const int lengthIndex = offset + kHeaderSize;
    const int lengthValue = static_cast<quint8>(m_rxBuffer.at(lengthIndex));

    const int candidateSizes[] = {
        kHeaderSize + kLengthSize + lengthValue + kCrcSize,
        kHeaderSize + kLengthSize + kCommandSize + lengthValue + kCrcSize
    };

    const bool preferIncludesCommand = (m_lengthMode == LengthIncludesCommand);
    const int firstCandidate = preferIncludesCommand ? 0 : 1;
    const int secondCandidate = preferIncludesCommand ? 1 : 0;
    const int candidateOrder[] = { firstCandidate, secondCandidate };

    for (int candidateIndex : candidateOrder) {
        const int totalSize = candidateSizes[candidateIndex];
        if (totalSize < kMinFrameSize || (m_rxBuffer.size() - offset) < totalSize) {
            continue;
        }

        const QByteArray frameBytes = m_rxBuffer.mid(offset, totalSize);
        const int lengthFieldBytes = totalSize - kHeaderSize - kLengthSize - kCrcSize;
        if (!validateFrame(frameBytes, lengthFieldBytes)) {
            continue;
        }

        const int payloadLength = lengthFieldBytes - kCommandSize;
        if (payloadLength < 0 || payloadLength > kMaxPayloadSize) {
            continue;
        }

        frame->totalSize = totalSize;
        frame->command = static_cast<quint8>(frameBytes.at(kHeaderSize + kLengthSize));
        frame->payload =
            frameBytes.mid(kHeaderSize + kLengthSize + kCommandSize, payloadLength);
        return true;
    }

    if ((m_rxBuffer.size() - offset) >= kMinFrameSize) {
        const int minCandidate = std::min(candidateSizes[0], candidateSizes[1]);
        if (minCandidate <= (m_rxBuffer.size() - offset)) {
            qWarning() << "CRC16 verification failed, discarding frame header.";
            m_rxBuffer.remove(0, 2);
        }
    }
    return false;
}

bool SerialComm::validateFrame(const QByteArray &frameBytes, int lengthFieldBytes) const
{
    if (frameBytes.size() != (kHeaderSize + kLengthSize + lengthFieldBytes + kCrcSize)) {
        return false;
    }

    const quint16 expectedCrc = crc16(frameBytes.constData() + kHeaderSize,
                                      kLengthSize + lengthFieldBytes);
    const uchar crcLo = static_cast<uchar>(frameBytes.at(frameBytes.size() - 2));
    const uchar crcHi = static_cast<uchar>(frameBytes.at(frameBytes.size() - 1));
    const quint16 receivedLittleEndian = static_cast<quint16>(crcLo)
                                       | (static_cast<quint16>(crcHi) << 8);
    const quint16 receivedBigEndian = static_cast<quint16>(crcHi)
                                    | (static_cast<quint16>(crcLo) << 8);

    return expectedCrc == receivedLittleEndian || expectedCrc == receivedBigEndian;
}

void SerialComm::handleFrame(quint8 command, const QByteArray &payload)
{
    emit frameReceived(command, payload);

    if (command != CommandReportAngles) {
        return;
    }

    double yawDeg = 0.0;
    double pitchDeg = 0.0;
    if (!decodeAngles(payload, &yawDeg, &pitchDeg)) {
        qWarning() << "Unsupported angle payload size:" << payload.size();
        return;
    }

    emit anglesReceived(yawDeg, pitchDeg);
}

bool SerialComm::decodeAngles(const QByteArray &payload, double *yawDeg, double *pitchDeg) const
{
    if (!yawDeg || !pitchDeg) {
        return false;
    }

    if (payload.size() >= static_cast<int>(sizeof(float) * 2)) {
        float yawValue = 0.0f;
        float pitchValue = 0.0f;
        quint32 yawBits = 0;
        quint32 pitchBits = 0;
        std::memcpy(&yawBits, payload.constData(), sizeof(quint32));
        std::memcpy(&pitchBits, payload.constData() + sizeof(quint32), sizeof(quint32));
        yawBits = qFromLittleEndian(yawBits);
        pitchBits = qFromLittleEndian(pitchBits);
        std::memcpy(&yawValue, &yawBits, sizeof(float));
        std::memcpy(&pitchValue, &pitchBits, sizeof(float));
        if (isFiniteAngle(yawValue) && isFiniteAngle(pitchValue)) {
            *yawDeg = yawValue;
            *pitchDeg = pitchValue;
            return true;
        }

        const qint32 yawMilliDeg =
            qFromLittleEndian<qint32>(reinterpret_cast<const uchar *>(payload.constData()));
        const qint32 pitchMilliDeg = qFromLittleEndian<qint32>(
            reinterpret_cast<const uchar *>(payload.constData() + sizeof(qint32)));
        *yawDeg = static_cast<double>(yawMilliDeg) / 1000.0;
        *pitchDeg = static_cast<double>(pitchMilliDeg) / 1000.0;
        return true;
    }

    if (payload.size() >= static_cast<int>(sizeof(qint16) * 2)) {
        const qint16 yawCentiDeg =
            qFromLittleEndian<qint16>(reinterpret_cast<const uchar *>(payload.constData()));
        const qint16 pitchCentiDeg = qFromLittleEndian<qint16>(
            reinterpret_cast<const uchar *>(payload.constData() + sizeof(qint16)));
        *yawDeg = static_cast<double>(yawCentiDeg) / 100.0;
        *pitchDeg = static_cast<double>(pitchCentiDeg) / 100.0;
        return true;
    }

    return false;
}

QByteArray SerialComm::buildFrame(quint8 command, const QByteArray &payload) const
{
    const int lengthFieldValue =
        (m_lengthMode == LengthIncludesCommand) ? (payload.size() + kCommandSize)
                                                : payload.size();
    if (payload.size() > kMaxPayloadSize || lengthFieldValue > 255) {
        qWarning() << "Payload too large for serial frame:" << payload.size();
        return {};
    }

    QByteArray frame;
    frame.reserve(kHeaderSize + kLengthSize + kCommandSize + payload.size() + kCrcSize);
    frame.append(static_cast<char>(kFrameHeader0));
    frame.append(static_cast<char>(kFrameHeader1));
    frame.append(static_cast<char>(lengthFieldValue));
    frame.append(static_cast<char>(command));
    frame.append(payload);

    const quint16 crc = crc16(frame.constData() + kHeaderSize,
                              frame.size() - kHeaderSize);
    frame.append(static_cast<char>(crc & 0xFF));
    frame.append(static_cast<char>((crc >> 8) & 0xFF));
    return frame;
}

quint16 SerialComm::crc16(const char *data, int length)
{
    quint16 crc = 0xFFFF;
    for (int i = 0; i < length; ++i) {
        crc ^= static_cast<quint8>(data[i]);
        for (int bit = 0; bit < 8; ++bit) {
            if ((crc & 0x0001U) != 0U) {
                crc = static_cast<quint16>((crc >> 1) ^ 0xA001U);
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}
