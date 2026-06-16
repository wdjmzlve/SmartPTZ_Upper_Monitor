#ifndef RKNNINFERENCE_H
#define RKNNINFERENCE_H

#include <QObject>
#include <QImage>
#include <QList>
#include <QMutex>
#include <QString>
#include <QVariantMap>
#include <QVector>

extern "C" {
#include <rknn_api.h>
}

class RKNNInference : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool initialized READ isInitialized NOTIFY initializedChanged)

public:
    explicit RKNNInference(QObject *parent = nullptr);
    explicit RKNNInference(const QString &modelPath, QObject *parent = nullptr);
    ~RKNNInference() override;

    bool initialize(const QString &modelPath);
    bool isInitialized() const;

    Q_INVOKABLE QList<QVariantMap> detect(const QImage &image);

signals:
    void initializedChanged();

    struct OutputBuffer
    {
        const float *data = nullptr;
        int count = 0;
        QVector<int> dims;
    };

    struct Detection
    {
        int classId = -1;
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
        float confidence = 0.0f;
    };

private:
    void resetLocked();
    bool queryModelInfoLocked();
    bool resolveInputShapeLocked();
    QByteArray preprocessImage(const QImage &image) const;
    QList<QVariantMap> runPostProcess(const QList<OutputBuffer> &outputs,
                                      int originalWidth,
                                      int originalHeight) const;
    QList<Detection> parseEndToEndOutput(const OutputBuffer &output,
                                         int originalWidth,
                                         int originalHeight) const;
    QList<Detection> parseYoloV8Output(const OutputBuffer &output,
                                       int originalWidth,
                                       int originalHeight) const;
    QList<Detection> applyNms(const QList<Detection> &detections) const;

    mutable QMutex m_mutex;
    rknn_context m_context = 0;
    bool m_initialized = false;
    QString m_modelPath;
    rknn_input_output_num m_ioNum {};
    rknn_tensor_attr m_inputAttr {};
    QVector<rknn_tensor_attr> m_outputAttrs;
    int m_inputWidth = 0;
    int m_inputHeight = 0;
    int m_inputChannels = 0;
    float m_confidenceThreshold = 0.25f;
    float m_nmsThreshold = 0.45f;
};

#endif // RKNNINFERENCE_H
