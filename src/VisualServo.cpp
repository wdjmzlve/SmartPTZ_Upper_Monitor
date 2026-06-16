#include "VisualServo.h"

#include <QtGlobal>

VisualServo::VisualServo() = default;

void VisualServo::setGains(double yawKp, double pitchKp)
{
    m_yawKp = yawKp;
    m_pitchKp = pitchKp;
}

void VisualServo::setMaxSpeed(double yawMaxSpeed, double pitchMaxSpeed)
{
    m_maxYawSpeed = yawMaxSpeed;
    m_maxPitchSpeed = pitchMaxSpeed;
}

double VisualServo::yawKp() const
{
    return m_yawKp;
}

double VisualServo::pitchKp() const
{
    return m_pitchKp;
}

VisualServo::Command VisualServo::compute(const QRectF &targetBox, const QSize &frameSize) const
{
    Command command;
    if (!targetBox.isValid() || frameSize.isEmpty()) {
        return command;
    }

    command.valid = true;
    command.frameCenter = QPointF(frameSize.width() * 0.5, frameSize.height() * 0.5);
    command.targetCenter = targetBox.center();
    command.dx = command.targetCenter.x() - command.frameCenter.x();
    command.dy = command.targetCenter.y() - command.frameCenter.y();
    command.yawSpeed = qBound(-m_maxYawSpeed, m_yawKp * command.dx, m_maxYawSpeed);
    command.pitchSpeed = qBound(-m_maxPitchSpeed, -m_pitchKp * command.dy, m_maxPitchSpeed);
    return command;
}
