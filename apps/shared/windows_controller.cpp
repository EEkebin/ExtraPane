#include "windows_child.hpp"
#include "windows_controller.hpp"
#include <ExtraPaneVersion.h>
#include <QCoreApplication>
#include <QFileInfo>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QSettings>
#include <QTimer>
#include <QProcess>
#include <QSet>

using extrapane::windows_bridge::Child;
struct WindowsController::Impl {QProcess* inventory=nullptr; QByteArray inventoryOutput; bool inventoryFailed=false; Child host,client;QString hostPending,clientPending,cameraProblem;bool hostFailed=false,clientFailed=false;quint64 codeExpires=0;HANDLE setup=nullptr;~Impl(){if(setup)CloseHandle(setup);}};
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
QString WindowsController::availability()const{
    if(clientMode_=="camera")return QFileInfo::exists(QCoreApplication::applicationDirPath()+"/ExtraPane.Resources.exe")?QString{}:QString("The camera connection engine is missing from this installation.");
    return unavailable_;
}
void WindowsController::setClientMode(QString mode){
    if(active()||cameraScanRunning()||(mode!="display"&&mode!="camera")||mode==clientMode_)return;
    clientMode_=std::move(mode);status_="Ready";emit changed();
}
bool WindowsController::cameraScanRunning()const{return impl_->inventory!=nullptr;}
void WindowsController::setSelectedCamera(QString id){
    if(active()||cameraScanRunning())return;
    if(!id.isEmpty()&&std::none_of(cameras_.begin(),cameras_.end(),[&](const QVariant& v){return v.toMap()["id"].toString()==id;}))return;
    selectedCamera_=std::move(id);emit camerasChanged();
}
void WindowsController::refreshCameras(){
    if(active()||cameraScanRunning()||clientMode_!="camera")return;
    if(!availability().isEmpty()){cameraStatus_=availability();emit camerasChanged();return;}
    auto* process=new QProcess(this);impl_->inventory=process;impl_->inventoryOutput.clear();impl_->inventoryFailed=false;
    cameraStatus_="Listing cameras...";emit camerasChanged();
    process->setProcessChannelMode(QProcess::MergedChannels);
    auto finish=[this,process](bool success){
        if(impl_->inventory!=process)return;
        impl_->inventoryOutput+=process->readAll();
        QVariantList found;QSet<QString> ids;bool done=false;
        bool valid=success&&!impl_->inventoryFailed&&impl_->inventoryOutput.size()<=1024*1024;
        if(valid)for(auto line:impl_->inventoryOutput.split('\n')){
            if(line.endsWith('\r'))line.chop(1);if(line.isEmpty())continue;
            if(line=="CAMERA_LIST_DONE"){if(done)valid=false;done=true;continue;}
            const auto fields=line.split('\t');
            if(done||fields.size()!=4||fields[0]!="CAMERA_DEVICE_V1"||fields[1]!=QByteArray::number(found.size())||fields[2].isEmpty()||fields[2].size()>2048||fields[3].isEmpty()||fields[3].size()>8192||fields[3].size()%2||found.size()>=128){valid=false;break;}
            const auto id=fields[3];
            if(std::any_of(id.begin(),id.end(),[](char c){return !((c>='0'&&c<='9')||(c>='a'&&c<='f'));})){valid=false;break;}
            const auto decoded=QByteArray::fromHex(id);const auto value=QString::fromUtf8(decoded);const auto label=QString::fromUtf8(fields[2]);
            if(value.toUtf8()!=decoded||label.toUtf8()!=fields[2]||decoded.contains('\n')||decoded.contains('\r')||decoded.contains('\0')||ids.contains(QString::fromLatin1(id))){valid=false;break;}
            ids.insert(QString::fromLatin1(id));found.push_back(QVariantMap{{"label",label},{"id",QString::fromLatin1(id)}});
        }
        impl_->inventory=nullptr;impl_->inventoryOutput.clear();process->deleteLater();
        if(valid&&done){cameras_=found;if(!ids.contains(selectedCamera_))selectedCamera_.clear();cameraStatus_=found.isEmpty()?"No cameras found. Check camera permissions and reconnect the device, then refresh.":"Select a camera. Sharing uses 1280 x 720 at 30 FPS; unsupported camera modes report an error.";}
        else{cameras_.clear();selectedCamera_.clear();cameraStatus_="Camera listing failed or timed out. Check camera permissions and the installation, then refresh.";}
        emit camerasChanged();
    };
    connect(process,&QProcess::readyRead,this,[this,process]{
        if(impl_->inventory!=process)return;impl_->inventoryOutput+=process->readAll();
        if(impl_->inventoryOutput.size()>1024*1024){impl_->inventoryFailed=true;impl_->inventoryOutput.clear();process->kill();}
    });
    connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[finish](int code,QProcess::ExitStatus state){finish(code==0&&state==QProcess::NormalExit);});
    connect(process,&QProcess::errorOccurred,this,[finish](QProcess::ProcessError error){if(error==QProcess::FailedToStart)finish(false);});
    QTimer::singleShot(10000,process,[this,process]{if(impl_->inventory==process){impl_->inventoryFailed=true;process->kill();}});
    process->start(QCoreApplication::applicationDirPath()+"/ExtraPane.Resources.exe",QStringList{"list-cameras"});
}
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
    if(active()||cameraScanRunning())return;
    const bool camera=clientMode_=="camera";
    const QHostAddress address(server.trimmed());
    if((!camera&&!resources_.canConnect())||address.protocol()!=QAbstractSocket::IPv4Protocol||address.isNull()||address.isMulticast()||address.isLoopback()||address.toIPv4Address()==0xffffffffu||!port||port>65535||scale>2){status_="Select a resource and enter a valid server IPv4 address and port.";emit changed();return;}
    if(!availability().isEmpty()){status_=availability();emit changed();return;}
    if(camera&&(selectedCamera_.isEmpty()||std::none_of(cameras_.begin(),cameras_.end(),[&](const QVariant& v){return v.toMap()["id"].toString()==selectedCamera_;}))){status_="Refresh cameras and select a local camera.";emit changed();return;}
    if(!code.isEmpty()&&(code.size()!=6||std::any_of(code.begin(),code.end(),[](QChar c){return c<'0'||c>'9';}))){status_="Access code must contain six digits.";emit changed();return;}
    bool valid=interfaceIndex==0&&local=="0.0.0.0";
    for(const auto& item:addresses_){auto entry=item.toMap();if(entry["address"].toString()==local&&entry["index"].toUInt()==interfaceIndex)valid=true;}
    if(!valid){status_="Refresh and select a connected local address.";emit changed();return;}
    try{
        impl_->clientFailed=false;impl_->clientPending.clear();
        if(camera){
            std::vector<std::wstring> args{L"send-camera",L"--bind",local=="0.0.0.0"?L"auto":local.toStdWString(),L"--peer",address.toString().toStdWString(),L"--port",std::to_wstring(port),L"--parent-pid",std::to_wstring(GetCurrentProcessId()),L"--device-stdin"};
            if(!code.isEmpty())args.push_back(L"--access-stdin");
            impl_->client.start((extrapane::windows_bridge::directory()/L"ExtraPane.Resources.exe").wstring(),args,true);
            impl_->client.send_control(QByteArray::fromHex(selectedCamera_.toLatin1()).toStdString()+"\n");
        }else{
        auto renderer=QSettings("HKEY_CURRENT_USER\\Software\\DM\\Receiver",QSettings::NativeFormat).value("Renderer","d3d11").toString();
        if(renderer!="d3d11"&&renderer!="d3d12"&&renderer!="vulkan"&&renderer!="opengl")renderer="d3d11";
        std::vector<std::wstring> args{L"receive",L"--lan",L"--bind",local=="0.0.0.0"?L"auto":local.toStdWString(),L"--peer",address.toString().toStdWString(),L"--port",std::to_wstring(port),L"--parent-pid",std::to_wstring(GetCurrentProcessId()),L"--quality",scale==1?L"75":scale==2?L"50":L"native",L"--renderer",renderer.toStdWString()};
        if(input)args.push_back(L"--control");if(!code.isEmpty())args.push_back(L"--access-stdin");
        impl_->client.start((extrapane::windows_bridge::directory()/L"dm-view.exe").wstring(),args);
        }
        if(!code.isEmpty()){auto bytes=code.toLatin1();impl_->client.send_control(bytes.toStdString()+"\n");bytes.fill('\0');code.fill(QChar(0));}
        QSettings("HKEY_CURRENT_USER\\Software\\DM\\Receiver",QSettings::NativeFormat).setValue("Peer",address.toString());
        resources_.setLocked(true);status_=camera?"Connecting camera; capture starts after acceptance...":"Connecting in the native display window…";
    }catch(const std::exception& e){impl_->client.stop();resources_.setLocked(false);status_=QString::fromUtf8(e.what());}
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
            else if(line.startsWith("CAMERA_STARTED")){if(!host)status_="Sharing camera. Select ExtraPane Camera in an app on the server.";if(host){impl_->cameraProblem.clear();hostStatus_="Camera connected — select ExtraPane Camera in your application.";}}
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
void WindowsController::shutdown(){if(impl_->inventory){auto* process=impl_->inventory;impl_->inventory=nullptr;process->disconnect(this);process->kill();process->waitForFinished(1000);process->deleteLater();}impl_->host.stop();impl_->client.stop();resources_.setLocked(false);}
