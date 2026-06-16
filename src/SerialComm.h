#ifndef SERIALCOMM_H
#define SERIALCOMM_H

#include <QObject>
#include <QByteArray>
#include <QSerialPort>

class SerialComm : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool connected READ isConnected NOTIFY connectedChanged)

public:
    static constexpr quint8 CommandSetAngularVelocity = 0x01;
    static constexpr quint8 CommandSetPid = 0x02;
    static constexpr quint8 CommandReportAngles = 0x81;

    enum LengthMode
    {
        LengthIncludesCommand,
        LengthIsPayloadOnly
    };
    Q_ENUM(LengthMode)

    explicit SerialComm(QObject *parent = nullptr);

    bool open(const QString &portName,
              qint32 baudRate = QSerialPort::Baud115200,
              QSerialPort::DataBits dataBits = QSerialPort::Data8,
              QSerialPort::Parity parity = QSerialPort::NoParity,
              QSerialPort::StopBits stopBits = QSerialPort::OneStop);
    void close();

    bool isConnected() const;
    QString portName() const;

    void setLengthMode(LengthMode mode);
    LengthMode lengthMode() const;

    bool writeFrame(quint8 command, const QByteArray &payload = QByteArray());
    bool sendAngularVelocity(float yawSpeedDegPerSec, float pitchSpeedDegPerSec);
    bool sendPidParameters(float kp, float ki, float kd);

signals:
    void connectedChanged();
    void frameReceived(quint8 command, const QByteArray &payload);
    void anglesReceived(double yawDeg, double pitchDeg);
    void errorOccurred(const QString &message);

private slots:
    void onReadyRead();
    void onErrorOccurred(QSerialPort::SerialPortError error);

private:
    struct ParsedFrame
    {
        int totalSize = 0;
        quint8 command = 0;
        QByteArray payload;
    };

    void parseIncomingBuffer();
    bool tryParseFrameAt(int offset, ParsedFrame *frame);
    bool validateFrame(const QByteArray &frameBytes, int lengthFieldBytes) const;
    void handleFrame(quint8 command, const QByteArray &payload);
    bool decodeAngles(const QByteArray &payload, double *yawDeg, double *pitchDeg) const;
    QByteArray buildFrame(quint8 command, const QByteArray &payload) const;
    static quint16 crc16(const char *data, int length);

    QSerialPort m_serialPort;
    QByteArray m_rxBuffer;
    LengthMode m_lengthMode = LengthIncludesCommand;
};

#endif // SERIALCOMM_H
