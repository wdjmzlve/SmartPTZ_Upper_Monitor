#include "RKNNInference.h"

#include <QByteArray>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

float clampFloat(float value, float minimum, float maximum)
{
    return std::max(minimum, std::min(maximum, value));
}

float intersectionOverUnion(const RKNNInference::Detection &left,
                            const RKNNInference::Detection &right)
{
    const float x1 = std::max(left.x, right.x);
    const float y1 = std::max(left.y, right.y);
    const float x2 = std::min(left.x + left.w, right.x + right.w);
    const float y2 = std::min(left.y + left.h, right.y + right.h);

    const float intersectionWidth = std::max(0.0f, x2 - x1);
    const float intersectionHeight = std::max(0.0f, y2 - y1);
    const float intersection = intersectionWidth * intersectionHeight;
    if (intersection <= 0.0f) {
        return 0.0f;
    }

    const float leftArea = left.w * left.h;
    const float rightArea = right.w * right.h;
    const float unionArea = leftArea + rightArea - intersection;
    if (unionArea <= 0.0f) {
        return 0.0f;
    }

    return intersection / unionArea;
}

QVector<int> tensorDims(const rknn_tensor_attr &attr)
{
    QVector<int> dims;
    dims.reserve(static_cast<int>(attr.n_dims));
    for (uint32_t i = 0; i < attr.n_dims; ++i) {
        dims.append(static_cast<int>(attr.dims[i]));
    }
    return dims;
}

bool extractInputShape(const rknn_tensor_attr &attr, int *width, int *height, int *channels)
{
    if (!width || !height || !channels || attr.n_dims < 3) {
        return false;
    }

    if (attr.fmt == RKNN_TENSOR_NHWC) {
        *height = static_cast<int>(attr.dims[attr.n_dims - 3]);
        *width = static_cast<int>(attr.dims[attr.n_dims - 2]);
        *channels = static_cast<int>(attr.dims[attr.n_dims - 1]);
        return true;
    }

    if (attr.fmt == RKNN_TENSOR_NCHW) {
        *channels = static_cast<int>(attr.dims[attr.n_dims - 3]);
        *height = static_cast<int>(attr.dims[attr.n_dims - 2]);
        *width = static_cast<int>(attr.dims[attr.n_dims - 1]);
        return true;
    }

    *height = static_cast<int>(attr.dims[attr.n_dims - 3]);
    *width = static_cast<int>(attr.dims[attr.n_dims - 2]);
    *channels = static_cast<int>(attr.dims[attr.n_dims - 1]);
    return true;
}

bool isLikelyEndToEndShape(const QVector<int> &dims)
{
    return dims.contains(6);
}

bool isLikelyYoloV8Shape(const QVector<int> &dims)
{
    if (dims.size() < 2) {
        return false;
    }

    for (int dim : dims) {
        if (dim > 6 && dim < 512) {
            return true;
        }
    }
    return false;
}

}

RKNNInference::RKNNInference(QObject *parent)
    : QObject(parent)
{
}

RKNNInference::RKNNInference(const QString &modelPath, QObject *parent)
    : QObject(parent)
{
    initialize(modelPath);
}

RKNNInference::~RKNNInference()
{
    QMutexLocker locker(&m_mutex);
    resetLocked();
}

bool RKNNInference::initialize(const QString &modelPath)
{
    QFile modelFile(modelPath);
    if (!modelFile.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open RKNN model" << modelPath << ":" << modelFile.errorString();
        return false;
    }

    const QByteArray modelData = modelFile.readAll();
    if (modelData.isEmpty()) {
        qWarning() << "RKNN model is empty:" << modelPath;
        return false;
    }

    QMutexLocker locker(&m_mutex);
    resetLocked();

    int ret = rknn_init(&m_context,
                        const_cast<char *>(modelData.constData()),
                        static_cast<uint32_t>(modelData.size()),
                        0,
                        nullptr);
    if (ret != RKNN_SUCC) {
        qWarning() << "rknn_init failed for" << modelPath << "error:" << ret;
        resetLocked();
        return false;
    }

    m_modelPath = QFileInfo(modelPath).absoluteFilePath();
    m_initialized = queryModelInfoLocked() && resolveInputShapeLocked();
    if (!m_initialized) {
        qWarning() << "Failed to query RKNN model information for" << modelPath;
        resetLocked();
        return false;
    }

    qInfo() << "RKNN model initialized:"
            << m_modelPath
            << "input" << m_inputWidth << "x" << m_inputHeight
            << "channels" << m_inputChannels
            << "outputs" << m_ioNum.n_output;
    emit initializedChanged();
    return true;
}

