#include "controller.hpp"
#include "resource_source.hpp"
#include <QJsonDocument>
#include <QJsonArray>
#include <cstdio>
#include <cstring>
#include <QApplication>
#include <QSystemTrayIcon>
#include <QMenu>
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
    // Device providers are third-party code and may block indefinitely. The UI
    // runs this bounded-output helper in a killable process, without a GUI.
    if(argc>1 && std::strcmp(argv[1],"--list-resource")==0){
        QCoreApplication inventory(argc,argv);
        if(argc!=3 || (std::strcmp(argv[2],"camera")&&std::strcmp(argv[2],"microphone")))return 2;
        GError* error=nullptr;if(!gst_init_check(nullptr,nullptr,&error)){if(error)g_error_free(error);return 3;}
        using Source=dm::linux_client::ResourceSource;
        const auto kind=std::strcmp(argv[2],"camera")==0?Source::Kind::Camera:Source::Kind::Microphone;
        QVariantList devices;
        try{const auto reason=Source::unavailable(kind);if(reason.isEmpty())devices=Source::devices(kind);
            else devices.push_back(QVariantMap{{"id",""},{"label",reason},{"available",false}});
        }catch(...){return 4;}
        const auto json=QJsonDocument(QJsonArray::fromVariantList(devices)).toJson(QJsonDocument::Compact);
        if(json.size()>1024*1024)return 5;
        return std::fwrite(json.constData(),1,static_cast<size_t>(json.size()),stdout)==static_cast<size_t>(json.size())?0:6;
    }
    std::signal(SIGTERM,[](int){quitRequested=1;});
    std::signal(SIGINT,[](int){quitRequested=1;});
    QCoreApplication::setAttribute(Qt::AA_SynthesizeMouseForUnhandledTouchEvents,false);
    QCoreApplication::setAttribute(Qt::AA_SynthesizeMouseForUnhandledTabletEvents,false);
    // A consistent Controls style honors our palette on KDE light and dark desktops.
    QQuickStyle::setStyle("Material");
    QApplication app(argc,argv);
    app.setQuitOnLastWindowClosed(false);
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
    controller.setEngine(&engine);
    engine.rootContext()->setContextProperty("receiver",&controller);
    engine.load(QUrl("qrc:/Main.qml"));
    if(engine.rootObjects().isEmpty())return 1;
    auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if(!window)return 1;
    controller.setWindow(window);
    QSystemTrayIcon tray(QIcon(":/extrapane.png"));tray.setToolTip("ExtraPane");
    QMenu trayMenu;auto* open=trayMenu.addAction("Open ExtraPane");auto* stop=trayMenu.addAction("Stop server");
    auto* disconnect=trayMenu.addAction("Disconnect all");trayMenu.addSeparator();auto* exit=trayMenu.addAction("Exit ExtraPane");tray.setContextMenu(&trayMenu);
    auto restore=[&]{if(window->visibility()==QWindow::Minimized)window->showNormal();else window->show();window->raise();window->requestActivate();tray.hide();};
    QObject::connect(open,&QAction::triggered,&app,restore);
    QObject::connect(&tray,&QSystemTrayIcon::activated,&app,[&](QSystemTrayIcon::ActivationReason reason){if(reason==QSystemTrayIcon::DoubleClick)restore();});
    QObject::connect(stop,&QAction::triggered,&controller,&Controller::stopHost);
    QObject::connect(disconnect,&QAction::triggered,&controller,&Controller::disconnectFromServer);
    QObject::connect(exit,&QAction::triggered,&app,&QCoreApplication::quit);
    QObject::connect(&app,&QGuiApplication::lastWindowClosed,&app,[&]{
        if(controller.active()||controller.hosting()){
            if(QSystemTrayIcon::isSystemTrayAvailable())tray.show();else restore();
        }else app.quit();
    });
    auto updateTray=[&]{stop->setEnabled(controller.hosting());disconnect->setEnabled(controller.active());if(!window->isVisible()&&!controller.hosting()&&!controller.active())app.quit();};
    QObject::connect(&controller,&Controller::changed,&app,updateTray);QObject::connect(&controller,&Controller::hostChanged,&app,updateTray);updateTray();
    QObject::connect(&controller,&Controller::hostChanged,&controller,[&controller]{qInfo().noquote()<<controller.hostStatus();});
    const auto hostOption=app.arguments().indexOf("--host");
    if(hostOption>=0 && hostOption+1<app.arguments().size())QTimer::singleShot(0,&controller,[&controller,&app,hostOption]{controller.startHost(app.arguments().at(hostOption+1));});
    QObject::connect(&app,&QCoreApplication::aboutToQuit,&controller,&Controller::shutdown);
    return app.exec();
}
