#include "controller.hpp"
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QSGRendererInterface>
#include <gst/gst.h>
#include <csignal>
#include <QTimer>
#include <QDebug>
namespace { volatile std::sig_atomic_t quitRequested=0; }

int main(int argc,char** argv){
    std::signal(SIGPIPE,SIG_IGN);
    std::signal(SIGTERM,[](int){quitRequested=1;});
    std::signal(SIGINT,[](int){quitRequested=1;});
    QCoreApplication::setAttribute(Qt::AA_SynthesizeMouseForUnhandledTouchEvents,false);
    QCoreApplication::setAttribute(Qt::AA_SynthesizeMouseForUnhandledTabletEvents,false);
    // A consistent Controls style honors our palette on KDE light and dark desktops.
    QQuickStyle::setStyle("Material");
    QGuiApplication app(argc,argv);
    QTimer shutdownTimer;QObject::connect(&shutdownTimer,&QTimer::timeout,&app,[]{if(quitRequested)QCoreApplication::quit();});shutdownTimer.start(100);
    QCoreApplication::setOrganizationName("ExtraPane");QCoreApplication::setApplicationName("ExtraPane");
    QGuiApplication::setDesktopFileName("io.github.EEkebin.ExtraPane");
    QGuiApplication::setWindowIcon(QIcon(":/extrapane.png"));
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QString unavailable;GError* error=nullptr;
    if(!gst_init_check(nullptr,nullptr,&error)){
        unavailable=error?QString::fromUtf8(error->message):"GStreamer initialization failed";
        if(error)g_error_free(error);
    }else{
        // Loading this plugin registers the QML video type before the engine imports it.
        auto* sink=gst_element_factory_make("qml6glsink",nullptr);
        if(!sink)unavailable="Missing GStreamer qml6glsink (Qt 6 OpenGL video integration).";
        else gst_object_unref(sink);
        for(const char* name:{"appsrc","h264parse","decodebin","capsfilter","identity","glupload","glcolorconvert"}){
            auto* factory=gst_element_factory_find(name);
            if(!factory){if(!unavailable.isEmpty())unavailable+='\n';unavailable+="Missing GStreamer element: "+QString::fromUtf8(name);}
            else gst_object_unref(factory);
        }
    }
    const auto platform=QGuiApplication::platformName();
    if(platform!="xcb" && !platform.startsWith("wayland"))unavailable="This client requires Qt's Wayland or X11 platform backend.";
    qmlRegisterType<dm::linux_client::InputSurface>("ExtraPane.Native",1,0,"InputSurface");
    Controller controller(unavailable);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("receiver",&controller);
    engine.load(QUrl("qrc:/Main.qml"));
    if(engine.rootObjects().isEmpty())return 1;
    controller.setWindow(qobject_cast<QQuickWindow*>(engine.rootObjects().first()));
    QObject::connect(&controller,&Controller::hostChanged,&controller,[&controller]{qInfo().noquote()<<controller.hostStatus();});
    const auto hostOption=app.arguments().indexOf("--host");
    if(hostOption>=0 && hostOption+1<app.arguments().size())QTimer::singleShot(0,&controller,[&controller,&app,hostOption]{controller.startHost(app.arguments().at(hostOption+1));});
    QObject::connect(&app,&QCoreApplication::aboutToQuit,&controller,&Controller::shutdown);
    return app.exec();
}
