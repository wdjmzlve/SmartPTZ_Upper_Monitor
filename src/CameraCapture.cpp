#include "CameraCapture.h"

#include <QByteArray>
#include <QDebug>
#include <QMutexLocker>
#include <QSize>

#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace {

constexpr int kBufferCount = 4;

int clampToByte(int value)
{
    if (value < 0) {
        return 0;
    }
    if (value > 255) {
        return 255;
    }
    return value;
}

}

CameraCapture::CameraCapture(const QString &devicePath, QObject *parent)
    : QThread(parent)
    , m_devicePath(devicePath)
{
}

CameraCapture::~CameraCapture()
{
    stop();
    wait();
}

void CameraCapture::stop()
{
    QMutexLocker locker(&m_stateMutex);
    m_running = false;
}

void CameraCapture::run()
{
    {
        QMutexLocker locker(&m_stateMutex);
        m_running = true;
    }

    if (!openDevice()) {
        return;
    }

    if (!configureDevice() || !initMmap() || !queueAllBuffers() || !startStreaming()) {
        stopStreaming();
        uninitMmap();
        closeDevice();
        return;
    }

    while (true) {
        {
            QMutexLocker locker(&m_stateMutex);
            if (!m_running) {
                break;
            }
        }

        if (!captureFrame()) {
            msleep(5);
        }
    }

    stopStreaming();
    uninitMmap();
    closeDevice();
}

bool CameraCapture::openDevice()
{
    const QByteArray deviceBytes = m_devicePath.toLocal8Bit();
    m_fd = ::open(deviceBytes.constData(), O_RDWR | O_NONBLOCK, 0);
    if (m_fd < 0) {
        qWarning() << "Failed to open camera device" << m_devicePath << ":" << strerror(errno);
        return false;
    }

    v4l2_capability capability;
    std::memset(&capability, 0, sizeof(capability));
    if (!xioctl(VIDIOC_QUERYCAP, &capability)) {
        qWarning() << "VIDIOC_QUERYCAP failed for" << m_devicePath << ":" << strerror(errno);
        closeDevice();
        return false;
    }

    const bool supportsCapture = (capability.capabilities & V4L2_CAP_VIDEO_CAPTURE) != 0;
    const bool supportsStreaming = (capability.capabilities & V4L2_CAP_STREAMING) != 0;
    if (!supportsCapture || !supportsStreaming) {
        qWarning() << "Device does not support capture/streaming:" << m_devicePath;
        closeDevice();
        return false;
    }

    return true;
}

bool CameraCapture::configureDevice()
{
    const quint32 preferredFormats[] = { V4L2_PIX_FMT_MJPEG, V4L2_PIX_FMT_YUYV };

    for (quint32 format : preferredFormats) {
        v4l2_format fmt;
        std::memset(&fmt, 0, sizeof(fmt));
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.width = static_cast<quint32>(m_width);
        fmt.fmt.pix.height = static_cast<quint32>(m_height);
        fmt.fmt.pix.pixelformat = format;
        fmt.fmt.pix.field = V4L2_FIELD_ANY;

        if (!xioctl(VIDIOC_S_FMT, &fmt)) {
            continue;
        }

        if (fmt.fmt.pix.pixelformat == format) {
            m_width = static_cast<int>(fmt.fmt.pix.width);
            m_height = static_cast<int>(fmt.fmt.pix.height);
            m_pixelFormat = format;
            break;
        }
    }

    if (m_pixelFormat == 0) {
        qWarning() << "Unable to configure camera to MJPG or YUYV.";
        return false;
    }

    v4l2_streamparm streamParm;
    std::memset(&streamParm, 0, sizeof(streamParm));
    streamParm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    streamParm.parm.capture.timeperframe.numerator = 1;
    streamParm.parm.capture.timeperframe.denominator = static_cast<quint32>(m_fps);
    if (!xioctl(VIDIOC_S_PARM, &streamParm)) {
        qWarning() << "VIDIOC_S_PARM failed, continuing with driver default fps:" << strerror(errno);
    }

    qInfo() << "Camera configured:"
            << "device" << m_devicePath
            << "format" << QString::fromLatin1(reinterpret_cast<const char *>(&m_pixelFormat), 4)
            << "size" << QSize(m_width, m_height)
            << "fps" << m_fps;
    return true;
}

bool CameraCapture::initMmap()
{
    v4l2_requestbuffers request;
    std::memset(&request, 0, sizeof(request));
    request.count = kBufferCount;
    request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    request.memory = V4L2_MEMORY_MMAP;

    if (!xioctl(VIDIOC_REQBUFS, &request)) {
        qWarning() << "VIDIOC_REQBUFS failed:" << strerror(errno);
        return false;
    }

    if (request.count < 2) {
        qWarning() << "Insufficient V4L2 mmap buffers:" << request.count;
        return false;
    }

    m_buffers.resize(static_cast<int>(request.count));
    for (quint32 i = 0; i < request.count; ++i) {
        v4l2_buffer buffer;
        std::memset(&buffer, 0, sizeof(buffer));
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = i;

        if (!xioctl(VIDIOC_QUERYBUF, &buffer)) {
            qWarning() << "VIDIOC_QUERYBUF failed for index" << i << ":" << strerror(errno);
            return false;
        }

        void *start = ::mmap(nullptr,
                             buffer.length,
                             PROT_READ | PROT_WRITE,
                             MAP_SHARED,
                             m_fd,
                             static_cast<off_t>(buffer.m.offset));
        if (start == MAP_FAILED) {
            qWarning() << "mmap failed for index" << i << ":" << strerror(errno);
            return false;
        }

        m_buffers[static_cast<int>(i)] = { start, buffer.length };
    }

    return true;
}

