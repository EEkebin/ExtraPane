#include "windows_child.hpp"
#include "windows_controller.hpp"
#include <ExtraPaneVersion.h>
#include <QCoreApplication>
#include <QFileInfo>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QSettings>
#include <QTimer>

using extrapane::windows_bridge::Child;
struct WindowsController::Impl {Child host,client;QString hostPending,clientPending,cameraProblem;bool hostFailed=false,clientFailed=false;quint64 codeExpires=0;HANDLE setup=nullptr;~Impl(){if(setup)CloseHandle(setup);}};
WindowsController::WindowsController(QObject* parent):QObject(parent),impl_(std::make_unique<Impl>()){
    const auto dir=QCoreApplication::applicationDirPath();
    if(!QFileInfo::exists(dir+"/dm-view.exe"))unavailable_="The native display engine is missing from this installation.";
    if(!QFileInfo::exists(dir+"/ExtraPane.Service.exe"))hostUnavailable_="The server helper is missing from this installation.";
    if(!unavailable_.isEmpty())hostUnavailable_=unavailable_;
    resources_.setCapability(extrapane::ResourceKind::display,{unavailable_.isEmpty(),unavailable_.toStdString()});
    refreshAddresses();auto* timer=new QTimer(this);timer->setInterval(100);
    connect(timer,&QTimer::timeout,this,&WindowsController::poll);timer->start();
}
WindowsController::~WindowsController(){shutdown();}
QString WindowsController::version()const{return QString::fromUtf8(EXTRAPANE_VERSION);}
bool WindowsController::hosting()const{return impl_->host.process!=nullptr;}
bool WindowsController::active()const{return impl_->client.process!=nullptr;}
QString WindowsController::hostAvailability()const{
    if(hostMode_!="camera")return hostUnavailable_;
    if(!QFileInfo::exists(QCoreApplication::applicationDirPath()+"/ExtraPane.Resources.exe"))return "The camera connection engine is missing from this build.";
    QSettings component("HKEY_LOCAL_MACHINE\\Software\\Classes\\CLSID\\{5A053AD3-02CD-4281-8367-CEF990182004}",QSettings::Registry64Format);
    wchar_t installed[32768]{};DWORD bytes=sizeof(installed);
    const auto read=RegGetValueW(HKEY_LOCAL_MACHINE,L"Software\\Classes\\CLSID\\{5A053AD3-02CD-4281-8367-CEF990182004}\\InprocServer32",nullptr,RRF_RT_REG_SZ|RRF_SUBKEY_WOW6464KEY,nullptr,installed,&bytes);
    const auto path=read==ERROR_SUCCESS?QString::fromWCharArray(installed):QString{};
    if(component.value("ExtraPaneOwner").toString()!="ExtraPane.CameraSource/v1"||!QFileInfo::exists(path))return "Install the ExtraPane camera component using Camera setup before hosting a camera.";
    return {};
}
QString WindowsController::hostDescription()const{
    if(hostMode_=="camera")return "Receive a camera from another device for use in local apps. This preview accepts camera-only connections; display sharing remains available in Display mode.";
    return "Creates a virtual monitor when a display client connects. The display driver must be installed. Supports up to four clients.";
}
void WindowsController::setHostMode(QString mode){
    if(hosting()||(mode!="display"&&mode!="camera")||mode==hostMode_)return;
    hostMode_=std::move(mode);hostStatus_="Server stopped";emit hostChanged();
}
bool WindowsController::cameraSetupRunning()const{return impl_->setup!=nullptr;}
void WindowsController::setupCamera(){
    if(hosting()||impl_->setup)return;
    const auto exe=extrapane::windows_bridge::directory()/L"ExtraPane.CameraSetup.exe";
    if(!std::filesystem::is_regular_file(exe)){hostStatus_="Camera setup is missing from this build.";emit hostChanged();return;}
    SHELLEXECUTEINFOW info{sizeof(info)};info.fMask=SEE_MASK_NOCLOSEPROCESS|SEE_MASK_NOASYNC;
    info.lpVerb=L"runas";info.lpFile=exe.c_str();info.lpParameters=L"--install";info.nShow=SW_HIDE;
    if(!ShellExecuteExW(&info)){hostStatus_=GetLastError()==ERROR_CANCELLED?"Camera setup canceled.":"Camera setup could not start.";}
    else{impl_->setup=info.hProcess;hostStatus_="Installing camera component...";}
    emit hostChanged();
}
QString WindowsController::savedAddress()const{return QSettings("HKEY_CURRENT_USER\\Software\\DM\\Receiver",QSettings::NativeFormat).value("Peer").toString();}
void WindowsController::refreshAddresses(){
    addresses_={QVariantMap{{"label","Automatic route"},{"address","0.0.0.0"},{"index",0}}};
    for(const auto& nic:QNetworkInterface::allInterfaces()){
        auto flags=nic.flags();if(!flags.testFlag(QNetworkInterface::IsUp)||!flags.testFlag(QNetworkInterface::IsRunning)||flags.testFlag(QNetworkInterface::IsLoopBack))continue;
        if(nic.type()!=QNetworkInterface::Ethernet&&nic.type()!=QNetworkInterface::Wifi)continue;
        for(const auto& item:nic.addressEntries())if(item.ip().protocol()==QAbstractSocket::IPv4Protocol&&!item.ip().isLoopback())
            addresses_.push_back(QVariantMap{{"label",nic.humanReadableName()+" — "+item.ip().toString()},{"address",item.ip().toString()},{"index",nic.index()}});
    }emit addressesChanged();
}
void WindowsController::connectTo(QString server,QString local,bool input,unsigned scale,unsigned interfaceIndex,unsigned port,QString code){
    if(active())return;
    const QHostAddress address(server.trimmed());
    if(!resources_.canConnect()||address.protocol()!=QAbstractSocket::IPv4Protocol||address.isNull()||address.isMulticast()||address.isLoopback()||address.toIPv4Address()==0xffffffffu||!port||port>65535||scale>2){status_="Select a display and enter a valid server IPv4 address and port.";emit changed();return;}
    if(!code.isEmpty()&&(code.size()!=6||std::any_of(code.begin(),code.end(),[](QChar c){return c<'0'||c>'9';}))){status_="Access code must contain six digits.";emit changed();return;}
    bool valid=interfaceIndex==0&&local=="0.0.0.0";
    for(const auto& item:addresses_){auto entry=item.toMap();if(entry["address"].toString()==local&&entry["index"].toUInt()==interfaceIndex)valid=true;}
    if(!valid){status_="Refresh and select a connected local address.";emit changed();return;}
    try{
        impl_->clientFailed=false;impl_->clientPending.clear();
        auto renderer=QSettings("HKEY_CURRENT_USER\\Software\\DM\\Receiver",QSettings::NativeFormat).value("Renderer","d3d11").toString();
        if(renderer!="d3d11"&&renderer!="d3d12"&&renderer!="vulkan"&&renderer!="opengl")renderer="d3d11";
        std::vector<std::wstring> args{L"receive",L"--lan",L"--bind",local=="0.0.0.0"?L"auto":local.toStdWString(),L"--peer",address.toString().toStdWString(),L"--port",std::to_wstring(port),L"--parent-pid",std::to_wstring(GetCurrentProcessId()),L"--quality",scale==1?L"75":scale==2?L"50":L"native",L"--renderer",renderer.toStdWString()};
        if(input)args.push_back(L"--control");if(!code.isEmpty())args.push_back(L"--access-stdin");
        impl_->client.start((extrapane::windows_bridge::directory()/L"dm-view.exe").wstring(),args);
        if(!code.isEmpty()){auto bytes=code.toLatin1();impl_->client.send_control(bytes.toStdString()+"\n");bytes.fill('\0');code.fill(QChar(0));}
        QSettings("HKEY_CURRENT_USER\\Software\\DM\\Receiver",QSettings::NativeFormat).setValue("Peer",address.toString());
        resources_.setLocked(true);status_="Connecting in the native display window…";
    }catch(const std::exception& e){impl_->client.stop();status_=QString::fromUtf8(e.what());}
    emit changed();
}
void WindowsController::disconnectFromServer(){impl_->client.stop();resources_.setLocked(false);status_="Disconnected";emit changed();}
void WindowsController::startHost(QString address,unsigned port,bool requireCode){
    if(hosting())return;
    if(!hostAvailability().isEmpty()){hostStatus_=hostAvailability();emit hostChanged();return;}
    if(!port||port>65535){hostStatus_="Enter a valid TCP port.";emit hostChanged();return;}
    bool local=false;refreshAddresses();
    for(const auto& item:addresses_)if(item.toMap()["address"].toString()==address)local=true;
    if(!local){hostStatus_="Select a connected local address.";emit hostChanged();return;}
    try{
        impl_->hostFailed=false;impl_->hostPending.clear();impl_->cameraProblem.clear();
        if(hostMode_=="camera"){
            std::vector<std::wstring> args{L"serve-camera",L"--bind",address=="0.0.0.0"?L"auto":address.toStdWString(),L"--port",std::to_wstring(port),L"--parent-pid",std::to_wstring(GetCurrentProcessId())};
            if(requireCode)args.push_back(L"--require-code");
            impl_->host.start((extrapane::windows_bridge::directory()/L"ExtraPane.Resources.exe").wstring(),args,true);
        }else impl_->host.start_driver(true,false,port,requireCode,address=="0.0.0.0"?L"auto":address.toStdWString());
        hostStatus_="Starting server...";
    }
    catch(const std::exception& e){hostStatus_=QString::fromUtf8(e.what());}
    emit hostChanged();
}
void WindowsController::stopHost(){impl_->host.stop();impl_->codeExpires=0;code_.clear();hostStatus_="Server stopped";emit hostChanged();}
void WindowsController::generateAccessCode(){
    if(!hosting())return;
    try{impl_->host.send_control(hostMode_=="camera"?"G\n":"G");}catch(const std::exception& e){hostStatus_=QString::fromUtf8(e.what());}emit hostChanged();
}
void WindowsController::poll(){
    if(impl_->setup&&WaitForSingleObject(impl_->setup,0)==WAIT_OBJECT_0){
        DWORD result=1;GetExitCodeProcess(impl_->setup,&result);CloseHandle(impl_->setup);impl_->setup=nullptr;
        hostStatus_=result==0?"Camera setup completed. You can start the camera server.":"Camera setup failed (exit "+QString::number(result)+").";emit hostChanged();
    }
    auto drain=[&](Child& child,QString& pending,bool host){
        if(!child.process)return;
        pending+=QString::fromUtf8(child.drain());
        while(pending.contains('\n')){
            auto at=pending.indexOf('\n');auto line=pending.left(at).trimmed();pending.remove(0,at+1);
            if(line.startsWith("ACCESS_CODE ")){
                auto digits=line.mid(12);
                if(host&&digits.size()==6&&std::all_of(digits.begin(),digits.end(),[](QChar c){return c>='0'&&c<='9';})){
                    code_=digits+" — one use, expires in five minutes";impl_->codeExpires=GetTickCount64()+300000;emit hostChanged();
                }digits.fill(QChar(0));line.fill(QChar(0));continue;
            }
            if(line.startsWith("ACCESS_STATUS ")){if(host){code_="Code "+line.mid(14)+". Generate a new code.";impl_->codeExpires=0;emit hostChanged();}continue;}
            if(line.startsWith("error:")){(host?impl_->hostFailed:impl_->clientFailed)=true;(host?hostStatus_:status_)=line.mid(6).trimmed();}
            else if(line.startsWith("VIDEO_RECEIVING")||line.startsWith("VIDEO_STARTED")){(host?hostStatus_:status_)="Connected";}
            else if(line.startsWith("LISTENING")||line.startsWith("HOST_LISTENING")){if(host)hostStatus_="Listening";}
            else if(line.startsWith("CAMERA_STARTED")){if(host){impl_->cameraProblem.clear();hostStatus_="Camera connected — select ExtraPane Camera in your application.";}}
            else if(line.startsWith("CAMERA_SESSION_ERROR")){if(host){impl_->cameraProblem=line.section(' ',2);hostStatus_="Camera connection failed; server still listening. "+impl_->cameraProblem;}}
            else if(line.startsWith("CAMERA_DISCONNECTED")){if(host)hostStatus_=impl_->cameraProblem.isEmpty()?"Listening — camera client disconnected.":"Listening. Last camera error: "+impl_->cameraProblem;}
            if(host)emit hostChanged();else emit changed();
        }
        if(pending.size()>16384)pending.clear();
        if(WaitForSingleObject(child.process,0)==WAIT_OBJECT_0){child.stop();if(host){impl_->codeExpires=0;code_.clear();if(!impl_->hostFailed)hostStatus_="Server stopped";emit hostChanged();}else{resources_.setLocked(false);if(!impl_->clientFailed)status_="Disconnected";emit changed();}}
    };
    drain(impl_->host,impl_->hostPending,true);drain(impl_->client,impl_->clientPending,false);
    if(impl_->codeExpires&&GetTickCount64()>=impl_->codeExpires){impl_->codeExpires=0;code_="Code expired. Generate a new code.";emit hostChanged();}
}
void WindowsController::shutdown(){impl_->host.stop();impl_->client.stop();resources_.setLocked(false);}