bool RKNNInference::isInitialized() const
{
    QMutexLocker locker(&m_mutex);
    return m_initialized;
}

QList<QVariantMap> RKNNInference::detect(const QImage &image)
{
    if (image.isNull()) {
        qWarning() << "detect called with null image.";
        return {};
    }

    QMutexLocker locker(&m_mutex);
    if (!m_initialized) {
        qWarning() << "RKNNInference is not initialized.";
        return {};
    }

    QByteArray inputBuffer = preprocessImage(image);
    if (inputBuffer.isEmpty()) {
        qWarning() << "Failed to preprocess image for RKNN.";
        return {};
    }

    rknn_input input;
    std::memset(&input, 0, sizeof(input));
    input.index = 0;
    input.buf = inputBuffer.data();
    input.size = static_cast<uint32_t>(inputBuffer.size());
    input.pass_through = 0;
    input.type = RKNN_TENSOR_FLOAT32;
    input.fmt = RKNN_TENSOR_NHWC;

    int ret = rknn_inputs_set(m_context, 1, &input);
    if (ret != RKNN_SUCC) {
        qWarning() << "rknn_inputs_set failed:" << ret;
        return {};
    }

    ret = rknn_run(m_context, nullptr);
    if (ret != RKNN_SUCC) {
        qWarning() << "rknn_run failed:" << ret;
        return {};
    }

    QVector<rknn_output> outputs(static_cast<int>(m_ioNum.n_output));
    for (int i = 0; i < outputs.size(); ++i) {
        std::memset(&outputs[i], 0, sizeof(rknn_output));
        outputs[i].index = static_cast<uint32_t>(i);
        outputs[i].want_float = 1;
        outputs[i].is_prealloc = 0;
    }

    ret = rknn_outputs_get(m_context, m_ioNum.n_output, outputs.data(), nullptr);
    if (ret != RKNN_SUCC) {
        qWarning() << "rknn_outputs_get failed:" << ret;
        return {};
    }

    QList<OutputBuffer> outputViews;
    outputViews.reserve(outputs.size());
    for (int i = 0; i < outputs.size(); ++i) {
        OutputBuffer view;
        view.data = static_cast<const float *>(outputs[i].buf);
        view.count = outputs[i].size > 0
                         ? static_cast<int>(outputs[i].size / sizeof(float))
                         : static_cast<int>(m_outputAttrs[i].n_elems);
        view.dims = tensorDims(m_outputAttrs[i]);
        outputViews.append(view);
    }

    const QList<QVariantMap> detections =
        runPostProcess(outputViews, image.width(), image.height());

    const int releaseRet = rknn_outputs_release(m_context, m_ioNum.n_output, outputs.data());
    if (releaseRet != RKNN_SUCC) {
        qWarning() << "rknn_outputs_release failed:" << releaseRet;
    }

    return detections;
}

void RKNNInference::resetLocked()
{
    if (m_context != 0) {
        const int ret = rknn_destroy(m_context);
        if (ret != RKNN_SUCC) {
            qWarning() << "rknn_destroy failed:" << ret;
        }
    }

    const bool wasInitialized = m_initialized;
    m_context = 0;
    m_initialized = false;
    m_modelPath.clear();
    m_ioNum = {};
    m_inputAttr = {};
    m_outputAttrs.clear();
    m_inputWidth = 0;
    m_inputHeight = 0;
    m_inputChannels = 0;

    if (wasInitialized) {
        emit initializedChanged();
    }
}

bool RKNNInference::queryModelInfoLocked()
{
    std::memset(&m_ioNum, 0, sizeof(m_ioNum));
    int ret = rknn_query(m_context, RKNN_QUERY_IN_OUT_NUM, &m_ioNum, sizeof(m_ioNum));
    if (ret != RKNN_SUCC) {
        qWarning() << "RKNN_QUERY_IN_OUT_NUM failed:" << ret;
        return false;
    }

    std::memset(&m_inputAttr, 0, sizeof(m_inputAttr));
    m_inputAttr.index = 0;
    ret = rknn_query(m_context, RKNN_QUERY_INPUT_ATTR, &m_inputAttr, sizeof(m_inputAttr));
    if (ret != RKNN_SUCC) {
        qWarning() << "RKNN_QUERY_INPUT_ATTR failed:" << ret;
        return false;
    }

    m_outputAttrs.resize(static_cast<int>(m_ioNum.n_output));
    for (int i = 0; i < m_outputAttrs.size(); ++i) {
        std::memset(&m_outputAttrs[i], 0, sizeof(rknn_tensor_attr));
        m_outputAttrs[i].index = static_cast<uint32_t>(i);
        ret = rknn_query(m_context,
                         RKNN_QUERY_OUTPUT_ATTR,
                         &m_outputAttrs[i],
                         sizeof(rknn_tensor_attr));
        if (ret != RKNN_SUCC) {
            qWarning() << "RKNN_QUERY_OUTPUT_ATTR failed for output" << i << ":" << ret;
            return false;
        }
    }

    return true;
}

