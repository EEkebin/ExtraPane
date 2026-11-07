#include "windows_controller.hpp"
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QWindow>
#include <QTimer>
#include <QStringList>
#include <windows.h>
#include <filesystem>

namespace {
// The installer finds this hidden top-level window and sends the legacy tray
// Exit command. A message-only HWND would not be found by FindWindow.
class InstallerShutdownWindow {
public:
    bool create(){
        instance_=GetModuleHandleW(nullptr);
        WNDCLASSW type{};
        type.lpfnWndProc=&procedure;
        type.hInstance=instance_;
        type.lpszClassName=L"ExtraPane.Tray";
        atom_=RegisterClassW(&type);
        if(!atom_)return false;
        window_=CreateWindowExW(WS_EX_TOOLWINDOW,type.lpszClassName,L"ExtraPane shutdown",
            WS_POPUP,0,0,0,0,nullptr,nullptr,instance_,nullptr);
        return window_!=nullptr;
    }
    ~InstallerShutdownWindow(){
        if(window_)DestroyWindow(window_);
        if(atom_)UnregisterClassW(L"ExtraPane.Tray",instance_);
    }
    InstallerShutdownWindow()=default;
    InstallerShutdownWindow(const InstallerShutdownWindow&)=delete;
    InstallerShutdownWindow& operator=(const InstallerShutdownWindow&)=delete;
private:
    static LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wparam,LPARAM lparam){
        if(message==WM_COMMAND&&wparam==3&&lparam==0){
            // Queue normal Qt shutdown so aboutToQuit stops both roles and
            // releases session-owned resources before the instance mutex closes.
            QMetaObject::invokeMethod(qApp,[]{QCoreApplication::quit();},Qt::QueuedConnection);
            return 0;
        }
        return DefWindowProcW(window,message,wparam,lparam);
    }
    HINSTANCE instance_=nullptr;
    ATOM atom_=0;
    HWND window_=nullptr;
};
}

int main(int argc,char** argv){
    bool checkUi=false;
    for(int i=1;i<argc;++i)if(QString::fromLocal8Bit(argv[i])=="--check-ui")checkUi=true;
    if(checkUi){
        SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
        wchar_t module[32768]{};
        const auto length=GetModuleFileNameW(nullptr,module,32768);
        if(!length||length>=32768)return 2;
        const auto plugin=std::filesystem::path(module).parent_path()/L"platforms"/L"qoffscreen.dll";
        if(!std::filesystem::is_regular_file(plugin))return 2;
        qputenv("QT_QPA_PLATFORM","offscreen");qputenv("QT_QUICK_BACKEND","software");
    }
    struct Instance {HANDLE handle=nullptr;~Instance(){if(handle)CloseHandle(handle);}} instance;
    if(!checkUi){instance.handle=CreateMutexW(nullptr,FALSE,L"Local\\SideScreen.App");if(!instance.handle||GetLastError()==ERROR_ALREADY_EXISTS)return 1;}
    QQuickStyle::setStyle("Fusion");
    QApplication app(argc,argv);
    app.setOrganizationName("ExtraPane");app.setApplicationName("ExtraPane");
    app.setQuitOnLastWindowClosed(false);
    const QIcon icon(":/extrapane.png");app.setWindowIcon(icon);
    WindowsController controller;
    QQmlApplicationEngine engine;
    bool qmlFailed=false;
    QObject::connect(&engine,&QQmlEngine::warnings,&app,[&](const QList<QQmlError>&){qmlFailed=true;});
    engine.rootContext()->setContextProperty("receiver",&controller);
    engine.load(QUrl("qrc:/Main.qml"));
    if(engine.rootObjects().isEmpty())return 1;
    auto* window=qobject_cast<QWindow*>(engine.rootObjects().first());
    if(!window)return 1;
    if(checkUi){
        for(int i=1;i<4;++i)controller.resources()->select(i,true);
        QTimer::singleShot(0,&app,[&]{app.exit(qmlFailed?1:0);});return app.exec();
    }
    QSystemTrayIcon tray(icon);tray.setToolTip("ExtraPane server");
    QMenu menu;auto* show=menu.addAction("Open ExtraPane");auto* stop=menu.addAction("Stop server");menu.addSeparator();auto* quit=menu.addAction("Exit ExtraPane");
    tray.setContextMenu(&menu);
    QObject::connect(show,&QAction::triggered,window,[&]{window->show();window->requestActivate();tray.hide();});
    QObject::connect(stop,&QAction::triggered,&controller,&WindowsController::stopHost);
    QObject::connect(quit,&QAction::triggered,&app,&QCoreApplication::quit);
    QObject::connect(&app,&QGuiApplication::lastWindowClosed,&app,[&]{if(controller.hosting()&&QSystemTrayIcon::isSystemTrayAvailable())tray.show();else app.quit();});
    QObject::connect(&controller,&WindowsController::hostChanged,&app,[&]{if(!controller.hosting()&&!window->isVisible())app.quit();});
    QObject::connect(&app,&QCoreApplication::aboutToQuit,&controller,&WindowsController::shutdown);
    InstallerShutdownWindow installerShutdown;
    if(!installerShutdown.create())return 1;
    return app.exec();
}
