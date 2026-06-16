#include "gimbalcontroller.h"

#include "RKNNInference.h"
#include "SerialComm.h"
#include "VisualServo.h"

#include <QDebug>
#include <QRectF>
#include <QSize>

namespace {

constexpr double kTargetPredictionHorizonSec = 0.15;

QString defaultStatusText(bool inferenceReady, bool serialReady)
{
    if (inferenceReady && serialReady) {
        return QStringLiteral("Active");
    }
    if (inferenceReady) {
        return QStringLiteral("Vision Ready");
    }
    if (serialReady) {
        return QStringLiteral("Serial Ready");
    }
    return QStringLiteral("Idle");
}

}

GimbalController::GimbalController(QObject *parent)
    : QObject(parent)
    , m_status(QStringLiteral("Idle"))
    , m_inference(new RKNNInference(this))
    , m_serial(new SerialComm(this))
    , m_servo(new VisualServo())
{
    connect(m_serial, &SerialComm::anglesReceived, this, &GimbalController::handleTelemetry);
    connect(m_serial, &SerialComm::errorOccurred, this, [this](const QString &message) {
        updateStatus(message);
    });
    connect(m_serial, &SerialComm::connectedChanged, this, [this]() {
        updateStatus(defaultStatusText(m_inference->isInitialized(), m_serial->isConnected()));
    });
    updateStatus(QStringLiteral("Idle"));
}

QString GimbalController::status() const
{
    return m_status;
}

GimbalController::GimbalController(const QString &modelPath,
                                   const QString &serialPortName,
                                   QObject *parent)
    : GimbalController(parent)
{
    m_modelPath = modelPath;
    m_serialPortName = serialPortName;
    initializeIfNeeded();
}

GimbalController::~GimbalController()
{
    delete m_servo;
}

double GimbalController::actualYaw() const
{
    return m_actualYaw;
}

double GimbalController::actualPitch() const
{
    return m_actualPitch;
}

double GimbalController::targetYaw() const
{
    return m_targetYaw;
}

double GimbalController::pidKp() const
{
    return m_pidKp;
}

double GimbalController::pidKi() const
{
    return m_pidKi;
}

double GimbalController::pidKd() const
{
    return m_pidKd;
}

void GimbalController::setPidKp(double pidKp)
{
    if (qFuzzyCompare(m_pidKp + 1.0, pidKp + 1.0)) {
        return;
    }

    m_pidKp = pidKp;
    emit pidKpChanged();
    pushPidParameters();
}

void GimbalController::setPidKi(double pidKi)
{
    if (qFuzzyCompare(m_pidKi + 1.0, pidKi + 1.0)) {
        return;
    }

    m_pidKi = pidKi;
    emit pidKiChanged();
    pushPidParameters();
}

void GimbalController::setPidKd(double pidKd)
{
    if (qFuzzyCompare(m_pidKd + 1.0, pidKd + 1.0)) {
        return;
    }

    m_pidKd = pidKd;
    emit pidKdChanged();
    pushPidParameters();
}

