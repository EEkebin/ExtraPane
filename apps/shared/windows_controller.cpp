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
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <shlobj.h>

using extrapane::windows_bridge::Child;
struct WindowsController::Impl {QProcess* recoveryScan=nullptr;QByteArray recoveryOutput;bool recoveryInvalid=false,recovering=false;QProcess* inventory=nullptr; QByteArray inventoryOutput; bool inventoryFailed=false; Child host,client;QString hostPending,clientPending,cameraProblem;bool hostFailed=false,clientFailed=false;quint64 codeExpires=0;HANDLE setup=nullptr;~Impl(){if(setup)CloseHandle(setup);}};
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
    if(clientMode_=="usb")return QFileInfo::exists(QCoreApplication::applicationDirPath()+"/ExtraPane.Usb.exe")?QString{}:QString("The USB connection engine is missing from this installation.");
    if(clientMode_=="microphone")return QFileInfo::exists(QCoreApplication::applicationDirPath()+"/ExtraPane.Microphone.exe")?QString{}:QString("The microphone connection engine is missing from this installation.");
    if(clientMode_=="camera")return QFileInfo::exists(QCoreApplication::applicationDirPath()+"/ExtraPane.Resources.exe")?QString{}:QString("The camera connection engine is missing from this installation.");
    return unavailable_;
}
void WindowsController::setClientMode(QString mode){
    if(active()||cameraScanRunning()||(mode!="display"&&mode!="camera"&&mode!="microphone"&&mode!="usb")||mode==clientMode_)return;
    clientMode_=std::move(mode);cameras_.clear();selectedCamera_.clear();cameraStatus_="Refresh devices and choose a source. Capture starts after admission.";status_="Ready";emit camerasChanged();emit changed();
}
bool WindowsController::cameraScanRunning()const{return impl_->inventory!=nullptr;}
void WindowsController::setSelectedCamera(QString id){
    if(active()||cameraScanRunning())return;
    if(!id.isEmpty()&&std::none_of(cameras_.begin(),cameras_.end(),[&](const QVariant& v){return v.toMap()["id"].toString()==id&&v.toMap().value("available",true).toBool();}))return;
    selectedCamera_=std::move(id);emit camerasChanged();
}
void WindowsController::refreshCameras(){
    if(active()||cameraScanRunning()||clientMode_=="display")return;
    if(!availability().isEmpty()){cameraStatus_=availability();emit camerasChanged();return;}
    auto* process=new QProcess(this);impl_->inventory=process;impl_->inventoryOutput.clear();impl_->inventoryFailed=false;
    cameraStatus_="Listing devices...";emit camerasChanged();
    process->setProcessChannelMode(QProcess::MergedChannels);
    auto finish=[this,process](bool success){
        if(impl_->inventory!=process)return;
        impl_->inventoryOutput+=process->readAll();
        QVariantList found;QSet<QString> ids;bool done=false;
        bool valid=success&&!impl_->inventoryFailed&&impl_->inventoryOutput.size()<=1024*1024;
        if(valid)for(auto line:impl_->inventoryOutput.split('\n')){
            if(line.endsWith('\r'))line.chop(1);if(line.isEmpty())continue;
            if(line==(clientMode_=="usb"?"USB_LIST_DONE":clientMode_=="microphone"?"MICROPHONE_LIST_DONE":"CAMERA_LIST_DONE")){if(done)valid=false;done=true;continue;}
            auto fields=line.split('\t');
            bool available=true;QString reason;
            if(clientMode_=="usb"){
                if(fields.size()!=4||fields[0]!="USB_DEVICE_V1"){valid=false;break;}
                available=fields[3]=="available";reason=available?QString{}:QString::fromUtf8(fields[3]);
                fields={QByteArray("USB_DEVICE_V1"),QByteArray::number(found.size()),fields[2],fields[1]};
            }
            if(done||fields.size()!=4||fields[0]!=(clientMode_=="usb"?"USB_DEVICE_V1":clientMode_=="microphone"?"MICROPHONE_DEVICE_V1":"CAMERA_DEVICE_V1")||fields[1]!=QByteArray::number(found.size())||fields[2].isEmpty()||fields[2].size()>2048||fields[3].isEmpty()||fields[3].size()>8192||fields[3].size()%2||found.size()>=128){valid=false;break;}
            const auto id=fields[3];
            if(std::any_of(id.begin(),id.end(),[](char c){return !((c>='0'&&c<='9')||(c>='a'&&c<='f'));})){valid=false;break;}
            const auto decoded=QByteArray::fromHex(id);const auto value=QString::fromUtf8(decoded);const auto label=QString::fromUtf8(fields[2]);
            if(value.toUtf8()!=decoded||label.toUtf8()!=fields[2]||decoded.contains('\n')||decoded.contains('\r')||decoded.contains('\0')||ids.contains(QString::fromLatin1(id))){valid=false;break;}
            ids.insert(QString::fromLatin1(id));found.push_back(QVariantMap{{"label",available?label:label+" ? "+reason},{"id",QString::fromLatin1(id)},{"available",available}});
        }
        impl_->inventory=nullptr;impl_->inventoryOutput.clear();process->deleteLater();
        if(valid&&done){cameras_=found;if(!ids.contains(selectedCamera_))selectedCamera_.clear();cameraStatus_=found.isEmpty()?"No devices found. Check privacy settings and reconnect the device, then refresh.":clientMode_=="usb"?"Only eligible removable storage can be shared. USB sharing temporarily removes it from this PC; eject on the receiving PC before stopping.":clientMode_=="microphone"?"Select a microphone. Supports 48 kHz mono/stereo sources; other formats report an error.":"Select a camera. Sharing uses 1280 x 720 at 30 FPS; unsupported camera modes report an error.";}
        else{cameras_.clear();selectedCamera_.clear();cameraStatus_="Device listing failed or timed out. Check privacy settings and the installation, then refresh.";}
        emit camerasChanged();
    };
    connect(process,&QProcess::readyRead,this,[this,process]{
        if(impl_->inventory!=process)return;impl_->inventoryOutput+=process->readAll();
        if(impl_->inventoryOutput.size()>1024*1024){impl_->inventoryFailed=true;impl_->inventoryOutput.clear();process->kill();}
    });
    connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[finish](int code,QProcess::ExitStatus state){finish(code==0&&state==QProcess::NormalExit);});
    connect(process,&QProcess::errorOccurred,this,[finish](QProcess::ProcessError error){if(error==QProcess::FailedToStart)finish(false);});
    QTimer::singleShot(10000,process,[this,process]{if(impl_->inventory==process){impl_->inventoryFailed=true;process->kill();}});
    process->start(QCoreApplication::applicationDirPath()+(clientMode_=="usb"?"/ExtraPane.Usb.exe":clientMode_=="microphone"?"/ExtraPane.Microphone.exe":"/ExtraPane.Resources.exe"),QStringList{clientMode_=="usb"?"list-storage":clientMode_=="microphone"?"list-microphones":"list-cameras"});
}
QString WindowsController::version()const{return QString::fromUtf8(EXTRAPANE_VERSION);}
bool WindowsController::hosting()const{return impl_->host.process!=nullptr;}
bool WindowsController::active()const{return impl_->client.process!=nullptr;}
QString WindowsController::hostAvailability()const{
    if(hostMode_=="usb"&&!QFileInfo::exists(QCoreApplication::applicationDirPath()+"/ExtraPane.Usb.exe"))return "The USB connection engine is missing from this installation.";
    if(hostMode_=="microphone"||hostMode_=="usb"){
        const auto dir=QCoreApplication::applicationDirPath();
        if(hostMode_=="microphone"&&(!QFileInfo::exists(dir+"/ExtraPane.Microphone.exe")||!QFileInfo::exists(dir+"/ExtraPane.MicrophoneEndpoint.exe")))return "The microphone receiving engine is missing from this installation.";
        PWSTR location=nullptr;
        if(FAILED(SHGetKnownFolderPath(FOLDERID_ProgramFilesX64,0,nullptr,&location)))return "The microphone component location is unavailable.";
        const auto root=QString::fromWCharArray(location)+"/ExtraPane Components/Microphone/";CoTaskMemFree(location);
        for(const auto* name:{"usbip.exe","libusbip.dll","resources.dll"})if(!QFileInfo::exists(root+name))return "Run receiving component setup before accepting a microphone or USB device.";
        return {}; // The endpoint verifies the protected backend before attaching.
    }
    if(hostMode_!="camera")return hostUnavailable_;
    if(!QFileInfo::exists(QCoreApplication::applicationDirPath()+"/ExtraPane.Resources.exe"))return "The camera connection engine is missing from this build.";
    wchar_t installed[32768]{};DWORD bytes=sizeof(installed);
    const auto read=RegGetValueW(HKEY_LOCAL_MACHINE,L"Software\\Classes\\CLSID\\{5A053AD3-02CD-4281-8367-CEF990182004}\\InprocServer32",nullptr,RRF_RT_REG_SZ|RRF_SUBKEY_WOW6464KEY,nullptr,installed,&bytes);
    const auto path=read==ERROR_SUCCESS?QString::fromWCharArray(installed):QString{};
    wchar_t owner[128]{};DWORD ownerBytes=sizeof(owner);
    const auto ownerRead=RegGetValueW(HKEY_LOCAL_MACHINE,L"Software\\Classes\\CLSID\\{5A053AD3-02CD-4281-8367-CEF990182004}",L"ExtraPaneOwner",RRF_RT_REG_SZ|RRF_SUBKEY_WOW6464KEY,nullptr,owner,&ownerBytes);
    if(ownerRead!=ERROR_SUCCESS||QString::fromWCharArray(owner)!="ExtraPane.CameraSource/v1"||!QFileInfo::exists(path))return "Install the ExtraPane camera component using Camera setup before hosting a camera.";
    return {};
}
QString WindowsController::hostDescription()const{
    if(hostMode_=="usb")return "Receive one removable USB storage device. Close files and eject it here before disconnecting. Interrupted transfers require recovery on the source PC.";
    if(hostMode_=="microphone")return "Receive a microphone for use in local recording apps. Microphone setup is required. This preview uses a separate connection from display and camera.";
    if(hostMode_=="camera")return "Receive a camera from another device for use in local apps. This preview accepts camera-only connections; display sharing remains available in Display mode.";
    return "Creates a virtual monitor when a display client connects. The display driver must be installed. Supports up to four clients.";
}
void WindowsController::setHostMode(QString mode){
    if(hosting()||(mode!="display"&&mode!="camera"&&mode!="microphone"&&mode!="usb")||mode==hostMode_)return;
    hostMode_=std::move(mode);hostStatus_="Server stopped";emit hostChanged();
}
bool WindowsController::cameraSetupRunning()const{return impl_->setup!=nullptr;}
void WindowsController::setMicrophoneMuted(bool muted){
    if(clientMode_!="microphone"||!active())return;
    try{impl_->client.send_control(muted?"M\n":"U\n");microphoneMuted_=muted;}
    catch(const std::exception& e){status_=QString::fromUtf8(e.what());}
    emit changed();
}
void WindowsController::setupMicrophone(){
    if(active()||hosting()||impl_->setup)return;
    const auto exe=extrapane::windows_bridge::directory()/L"ExtraPane.UsbSetup.exe";
    if(!std::filesystem::is_regular_file(exe)){hostStatus_="Microphone component setup is missing from this build.";emit hostChanged();return;}
    SHELLEXECUTEINFOW info{sizeof(info)};info.fMask=SEE_MASK_NOCLOSEPROCESS|SEE_MASK_NOASYNC;
    info.lpVerb=L"runas";info.lpFile=exe.c_str();info.lpParameters=L"--install";info.nShow=SW_HIDE;
    if(!ShellExecuteExW(&info))hostStatus_="Microphone component setup could not start or was canceled.";
    else{impl_->setup=info.hProcess;hostStatus_="Installing microphone component...";}
    emit hostChanged();
}
void WindowsController::setupCamera(){
    if(active()||hosting()||impl_->setup)return;
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
            addresses_.push_back(QVariantMap{{"label",nic.humanReadableName()+" â€” "+item.ip().toString()},{"address",item.ip().toString()},{"index",nic.index()}});
    }emit addressesChanged();
}
void WindowsController::connectTo(QString server,QString local,bool input,unsigned scale,unsigned interfaceIndex,unsigned port,QString code){
    if(active()||cameraScanRunning()||impl_->setup)return;
    const bool camera=clientMode_!="display";
    const QHostAddress address(server.trimmed());
    if((!camera&&!resources_.canConnect())||address.protocol()!=QAbstractSocket::IPv4Protocol||address.isNull()||address.isMulticast()||address.isLoopback()||address.toIPv4Address()==0xffffffffu||!port||port>65535||scale>2){status_="Select a resource and enter a valid server IPv4 address and port.";emit changed();return;}
    if(!availability().isEmpty()){status_=availability();emit changed();return;}
    if(camera&&(selectedCamera_.isEmpty()||std::none_of(cameras_.begin(),cameras_.end(),[&](const QVariant& v){return v.toMap()["id"].toString()==selectedCamera_&&v.toMap().value("available",true).toBool();}))){status_="Refresh devices and select a local source.";emit changed();return;}
    if(!code.isEmpty()&&(code.size()!=6||std::any_of(code.begin(),code.end(),[](QChar c){return c<'0'||c>'9';}))){status_="Access code must contain six digits.";emit changed();return;}
    bool valid=interfaceIndex==0&&local=="0.0.0.0";
    for(const auto& item:addresses_){auto entry=item.toMap();if(entry["address"].toString()==local&&entry["index"].toUInt()==interfaceIndex)valid=true;}
    if(!valid){status_="Refresh and select a connected local address.";emit changed();return;}
    try{
        impl_->clientFailed=false;impl_->clientPending.clear();microphoneMuted_=false;
        if(camera){
            std::vector<std::wstring> args{clientMode_=="usb"?L"send-usb":clientMode_=="microphone"?L"send-microphone":L"send-camera",L"--bind",local=="0.0.0.0"?L"auto":local.toStdWString(),L"--peer",address.toString().toStdWString(),L"--port",std::to_wstring(port),L"--parent-pid",std::to_wstring(GetCurrentProcessId()),L"--device-stdin"};
            if(!code.isEmpty())args.push_back(L"--access-stdin");
            if(clientMode_=="usb"){auto at=std::find(args.begin(),args.end(),L"--parent-pid");if(at!=args.end())args.erase(at,at+2);impl_->client.start_elevated(L"ExtraPane.Usb.exe",args);}
            else impl_->client.start((extrapane::windows_bridge::directory()/(clientMode_=="microphone"?L"ExtraPane.Microphone.exe":L"ExtraPane.Resources.exe")).wstring(),args,true);
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
        resources_.setLocked(true);status_=camera?"Connecting source; capture starts after acceptance...":"Connecting in the native display windowâ€¦";
    }catch(const std::exception& e){impl_->client.stop();resources_.setLocked(false);status_=QString::fromUtf8(e.what());}
    emit changed();
}
void WindowsController::disconnectFromServer(){if(clientMode_=="usb"&&active()){try{impl_->client.send_control("Q\n");status_="Stopping USB transfer. If it was not ejected by the receiver, source recovery is required.";}catch(const std::exception& e){status_=QString::fromUtf8(e.what());}emit changed();return;}impl_->client.stop();resources_.setLocked(false);status_="Disconnected";emit changed();}
void WindowsController::startHost(QString address,unsigned port,bool requireCode){
    if(hosting()||impl_->setup)return;
    if(!hostAvailability().isEmpty()){hostStatus_=hostAvailability();emit hostChanged();return;}
    if(!port||port>65535){hostStatus_="Enter a valid TCP port.";emit hostChanged();return;}
    bool local=false;refreshAddresses();
    for(const auto& item:addresses_)if(item.toMap()["address"].toString()==address)local=true;
    if(!local){hostStatus_="Select a connected local address.";emit hostChanged();return;}
    try{
        impl_->hostFailed=false;impl_->hostPending.clear();impl_->cameraProblem.clear();
        if(hostMode_!="display"){
            std::vector<std::wstring> args{hostMode_=="usb"?L"serve-usb":hostMode_=="microphone"?L"serve-microphone":L"serve-camera",L"--bind",address=="0.0.0.0"?L"auto":address.toStdWString(),L"--port",std::to_wstring(port),L"--parent-pid",std::to_wstring(GetCurrentProcessId())};
            if(requireCode)args.push_back(L"--require-code");
            if(hostMode_=="usb"){auto at=std::find(args.begin(),args.end(),L"--parent-pid");if(at!=args.end())args.erase(at,at+2);impl_->host.start_elevated(L"ExtraPane.Usb.exe",args);}
            else impl_->host.start((extrapane::windows_bridge::directory()/(hostMode_=="microphone"?L"ExtraPane.Microphone.exe":L"ExtraPane.Resources.exe")).wstring(),args,true);
        }else impl_->host.start_driver(true,false,port,requireCode,address=="0.0.0.0"?L"auto":address.toStdWString());
        hostStatus_="Starting server...";
    }
    catch(const std::exception& e){hostStatus_=QString::fromUtf8(e.what());}
    emit hostChanged();
}
void WindowsController::stopHost(){if(hostMode_=="usb"&&hosting()){try{impl_->host.send_control("Q\n");hostStatus_="Requesting safe eject before stopping...";}catch(const std::exception& e){hostStatus_=QString::fromUtf8(e.what());}emit hostChanged();return;}impl_->host.stop();impl_->codeExpires=0;code_.clear();hostStatus_="Server stopped";emit hostChanged();}
void WindowsController::generateAccessCode(){
    if(!hosting())return;
    try{impl_->host.send_control(hostMode_!="display"?"G\n":"G");}catch(const std::exception& e){hostStatus_=QString::fromUtf8(e.what());}emit hostChanged();
}
void WindowsController::poll(){
    if(impl_->setup&&WaitForSingleObject(impl_->setup,0)==WAIT_OBJECT_0){
        DWORD result=1;GetExitCodeProcess(impl_->setup,&result);CloseHandle(impl_->setup);impl_->setup=nullptr;
        hostStatus_=result==0?"Component setup completed.":result==3010?"Component setup completed; restart Windows before using it.":"Component setup failed (exit "+QString::number(result)+").";if(impl_->recovering){recoveryStatus_=result==0?"USB storage restored. Refresh to check remaining records.":"Recovery refused or failed. No forced recovery is available; review the record and USB connection state.";impl_->recovering=false;recoveries_.clear();emit recoveryChanged();}
        status_=hostStatus_;emit changed();emit hostChanged();
    }
    auto drain=[&](Child& child,QString& pending,bool host){
        if(!child.process)return;
        pending+=QString::fromUtf8(child.drain());
        while(pending.contains('\n')){
            auto at=pending.indexOf('\n');auto line=pending.left(at).trimmed();pending.remove(0,at+1);
            if(line.startsWith("ACCESS_CODE ")){
                auto digits=line.mid(12);
                if(host&&digits.size()==6&&std::all_of(digits.begin(),digits.end(),[](QChar c){return c>='0'&&c<='9';})){
                    code_=digits+" â€” one use, expires in five minutes";impl_->codeExpires=GetTickCount64()+300000;emit hostChanged();
                }digits.fill(QChar(0));line.fill(QChar(0));continue;
            }
            if(line.startsWith("ACCESS_STATUS ")){if(host){code_="Code "+line.mid(14)+". Generate a new code.";impl_->codeExpires=0;emit hostChanged();}continue;}
            if(line.startsWith("error:")){(host?impl_->hostFailed:impl_->clientFailed)=true;(host?hostStatus_:status_)=(!host&&clientMode_=="usb")?"USB sharing failed: "+line.mid(6).trimmed()+" Check USB recovery before reconnecting.":line.mid(6).trimmed();}
            else if(line.startsWith("VIDEO_RECEIVING")||line.startsWith("VIDEO_STARTED")){(host?hostStatus_:status_)="Connected";}
            else if(line.startsWith("LISTENING")||line.startsWith("HOST_LISTENING")){if(host)hostStatus_="Listening";}
            else if(line.startsWith("USB_SESSION_ERROR")){(host?hostStatus_:status_)="USB session ended; server continues listening. "+line.mid(18);}
            else if(line.startsWith("USB_STARTED")){(host?hostStatus_:status_)=host?"USB storage connected. Close files and eject before disconnecting.":"USB storage is shared exclusively. Eject it on the receiving PC to return it safely.";}
            else if(line.startsWith("USB_EJECT_BUSY")){(host?hostStatus_:status_)="USB is busy; close files and try eject again. "+line.mid(14);}
            else if(line.startsWith("USB_RECOVERY_REQUIRED")){(host?impl_->hostFailed:impl_->clientFailed)=true;(host?hostStatus_:status_)="USB recovery required on the source PC. "+line.mid(21);}
            else if(line.startsWith("USB_EJECTED")){(host?hostStatus_:status_)="USB safely ejected. "+line.mid(11);}
            else if(line.startsWith("USB_DISCONNECTED")){(host?hostStatus_:status_)=host?"Listening; USB disconnected.":"USB disconnected.";}
            else if(line.startsWith("MICROPHONE_SENDING")){if(!host)status_="Sharing microphone. Select ExtraPane Microphone in an app on the server.";}
            else if(line.startsWith("MICROPHONE_STARTED")){if(host){impl_->cameraProblem.clear();hostStatus_="Microphone connected. Select ExtraPane Microphone in your recording app.";}else status_="Sharing microphone. Select ExtraPane Microphone in an app on the server.";}
            else if(line.startsWith("MICROPHONE_SESSION_ERROR")){if(host){impl_->cameraProblem=line.mid(25).trimmed();hostStatus_="Microphone connection failed; server still listening. "+impl_->cameraProblem;}}
            else if(line.startsWith("MICROPHONE_DISCONNECTED")){if(host)hostStatus_="Listening; microphone disconnected. "+impl_->cameraProblem;}
            else if(line.startsWith("CAMERA_STARTED")){if(!host)status_="Sharing camera. Select ExtraPane Camera in an app on the server.";if(host){impl_->cameraProblem.clear();hostStatus_="Camera connected â€” select ExtraPane Camera in your application.";}}
            else if(line.startsWith("CAMERA_SESSION_ERROR")){if(host){impl_->cameraProblem=line.section(' ',2);hostStatus_="Camera connection failed; server still listening. "+impl_->cameraProblem;}}
            else if(line.startsWith("CAMERA_DISCONNECTED")){if(host)hostStatus_=impl_->cameraProblem.isEmpty()?"Listening â€” camera client disconnected.":"Listening. Last camera error: "+impl_->cameraProblem;}
            if(host)emit hostChanged();else emit changed();
        }
        if(pending.size()>16384)pending.clear();
        if(WaitForSingleObject(child.process,0)==WAIT_OBJECT_0){child.stop();if(host){impl_->codeExpires=0;code_.clear();if(!impl_->hostFailed)hostStatus_="Server stopped";emit hostChanged();}else{resources_.setLocked(false);if(!impl_->clientFailed&&clientMode_!="usb")status_="Disconnected";emit changed();}}
    };
    drain(impl_->host,impl_->hostPending,true);drain(impl_->client,impl_->clientPending,false);
    if(impl_->codeExpires&&GetTickCount64()>=impl_->codeExpires){impl_->codeExpires=0;code_="Code expired. Generate a new code.";emit hostChanged();}
}
void WindowsController::shutdown(){if(impl_->recoveryScan){auto* scan=impl_->recoveryScan;impl_->recoveryScan=nullptr;scan->disconnect(this);scan->kill();scan->waitForFinished(1000);scan->deleteLater();}if(impl_->inventory){auto* process=impl_->inventory;impl_->inventory=nullptr;process->disconnect(this);process->kill();process->waitForFinished(1000);process->deleteLater();}impl_->host.stop();impl_->client.stop();resources_.setLocked(false);}

void WindowsController::ejectUsb(){
    if(hostMode_!="usb"||!hosting())return;
    try{impl_->host.send_control("E\n");hostStatus_="Requesting safe eject...";}
    catch(const std::exception& e){hostStatus_=QString::fromUtf8(e.what());}emit hostChanged();
}
void WindowsController::setupUsbSource(){
    if(active()||hosting()||impl_->setup)return;
    const auto exe=extrapane::windows_bridge::directory()/L"ExtraPane.UsbHostSetup.exe";
    if(!std::filesystem::is_regular_file(exe)){status_="USB source setup is missing from this build.";emit changed();return;}
    SHELLEXECUTEINFOW info{sizeof(info)};info.fMask=SEE_MASK_NOCLOSEPROCESS|SEE_MASK_NOASYNC;
    info.lpVerb=L"runas";info.lpFile=exe.c_str();info.lpParameters=L"--install";info.nShow=SW_HIDE;
    if(!ShellExecuteExW(&info))status_="USB source setup could not start or was canceled.";
    else{impl_->setup=info.hProcess;status_="Installing USB source component...";}
    emit changed();emit hostChanged();
}

bool WindowsController::recoveryBusy()const{return impl_->recoveryScan||impl_->setup;}
void WindowsController::refreshUsbRecovery(){
    if(active()||hosting()||recoveryBusy())return;
    auto* process=new QProcess(this);impl_->recoveryScan=process;impl_->recoveryOutput.clear();impl_->recoveryInvalid=false;
    recoveries_.clear();recoveryStatus_="Reading recovery records...";emit recoveryChanged();
    process->setProcessChannelMode(QProcess::MergedChannels);
    auto finish=[this,process](bool ok){
        if(impl_->recoveryScan!=process)return;
        impl_->recoveryOutput+=process->readAll();QJsonParseError error;
        const auto document=QJsonDocument::fromJson(impl_->recoveryOutput,&error);
        bool valid=ok&&!impl_->recoveryInvalid&&impl_->recoveryOutput.size()<=1024*1024&&error.error==QJsonParseError::NoError&&document.isObject()&&document.object()["journals"].isArray();
        QVariantList records;QSet<QString> ids;
        if(valid)for(const auto value:document.object()["journals"].toArray()){
            const auto item=value.toObject();const auto id=item["id"].toString();
            if(!value.isObject()||id.size()!=64||std::any_of(id.begin(),id.end(),[](QChar c){return !((c>='0'&&c<='9')||(c>='a'&&c<='f'));})||ids.contains(id)||records.size()>=128||!item["canAttemptRecovery"].isBool()){valid=false;break;}
            ids.insert(id);records.push_back(QVariantMap{{"id",id},{"label",QString("Disk %1 ? %2 ? %3").arg(item["disk"].toInt(-1)).arg(item["state"].toString(),item["reason"].toString())},{"available",item["canAttemptRecovery"].toBool()}});
        }
        impl_->recoveryScan=nullptr;impl_->recoveryOutput.clear();process->deleteLater();
        recoveries_=valid?records:QVariantList{};
        recoveryStatus_=valid?(records.isEmpty()?"No pending USB recovery records.":"Select a record. Restore rechecks the original disk and requires verified eject and binding release."):"Recovery records could not be read. Check component setup or request administrator assistance; no disk was changed.";
        emit recoveryChanged();
    };
    connect(process,&QProcess::readyRead,this,[this,process]{if(impl_->recoveryScan!=process)return;impl_->recoveryOutput+=process->readAll();if(impl_->recoveryOutput.size()>1024*1024){impl_->recoveryInvalid=true;process->kill();}});
    connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[finish](int result,QProcess::ExitStatus state){finish(result==0&&state==QProcess::NormalExit);});
    connect(process,&QProcess::errorOccurred,this,[finish](QProcess::ProcessError error){if(error==QProcess::FailedToStart)finish(false);});
    QTimer::singleShot(10000,process,[this,process]{if(impl_->recoveryScan==process){impl_->recoveryInvalid=true;process->kill();}});
    process->start(QCoreApplication::applicationDirPath()+"/ExtraPane.UsbRecovery.exe",QStringList{"--list"});
}
void WindowsController::recoverUsb(QString id){
    if(active()||hosting()||recoveryBusy()||id.size()!=64)return;
    if(std::none_of(recoveries_.begin(),recoveries_.end(),[&](const QVariant& v){return v.toMap()["id"].toString()==id&&v.toMap()["available"].toBool();}))return;
    if(std::any_of(id.begin(),id.end(),[](QChar c){return !((c>='0'&&c<='9')||(c>='a'&&c<='f'));}))return;
    const auto exe=extrapane::windows_bridge::directory()/L"ExtraPane.UsbRecovery.exe";
    const auto args=L"--recover "+id.toStdWString();SHELLEXECUTEINFOW info{sizeof(info)};info.fMask=SEE_MASK_NOCLOSEPROCESS|SEE_MASK_NOASYNC;
    info.lpVerb=L"runas";info.lpFile=exe.c_str();info.lpParameters=args.c_str();info.nShow=SW_HIDE;
    if(!ShellExecuteExW(&info))recoveryStatus_="Recovery could not start or administrator permission was canceled.";
    else{impl_->setup=info.hProcess;impl_->recovering=true;recoveryStatus_="Checking and restoring the selected USB disk...";}
    emit recoveryChanged();emit hostChanged();
}