bool RKNNInference::resolveInputShapeLocked()
{
    if (!extractInputShape(m_inputAttr, &m_inputWidth, &m_inputHeight, &m_inputChannels)) {
        qWarning() << "Unsupported RKNN input shape.";
        return false;
    }

    if (m_inputWidth <= 0 || m_inputHeight <= 0 || m_inputChannels <= 0) {
        qWarning() << "Invalid RKNN input dimensions:"
                   << m_inputWidth << m_inputHeight << m_inputChannels;
        return false;
    }

    return true;
}

QByteArray RKNNInference::preprocessImage(const QImage &image) const
{
    if (m_inputWidth <= 0 || m_inputHeight <= 0 || m_inputChannels < 3) {
        return {};
    }

    QImage rgbImage = image.convertToFormat(QImage::Format_RGB888);
    if (rgbImage.isNull()) {
        return {};
    }

    const QImage resized =
        rgbImage.scaled(m_inputWidth, m_inputHeight, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    if (resized.isNull()) {
        return {};
    }

    QByteArray buffer;
    buffer.resize(m_inputWidth * m_inputHeight * 3 * static_cast<int>(sizeof(float)));
    float *dst = reinterpret_cast<float *>(buffer.data());

    for (int y = 0; y < resized.height(); ++y) {
        const uchar *src = resized.constScanLine(y);
        for (int x = 0; x < resized.width(); ++x) {
            const int pixelOffset = x * 3;
            *dst++ = static_cast<float>(src[pixelOffset + 0]) / 255.0f;
            *dst++ = static_cast<float>(src[pixelOffset + 1]) / 255.0f;
            *dst++ = static_cast<float>(src[pixelOffset + 2]) / 255.0f;
        }
    }

    return buffer;
}

QList<QVariantMap> RKNNInference::runPostProcess(const QList<OutputBuffer> &outputs,
                                                 int originalWidth,
                                                 int originalHeight) const
{
    QList<Detection> detections;

    for (const OutputBuffer &output : outputs) {
        if (!output.data || output.count <= 0) {
            continue;
        }

        if (isLikelyEndToEndShape(output.dims)) {
            detections.append(parseEndToEndOutput(output, originalWidth, originalHeight));
            continue;
        }

        if (isLikelyYoloV8Shape(output.dims)) {
            detections.append(parseYoloV8Output(output, originalWidth, originalHeight));
        }
    }

    const QList<Detection> filtered = applyNms(detections);
    QList<QVariantMap> results;
    results.reserve(filtered.size());

    for (const Detection &detection : filtered) {
        QVariantMap item;
        item.insert(QStringLiteral("class"), detection.classId);
        item.insert(QStringLiteral("x"), detection.x);
        item.insert(QStringLiteral("y"), detection.y);
        item.insert(QStringLiteral("w"), detection.w);
        item.insert(QStringLiteral("h"), detection.h);
        item.insert(QStringLiteral("confidence"), detection.confidence);
        results.append(item);
    }

    return results;
}

QList<RKNNInference::Detection> RKNNInference::parseEndToEndOutput(const OutputBuffer &output,
                                                                   int originalWidth,
                                                                   int originalHeight) const
{
    QList<Detection> detections;
    if (!output.data || output.count < 6) {
        return detections;
    }

    int rows = 0;
    int cols = 0;
    bool channelFirst = false;

    if (output.dims.size() >= 2) {
        const int last = output.dims.last();
        const int secondLast = output.dims.at(output.dims.size() - 2);
        if (last == 6) {
            rows = output.count / 6;
            cols = 6;
        } else if (secondLast == 6) {
            rows = output.count / 6;
            cols = 6;
            channelFirst = true;
        }
    }

    if (rows <= 0 || cols != 6) {
        if (output.count % 6 != 0) {
            return detections;
        }
        rows = output.count / 6;
        cols = 6;
    }

    const float xScale = static_cast<float>(originalWidth) / static_cast<float>(m_inputWidth);
    const float yScale = static_cast<float>(originalHeight) / static_cast<float>(m_inputHeight);

    for (int row = 0; row < rows; ++row) {
        auto valueAt = [&](int column) -> float {
            if (channelFirst) {
                return output.data[column * rows + row];
            }
            return output.data[row * cols + column];
        };

        const float x1 = valueAt(0) * xScale;
        const float y1 = valueAt(1) * yScale;
        const float x2 = valueAt(2) * xScale;
        const float y2 = valueAt(3) * yScale;
        const float confidence = valueAt(4);
        const int classId = static_cast<int>(std::round(valueAt(5)));

        if (confidence < m_confidenceThreshold) {
            continue;
        }

        Detection detection;
        detection.classId = classId;
        detection.x = clampFloat(x1, 0.0f, static_cast<float>(originalWidth));
        detection.y = clampFloat(y1, 0.0f, static_cast<float>(originalHeight));
        detection.w = clampFloat(x2 - x1, 0.0f, static_cast<float>(originalWidth));
        detection.h = clampFloat(y2 - y1, 0.0f, static_cast<float>(originalHeight));
        detection.confidence = confidence;
        detections.append(detection);
    }

    return detections;
}

QList<RKNNInference::Detection> RKNNInference::parseYoloV8Output(const OutputBuffer &output,
                                                                 int originalWidth,
                                                                 int originalHeight) const
{
    QList<Detection> detections;
    if (!output.data || output.count <= 0 || output.dims.size() < 2) {
        return detections;
    }

    int candidateCount = 0;
    int featureCount = 0;
    bool channelFirst = false;

    if (output.dims.size() >= 3) {
        const int dimA = output.dims.at(output.dims.size() - 2);
        const int dimB = output.dims.at(output.dims.size() - 1);
        if (dimA > dimB && dimB > 4) {
            candidateCount = dimA;
            featureCount = dimB;
        } else if (dimB > dimA && dimA > 4) {
            candidateCount = dimB;
            featureCount = dimA;
            channelFirst = true;
        } else if (dimB > 4) {
            candidateCount = output.count / dimB;
            featureCount = dimB;
        } else if (dimA > 4) {
            candidateCount = output.count / dimA;
            featureCount = dimA;
            channelFirst = true;
        }
    }

    if (candidateCount <= 0 || featureCount <= 4) {
        return detections;
    }

    const int classCount = featureCount - 4;
    const float xScale = static_cast<float>(originalWidth) / static_cast<float>(m_inputWidth);
    const float yScale = static_cast<float>(originalHeight) / static_cast<float>(m_inputHeight);

    auto valueAt = [&](int candidateIndex, int featureIndex) -> float {
        if (channelFirst) {
            return output.data[featureIndex * candidateCount + candidateIndex];
        }
        return output.data[candidateIndex * featureCount + featureIndex];
    };

    for (int candidate = 0; candidate < candidateCount; ++candidate) {
        float bestScore = 0.0f;
        int bestClass = -1;
        for (int classIndex = 0; classIndex < classCount; ++classIndex) {
            const float score = valueAt(candidate, classIndex + 4);
            if (score > bestScore) {
                bestScore = score;
                bestClass = classIndex;
            }
        }

        if (bestScore < m_confidenceThreshold) {
            continue;
        }

        const float centerX = valueAt(candidate, 0) * xScale;
        const float centerY = valueAt(candidate, 1) * yScale;
        const float width = valueAt(candidate, 2) * xScale;
        const float height = valueAt(candidate, 3) * yScale;
        const float x = centerX - (width * 0.5f);
        const float y = centerY - (height * 0.5f);

        Detection detection;
        detection.classId = bestClass;
        detection.x = clampFloat(x, 0.0f, static_cast<float>(originalWidth));
        detection.y = clampFloat(y, 0.0f, static_cast<float>(originalHeight));
        detection.w = clampFloat(width, 0.0f, static_cast<float>(originalWidth));
        detection.h = clampFloat(height, 0.0f, static_cast<float>(originalHeight));
        detection.confidence = bestScore;
        detections.append(detection);
    }

    return detections;
}

QList<RKNNInference::Detection> RKNNInference::applyNms(const QList<Detection> &detections) const
{
    QList<Detection> sorted = detections;
    std::sort(sorted.begin(),
              sorted.end(),
              [](const Detection &left, const Detection &right) {
                  return left.confidence > right.confidence;
              });

    QList<Detection> kept;
    for (const Detection &candidate : sorted) {
        bool keep = true;
        for (const Detection &existing : kept) {
            if (existing.classId != candidate.classId) {
                continue;
            }

            if (intersectionOverUnion(existing, candidate) > m_nmsThreshold) {
                keep = false;
                break;
            }
        }

        if (keep) {
            kept.append(candidate);
        }
    }

    return kept;
}
