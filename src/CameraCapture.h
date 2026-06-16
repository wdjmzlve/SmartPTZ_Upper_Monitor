#ifndef CAMERACAPTURE_H
#define CAMERACAPTURE_H

#include <QImage>
#include <QMutex>
#include <QThread>
#include <QString>
#include <QVector>

#include <cstddef>

class CameraCapture : public QThread
{
    Q_OBJECT

public:
    explicit CameraCapture(const QString &devicePath = QStringLiteral("/dev/video0"),
                           QObject *parent = nullptr);
    ~CameraCapture() override;

    void stop();

signals:
    void newFrameReady(const QImage &frame);

protected:
    void run() override;

private:
    struct Buffer
    {
        void *start = nullptr;
        size_t length = 0;
    };

    bool openDevice();
    bool configureDevice();
    bool initMmap();
    bool startStreaming();
    void stopStreaming();
    void uninitMmap();
    void closeDevice();
    bool queueAllBuffers();
    bool captureFrame();
    bool xioctl(unsigned long request, void *arg) const;

    QImage decodeFrame(const void *data, size_t bytesUsed) const;
    QImage decodeMjpg(const void *data, size_t bytesUsed) const;
    QImage decodeYuyv(const void *data, int width, int height) const;

    QString m_devicePath;
    mutable QMutex m_stateMutex;
    bool m_running = false;
    int m_fd = -1;
    QVector<Buffer> m_buffers;
    quint32 m_pixelFormat = 0;
    int m_width = 640;
    int m_height = 480;
    int m_fps = 30;
};

#endif // CAMERACAPTURE_H