void GimbalController::processFrame(const QImage &frame)
{
    if (frame.isNull()) {
        return;
    }

    initializeIfNeeded();
    if (!m_inference || !m_inference->isInitialized() || !m_serial || !m_serial->isConnected()) {
        return;
    }

    const QList<QVariantMap> detections = m_inference->detect(frame);
    if (detections.isEmpty()) {
        updateTargetYaw(m_actualYaw);
        updateStatus(QStringLiteral("No target"));
        return;
    }

    QVariantMap bestDetection = detections.first();
    for (const QVariantMap &detection : detections) {
        if (detection.value(QStringLiteral("confidence")).toFloat()
            > bestDetection.value(QStringLiteral("confidence")).toFloat()) {
            bestDetection = detection;
        }
    }

    const QRectF targetBox(bestDetection.value(QStringLiteral("x")).toFloat(),
                           bestDetection.value(QStringLiteral("y")).toFloat(),
                           bestDetection.value(QStringLiteral("w")).toFloat(),
                           bestDetection.value(QStringLiteral("h")).toFloat());
    const VisualServo::Command command = m_servo->compute(targetBox, frame.size());
    if (!command.valid) {
        updateTargetYaw(m_actualYaw);
        updateStatus(QStringLiteral("Invalid target"));
        return;
    }

    updateTargetYaw(m_actualYaw + (command.yawSpeed * kTargetPredictionHorizonSec));

    qDebug().noquote()
        << QStringLiteral("IBVS dx=%1 dy=%2 yaw=%3 pitch=%4")
               .arg(command.dx, 0, 'f', 2)
               .arg(command.dy, 0, 'f', 2)
               .arg(command.yawSpeed, 0, 'f', 2)
               .arg(command.pitchSpeed, 0, 'f', 2);

    m_serial->sendAngularVelocity(static_cast<float>(command.yawSpeed),
                                  static_cast<float>(command.pitchSpeed));
    updateStatus(QStringLiteral("Tracking"));
}

void GimbalController::setModelPath(const QString &modelPath)
{
    if (m_modelPath == modelPath) {
        return;
    }

    m_modelPath = modelPath;
    initializeIfNeeded();
}

void GimbalController::setSerialPortName(const QString &serialPortName)
{
    if (m_serialPortName == serialPortName) {
        return;
    }

    m_serialPortName = serialPortName;
    if (m_serial) {
        m_serial->close();
    }
    initializeIfNeeded();
}

void GimbalController::initializeIfNeeded()
{
    if (m_inference && !m_modelPath.isEmpty() && !m_inference->isInitialized()) {
        if (m_inference->initialize(m_modelPath)) {
            qInfo() << "RKNN inference initialized with model:" << m_modelPath;
        }
    }

    if (m_serial && !m_serialPortName.isEmpty() && !m_serial->isConnected()) {
        if (!m_serial->open(m_serialPortName)) {
            qWarning() << "Serial initialization failed for" << m_serialPortName;
        }
    }

    updateStatus(defaultStatusText(m_inference && m_inference->isInitialized(),
                                   m_serial && m_serial->isConnected()));
}

void GimbalController::updateStatus(const QString &status)
{
    if (m_status == status) {
        return;
    }

    m_status = status;
    emit statusChanged();
}

void GimbalController::handleTelemetry(double yawDeg, double pitchDeg)
{
    if (!m_targetYawInitialized) {
        updateTargetYaw(yawDeg);
    }

    if (qFuzzyCompare(m_actualYaw + 1.0, yawDeg + 1.0) == false) {
        m_actualYaw = yawDeg;
        emit actualYawChanged();
    }

    if (qFuzzyCompare(m_actualPitch + 1.0, pitchDeg + 1.0) == false) {
        m_actualPitch = pitchDeg;
        emit actualPitchChanged();
    }

    qDebug().noquote()
        << QStringLiteral("MCU angles yaw=%1 pitch=%2")
               .arg(m_actualYaw, 0, 'f', 2)
               .arg(m_actualPitch, 0, 'f', 2);

    emit chartDataUpdated(static_cast<float>(m_targetYaw), static_cast<float>(m_actualYaw));
}

void GimbalController::updateTargetYaw(double targetYaw)
{
    if (m_targetYawInitialized && qFuzzyCompare(m_targetYaw + 1.0, targetYaw + 1.0)) {
        return;
    }

    m_targetYawInitialized = true;
    m_targetYaw = targetYaw;
    emit targetYawChanged();
}

void GimbalController::pushPidParameters()
{
    initializeIfNeeded();
    if (!m_serial || !m_serial->isConnected()) {
        qWarning() << "Serial not connected, unable to send PID gains.";
        return;
    }

    m_serial->sendPidParameters(static_cast<float>(m_pidKp),
                                static_cast<float>(m_pidKi),
                                static_cast<float>(m_pidKd));
}
