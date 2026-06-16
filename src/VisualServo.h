#ifndef VISUALSERVO_H
#define VISUALSERVO_H

#include <QPointF>
#include <QRectF>
#include <QSize>

class VisualServo
{
public:
    struct Command
    {
        bool valid = false;
        QPointF frameCenter;
        QPointF targetCenter;
        double dx = 0.0;
        double dy = 0.0;
        double yawSpeed = 0.0;
        double pitchSpeed = 0.0;
    };

    VisualServo();

    void setGains(double yawKp, double pitchKp);
    void setMaxSpeed(double yawMaxSpeed, double pitchMaxSpeed);

    double yawKp() const;
    double pitchKp() const;

    Command compute(const QRectF &targetBox, const QSize &frameSize) const;

private:
    double m_yawKp = 0.02;
    double m_pitchKp = 0.02;
    double m_maxYawSpeed = 120.0;
    double m_maxPitchSpeed = 120.0;
};

#endif // VISUALSERVO_H
