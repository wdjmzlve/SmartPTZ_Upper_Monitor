#include <QCoreApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QDebug>
#include <QGuiApplication>
#include <QImage>
#include <QQuickStyle>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStringList>

#include "CameraCapture.h"
#include "RKNNInference.h"
#include "gimbalcontroller.h"

int main(int argc, char *argv[])
{
    bool detectMode = false;
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QStringLiteral("--detect")) {
            detectMode = true;
            break;
        }
    }

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif

    if (detectMode) {
        QCoreApplication app(argc, argv);
        app.setApplicationName(QStringLiteral("gimbal_host"));
        app.setOrganizationName(QStringLiteral("rk3576"));

        QCommandLineParser parser;
        parser.setApplicationDescription(QStringLiteral("RK3576 gimbal host"));
        parser.addHelpOption();
        parser.addPositionalArgument(QStringLiteral("model"),
                                     QStringLiteral("RKNN model path when using --detect."));
        parser.addPositionalArgument(QStringLiteral("image"),
                                     QStringLiteral("Test image path when using --detect."));
        QCommandLineOption detectOption(QStringLiteral("detect"),
                                        QStringLiteral("Run a one-shot RKNN detection test."));
        parser.addOption(detectOption);
        parser.process(app);

        const QStringList positionalArguments = parser.positionalArguments();
        if (positionalArguments.size() < 2) {
            qCritical() << "Usage:" << app.applicationName() << "--detect <model.rknn> <image>";
            return -1;
        }

        const QString modelPath = positionalArguments.at(0);
        const QString imagePath = positionalArguments.at(1);
        QImage image(imagePath);
        if (image.isNull()) {
            qCritical() << "Failed to load test image:" << imagePath;
            return -1;
        }

        RKNNInference inference;
        if (!inference.initialize(modelPath)) {
            qCritical() << "Failed to initialize RKNN model:" << modelPath;
            return -1;
        }

        const QList<QVariantMap> detections = inference.detect(image);
        qInfo() << "Detection count:" << detections.size();
        for (const QVariantMap &detection : detections) {
            qInfo().noquote()
                << QStringLiteral("class=%1 x=%2 y=%3 w=%4 h=%5 confidence=%6")
                       .arg(detection.value(QStringLiteral("class")).toInt())
                       .arg(detection.value(QStringLiteral("x")).toFloat(), 0, 'f', 2)
                       .arg(detection.value(QStringLiteral("y")).toFloat(), 0, 'f', 2)
                       .arg(detection.value(QStringLiteral("w")).toFloat(), 0, 'f', 2)
                       .arg(detection.value(QStringLiteral("h")).toFloat(), 0, 'f', 2)
                       .arg(detection.value(QStringLiteral("confidence")).toFloat(), 0, 'f', 4);
        }
        return 0;
    }

    QQuickStyle::setStyle(QStringLiteral("Material"));

    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("gimbal_host"));
    app.setOrganizationName(QStringLiteral("rk3576"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("RK3576 gimbal host"));
    parser.addHelpOption();
    QCommandLineOption modelOption(QStringLiteral("model"),
                                   QStringLiteral("RKNN model path."),
                                   QStringLiteral("path"));
    QCommandLineOption serialOption(QStringLiteral("serial"),
                                    QStringLiteral("Serial port name."),
                                    QStringLiteral("port"));
    parser.addOption(modelOption);
    parser.addOption(serialOption);
    parser.process(app);

    QQmlApplicationEngine engine;
    GimbalController gimbalController(parser.value(modelOption), parser.value(serialOption));
    CameraCapture cameraCapture(QStringLiteral("/dev/video0"));
    engine.rootContext()->setContextProperty(QStringLiteral("controller"), &gimbalController);

    QObject::connect(
        &cameraCapture,
        &CameraCapture::newFrameReady,
        &gimbalController,
        &GimbalController::processFrame,
        Qt::QueuedConnection);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &cameraCapture, &CameraCapture::stop);

    const QUrl mainQmlUrl(QStringLiteral("qrc:/qml/main.qml"));
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreated,
        &app,
        [mainQmlUrl](QObject *object, const QUrl &objectUrl) {
            if (!object && objectUrl == mainQmlUrl) {
                QCoreApplication::exit(-1);
            }
        },
        Qt::QueuedConnection);

    engine.load(mainQmlUrl);
    if (engine.rootObjects().isEmpty()) {
        return -1;
    }

    cameraCapture.start();
    return app.exec();
}