bool CameraCapture::queueAllBuffers()
{
    for (int i = 0; i < m_buffers.size(); ++i) {
        v4l2_buffer buffer;
        std::memset(&buffer, 0, sizeof(buffer));
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = static_cast<quint32>(i);

        if (!xioctl(VIDIOC_QBUF, &buffer)) {
            qWarning() << "VIDIOC_QBUF failed for index" << i << ":" << strerror(errno);
            return false;
        }
    }

    return true;
}

bool CameraCapture::startStreaming()
{
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (!xioctl(VIDIOC_STREAMON, &type)) {
        qWarning() << "VIDIOC_STREAMON failed:" << strerror(errno);
        return false;
    }
    return true;
}

void CameraCapture::stopStreaming()
{
    if (m_fd < 0) {
        return;
    }

    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (!xioctl(VIDIOC_STREAMOFF, &type)) {
        if (errno != EINVAL) {
            qWarning() << "VIDIOC_STREAMOFF failed:" << strerror(errno);
        }
    }
}

void CameraCapture::uninitMmap()
{
    for (Buffer &buffer : m_buffers) {
        if (buffer.start && buffer.start != MAP_FAILED) {
            ::munmap(buffer.start, buffer.length);
        }
        buffer.start = nullptr;
        buffer.length = 0;
    }
    m_buffers.clear();
}

void CameraCapture::closeDevice()
{
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
}

bool CameraCapture::captureFrame()
{
    pollfd pfd;
    std::memset(&pfd, 0, sizeof(pfd));
    pfd.fd = m_fd;
    pfd.events = POLLIN;

    const int pollResult = ::poll(&pfd, 1, 1000);
    if (pollResult < 0) {
        if (errno != EINTR) {
            qWarning() << "poll failed:" << strerror(errno);
        }
        return false;
    }
    if (pollResult == 0) {
        qWarning() << "Camera capture timeout.";
        return false;
    }

    v4l2_buffer buffer;
    std::memset(&buffer, 0, sizeof(buffer));
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer.memory = V4L2_MEMORY_MMAP;

    if (!xioctl(VIDIOC_DQBUF, &buffer)) {
        if (errno != EAGAIN) {
            qWarning() << "VIDIOC_DQBUF failed:" << strerror(errno);
        }
        return false;
    }

    if (buffer.index >= static_cast<quint32>(m_buffers.size())) {
        qWarning() << "Received invalid buffer index:" << buffer.index;
        return false;
    }

    const Buffer &mappedBuffer = m_buffers.at(static_cast<int>(buffer.index));
    QImage frame = decodeFrame(mappedBuffer.start, buffer.bytesused);
    if (!frame.isNull()) {
        emit newFrameReady(frame);
    }

    if (!xioctl(VIDIOC_QBUF, &buffer)) {
        qWarning() << "VIDIOC_QBUF requeue failed:" << strerror(errno);
        return false;
    }

    return true;
}

bool CameraCapture::xioctl(unsigned long request, void *arg) const
{
    int result = 0;
    do {
        result = ::ioctl(m_fd, static_cast<unsigned long>(request), arg);
    } while (result == -1 && errno == EINTR);

    return result != -1;
}

QImage CameraCapture::decodeFrame(const void *data, size_t bytesUsed) const
{
    if (m_pixelFormat == V4L2_PIX_FMT_MJPEG) {
        return decodeMjpg(data, bytesUsed);
    }

    if (m_pixelFormat == V4L2_PIX_FMT_YUYV) {
        return decodeYuyv(data, m_width, m_height);
    }

    return QImage();
}

QImage CameraCapture::decodeMjpg(const void *data, size_t bytesUsed) const
{
    const QByteArray jpegBytes(static_cast<const char *>(data), static_cast<int>(bytesUsed));
    QImage frame = QImage::fromData(jpegBytes, "JPEG");
    if (frame.isNull()) {
        qWarning() << "Failed to decode MJPG frame.";
        return QImage();
    }

    if (frame.format() != QImage::Format_RGB888) {
        frame = frame.convertToFormat(QImage::Format_RGB888);
    }
    return frame;
}

QImage CameraCapture::decodeYuyv(const void *data, int width, int height) const
{
    const unsigned char *src = static_cast<const unsigned char *>(data);
    QImage image(width, height, QImage::Format_RGB888);
    if (image.isNull()) {
        return QImage();
    }

    for (int y = 0; y < height; ++y) {
        uchar *dst = image.scanLine(y);
        for (int x = 0; x < width; x += 2) {
            const int offset = (y * width + x) * 2;
            const int y0 = src[offset + 0];
            const int u = src[offset + 1] - 128;
            const int y1 = src[offset + 2];
            const int v = src[offset + 3] - 128;

            const int c0 = y0 - 16;
            const int c1 = y1 - 16;

            const int r0 = clampToByte((298 * c0 + 409 * v + 128) >> 8);
            const int g0 = clampToByte((298 * c0 - 100 * u - 208 * v + 128) >> 8);
            const int b0 = clampToByte((298 * c0 + 516 * u + 128) >> 8);

            const int r1 = clampToByte((298 * c1 + 409 * v + 128) >> 8);
            const int g1 = clampToByte((298 * c1 - 100 * u - 208 * v + 128) >> 8);
            const int b1 = clampToByte((298 * c1 + 516 * u + 128) >> 8);

            dst[x * 3 + 0] = static_cast<uchar>(r0);
            dst[x * 3 + 1] = static_cast<uchar>(g0);
            dst[x * 3 + 2] = static_cast<uchar>(b0);
            dst[(x + 1) * 3 + 0] = static_cast<uchar>(r1);
            dst[(x + 1) * 3 + 1] = static_cast<uchar>(g1);
            dst[(x + 1) * 3 + 2] = static_cast<uchar>(b1);
        }
    }

    return image;
}
