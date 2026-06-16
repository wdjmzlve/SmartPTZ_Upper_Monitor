#ifndef GIMBALCONTROLLER_H
#define GIMBALCONTROLLER_H

#include <QObject>
#include <QImage>
#include <QRectF>
#include <QString>
#include <QVariantMap>

class RKNNInference;
class SerialComm;
class VisualServo;

class GimbalController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(double actualYaw READ actualYaw NOTIFY actualYawChanged)
    Q_PROPERTY(double actualPitch READ actualPitch NOTIFY actualPitchChanged)
    Q_PROPERTY(double targetYaw READ targetYaw NOTIFY targetYawChanged)
    Q_PROPERTY(double pidKp READ pidKp WRITE setPidKp NOTIFY pidKpChanged)
    Q_PROPERTY(double pidKi READ pidKi WRITE setPidKi NOTIFY pidKiChanged)
    Q_PROPERTY(double pidKd READ pidKd WRITE setPidKd NOTIFY pidKdChanged)

public:
    explicit GimbalController(QObject *parent = nullptr);
    GimbalController(const QString &modelPath,
                     const QString &serialPortName,
                     QObject *parent = nullptr);
    ~GimbalController() override;

    QString status() const;
    double actualYaw() const;
    double actualPitch() const;
    double targetYaw() const;
    double pidKp() const;
    double pidKi() const;
    double pidKd() const;

    void setPidKp(double pidKp);
    void setPidKi(double pidKi);
    void setPidKd(double pidKd);

public slots:
    void processFrame(const QImage &frame);
    void setModelPath(const QString &modelPath);
    void setSerialPortName(const QString &serialPortName);

signals:
    void statusChanged();
    void actualYawChanged();
    void actualPitchChanged();
    void targetYawChanged();
    void pidKpChanged();
    void pidKiChanged();
    void pidKdChanged();
    void chartDataUpdated(float target, float actual);

private:
    void initializeIfNeeded();
    void updateStatus(const QString &status);
    void handleTelemetry(double yawDeg, double pitchDeg);
    void updateTargetYaw(double targetYaw);
    void pushPidParameters();

    QString m_status;
    QString m_modelPath;
    QString m_serialPortName;
    double m_actualYaw = 0.0;
    double m_actualPitch = 0.0;
    double m_targetYaw = 0.0;
    bool m_targetYawInitialized = false;
    double m_pidKp = 0.60;
    double m_pidKi = 0.05;
    double m_pidKd = 0.02;
    RKNNInference *m_inference = nullptr;
    SerialComm *m_serial = nullptr;
    VisualServo *m_servo = nullptr;
};

#endif // GIMBALCONTROLLER_H
