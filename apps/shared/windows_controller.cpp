#include "windows_child.hpp"
#include "windows_controller.hpp"
#include <ExtraPaneVersion.h>
#include <QCoreApplication>
#include <QFileInfo>
#include <QFile>
#include <QDateTime>
#include <QDir>
#include <QRegularExpression>
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
namespace {
// USB diagnostics only: bounded local history, never admission/code messages.
void usbDiagnostic(QString role,QString event){
    event=event.left(2048);
    event.replace(QRegularExpression("EPCT1:[^\\s]+"),"[redacted credential]");
    event.replace(QRegularExpression("(?i)(access[ _-]?code|password|credential|ticket)[ :=]+.*"),"[redacted sensitive detail]");
    event.replace(QRegularExpression("\\b[0-9]{6}\\b"),"[redacted code]");
    event.replace('\r',' ');event.replace('\n',' ');
#ifdef EXTRAPANE_CONTROLLER_TEST_CHILDREN
    const auto directory=QCoreApplication::applicationDirPath()+"/usb-diagnostics";
#else
    PWSTR local=nullptr;if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&local)))return;
    const auto directory=QString::fromWCharArray(local)+"/ExtraPane/logs";CoTaskMemFree(local);
#endif
    if(!QDir().mkpath(directory))return;
    const auto path=directory+"/usb.log";
    const auto entry=(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)+" "+role+" "+event+"\n").toUtf8();
    constexpr qint64 limit=256*1024;
    if(QFileInfo(path).size()+entry.size()>limit){QFile::remove(path+".1");if(!QFile::rename(path,path+".1"))return;}
    QFile log(path);if(log.open(QIODevice::WriteOnly|QIODevice::Append))log.write(entry);
}
constexpr std::array<const char*,4> resourceNames{"display","camera","microphone","usb"};
int resourceIndex(const QString& key){for(int i=0;i<4;++i)if(key==resourceNames[i])return i;return -1;}
extrapane::ResourceKind resourceKind(int i){return static_cast<extrapane::ResourceKind>(i);}
const wchar_t* helper(int i){return i==0?L"dm-view.exe":i==1?L"ExtraPane.Resources.exe":i==2?L"ExtraPane.Microphone.exe":L"ExtraPane.Usb.exe";}
}
struct WindowsController::Impl {
    struct Client{Child child;QProcess* inventory=nullptr;QByteArray inventoryOutput;bool inventoryFailed=false,failed=false,stopping=false,ticketPending=false,returned=false;QString pending,format,status="Ready";};
    struct Request{QString server,local;unsigned port=0,scale=0;bool input=false;std::array<bool,4> selected{};} request;
    std::array<Client,4> clients;
    QProcess* admission=nullptr;QByteArray admissionOutput,admissionError;bool admissionFailed=false,sessionReady=false,sessionAuthenticated=false,reconciling=false,disconnectPending=false;
    QProcess* recoveryScan=nullptr;QByteArray recoveryOutput;bool recoveryInvalid=false,recovering=false;
    Child host;QString hostPending,cameraProblem;bool hostFailed=false;quint64 codeExpires=0;HANDLE setup=nullptr;
    bool preparing=false,setupAttempted=false;int component=0;QProcess* probe=nullptr;QByteArray probeOutput;
    QString cameraHash;QString hostAddress;unsigned hostPort=0;bool hostRequireCode=false;
    ~Impl(){if(setup)CloseHandle(setup);}
};
WindowsController::WindowsController(QObject* parent,bool rememberAddress):QObject(parent),impl_(std::make_unique<Impl>()),rememberAddress_(rememberAddress){
    usbDiagnostic("app","CONTROLLER_STARTED");
    const auto dir=QCoreApplication::applicationDirPath();
    if(!QFileInfo::exists(dir+"/dm-view.exe"))unavailable_="The native display engine is missing from this installation.";
    if(!QFileInfo::exists(dir+"/ExtraPane.Service.exe"))hostUnavailable_="The server helper is missing from this installation.";
    if(!unavailable_.isEmpty())hostUnavailable_=unavailable_;
    for(int i=0;i<4;++i){const bool present=QFileInfo::exists(dir+"/"+QString::fromWCharArray(helper(i)));
        resources_.setCapability(resourceKind(i),{present,present?"":"The packaged resource engine is missing."});
    }
    connect(&resources_,&ResourceModel::selectionChanged,this,[this]{reconcileResources();emit changed();});
    connect(this,&WindowsController::hostChanged,this,&WindowsController::changed);
    refreshAddresses();auto* timer=new QTimer(this);timer->setInterval(100);
    connect(timer,&QTimer::timeout,this,&WindowsController::poll);timer->start();
}
WindowsController::~WindowsController(){shutdown();}
QString WindowsController::availability()const{return {};}
bool WindowsController::clientReady()const{return !active()&&!cameraSetupRunning()&&QFileInfo::exists(QCoreApplication::applicationDirPath()+"/ExtraPane.Session.exe");}
void WindowsController::selectDevice(QString kind,QString id){const auto i=resourceIndex(kind);if(i<1||impl_->clients[i].child.process||impl_->clients[i].ticketPending||impl_->clients[i].stopping)return;resources_.setSelectedDevice(resourceKind(i),id);emit changed();}
void WindowsController::refreshDevices(QString key){
    const auto i=resourceIndex(key);if(i<1||impl_->clients[i].child.process||impl_->clients[i].ticketPending||impl_->clients[i].stopping||cameraSetupRunning())return;
    auto& state=impl_->clients[i];if(state.inventory)return;
    auto* process=new QProcess(this);state.inventory=process;state.inventoryOutput.clear();state.inventoryFailed=false;
    resources_.setDevices(resourceKind(i),resources_.devices(resourceKind(i)),resources_.selectedDevice(resourceKind(i)),"Listing devices...",true);
    process->setProcessChannelMode(QProcess::MergedChannels);
    auto finish=[this,process,i](bool success){
        auto& state=impl_->clients[i];if(state.inventory!=process)return;state.inventoryOutput+=process->readAll();
        QVariantList found;QSet<QString> ids;bool done=false;bool valid=success&&!state.inventoryFailed&&state.inventoryOutput.size()<=1024*1024;
        const QByteArray prefix=i==1?"CAMERA_DEVICE_V1":i==2?"MICROPHONE_DEVICE_V1":"USB_DEVICE_V1";
        const QByteArray end=i==1?"CAMERA_LIST_DONE":i==2?"MICROPHONE_LIST_DONE":"USB_LIST_DONE";
        if(valid)for(auto line:state.inventoryOutput.split('\n')){
            if(line.endsWith('\r'))line.chop(1);if(line.isEmpty())continue;
            if(line==end){if(done)valid=false;done=true;continue;}
            auto fields=line.split('\t');bool available=true;QString reason;
            if(done||fields.size()!=4||fields[0]!=prefix||found.size()>=128){valid=false;break;}
            if(i==3){available=fields[3]=="available";reason=available?QString{}:QString::fromUtf8(fields[3]);fields={prefix,QByteArray::number(found.size()),fields[2],fields[1]};}
            if(fields[1]!=QByteArray::number(found.size())||fields[2].isEmpty()||fields[2].size()>2048||fields[3].isEmpty()||fields[3].size()>8192||fields[3].size()%2){valid=false;break;}
            const auto id=fields[3];if(std::any_of(id.begin(),id.end(),[](char c){return !((c>='0'&&c<='9')||(c>='a'&&c<='f'));})){valid=false;break;}
            const auto decoded=QByteArray::fromHex(id);const auto value=QString::fromUtf8(decoded);const auto label=QString::fromUtf8(fields[2]);
            if(value.toUtf8()!=decoded||label.toUtf8()!=fields[2]||decoded.contains('\n')||decoded.contains('\r')||decoded.contains('\0')||ids.contains(QString::fromLatin1(id))){valid=false;break;}
            ids.insert(QString::fromLatin1(id));found.push_back(QVariantMap{{"label",available?label:label+" - "+reason},{"id",QString::fromLatin1(id)},{"available",available}});
        }
        state.inventory=nullptr;state.inventoryOutput.clear();process->deleteLater();
        const QString message=valid&&done?(found.isEmpty()?"No devices found. Check privacy settings and reopen this list to scan again.":i==1?"Choose a camera; its supported resolution and frame rate are selected automatically.":i==2?"Choose a microphone; supported sources use 48 kHz mono/stereo.":"Choose removable storage. Uncheck USB to safely return it to this PC."):"Device listing failed or timed out. Check privacy settings; for USB, run the installer with USB storage sharing enabled.";
        resources_.setDevices(resourceKind(i),valid&&done?found:QVariantList{},resources_.selectedDevice(resourceKind(i)),message,false);emit changed();
    };
    connect(process,&QProcess::readyRead,this,[this,process,i]{auto& state=impl_->clients[i];if(state.inventory!=process)return;state.inventoryOutput+=process->readAll();if(state.inventoryOutput.size()>1024*1024){state.inventoryFailed=true;state.inventoryOutput.clear();process->kill();}});
    connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[finish](int code,QProcess::ExitStatus state){finish(code==0&&state==QProcess::NormalExit);});
    connect(process,&QProcess::errorOccurred,this,[finish](QProcess::ProcessError error){if(error==QProcess::FailedToStart)finish(false);});
    QTimer::singleShot(10000,process,[this,process,i]{auto& state=impl_->clients[i];if(state.inventory==process){state.inventoryFailed=true;process->kill();}});
    process->start(QCoreApplication::applicationDirPath()+"/"+QString::fromWCharArray(helper(i)),QStringList{i==1?"list-cameras":i==2?"list-microphones":"list-storage"});
}
QString WindowsController::version()const{return QString::fromUtf8(EXTRAPANE_VERSION);}
bool WindowsController::hosting()const{return impl_->host.process!=nullptr;}
bool WindowsController::active()const{if(impl_->admission)return true;for(const auto& state:impl_->clients)if(state.child.process)return true;return false;}
QString WindowsController::resourceAvailability(const QString& kind)const{
    if(kind=="usb"&&!QFileInfo::exists(QCoreApplication::applicationDirPath()+"/ExtraPane.Usb.exe"))return "The USB connection engine is missing from this installation.";
    if(kind=="microphone"||kind=="usb"){
        const auto dir=QCoreApplication::applicationDirPath();
        if(kind=="microphone"&&(!QFileInfo::exists(dir+"/ExtraPane.Microphone.exe")||!QFileInfo::exists(dir+"/ExtraPane.MicrophoneEndpoint.exe")))return "The microphone receiving engine is missing from this installation.";
        PWSTR location=nullptr;
        if(FAILED(SHGetKnownFolderPath(FOLDERID_ProgramFilesX64,0,nullptr,&location)))return "The microphone component location is unavailable.";
        const auto root=QString::fromWCharArray(location)+"/ExtraPane/Components/Microphone/";CoTaskMemFree(location);
        for(const auto* name:{"usbip.exe","libusbip.dll","resources.dll"})if(!QFileInfo::exists(root+name))return "Start server prepares the microphone and USB receiving component.";
        return {}; // The endpoint verifies the protected backend before attaching.
    }
    if(kind=="display"){
        wchar_t windows[MAX_PATH]{};if(!GetWindowsDirectoryW(windows,MAX_PATH))return "Cannot locate the Windows driver store.";
        const QDir store(QString::fromWCharArray(windows)+"/System32/DriverStore/FileRepository");
        for(const auto& name:store.entryList({"dmindirectdisplay.inf_*"},QDir::Dirs|QDir::NoDotAndDotDot)){
            const auto path=store.filePath(name)+"/";
            if(QFileInfo::exists(path+"DMIndirectDisplay.inf")&&QFileInfo::exists(path+"DMIndirectDisplay.dll")&&QFileInfo::exists(path+"DMIndirectDisplay.cat"))return {};
        }
        return "The virtual-display driver is not staged. Install Server + Client with its display driver and explicit certificate consent, then retry Start server.";
    }
    if(kind!="camera")return hostUnavailable_;
    if(!QFileInfo::exists(QCoreApplication::applicationDirPath()+"/ExtraPane.Resources.exe"))return "The camera connection engine is missing from this build.";
    wchar_t installed[32768]{};DWORD bytes=sizeof(installed);
    const auto read=RegGetValueW(HKEY_LOCAL_MACHINE,L"Software\\Classes\\CLSID\\{5A053AD3-02CD-4281-8367-CEF990182004}\\InprocServer32",nullptr,RRF_RT_REG_SZ|RRF_SUBKEY_WOW6464KEY,nullptr,installed,&bytes);
    const auto path=read==ERROR_SUCCESS?QString::fromWCharArray(installed):QString{};
    wchar_t owner[128]{};DWORD ownerBytes=sizeof(owner);
    const auto ownerRead=RegGetValueW(HKEY_LOCAL_MACHINE,L"Software\\Classes\\CLSID\\{5A053AD3-02CD-4281-8367-CEF990182004}",L"ExtraPaneOwner",RRF_RT_REG_SZ|RRF_SUBKEY_WOW6464KEY,nullptr,owner,&ownerBytes);
    if(ownerRead!=ERROR_SUCCESS||QString::fromWCharArray(owner)!="ExtraPane.CameraSource/v1"||!QFileInfo::exists(path))return "Start server prepares the camera receiving component.";
    return {};
}
QString WindowsController::hostAvailability()const{return hostUnavailable_;}
QVariantList WindowsController::hostComponents()const{
    QVariantList result;
    for(const auto& item:std::initializer_list<std::pair<QString,QString>>{{"display","Display"},{"camera","Camera"},{"microphone","Microphone"},{"usb","USB storage"}}){
        const auto reason=resourceAvailability(item.first);
        result.push_back(QVariantMap{{"label",item.second},{"available",reason.isEmpty()},{"detail",reason.isEmpty()?(item.first=="display"?"Display driver package present; loading is checked when a client requests its monitor.":"Component present; availability is checked when a client connects."):reason}});
    }
    return result;
}
QString WindowsController::hostDescription()const{
    return "Clients choose which resources to use. Display, camera, microphone and USB connections share this server address and port. Start server checks and prepares the required receiving components. Windows may ask for administrator permission.";
}
bool WindowsController::cameraSetupRunning()const{return impl_->setup!=nullptr||impl_->preparing;}
void WindowsController::setMicrophoneMuted(bool muted){
    auto& state=impl_->clients[2];if(!state.child.process||state.stopping)return;
    try{state.child.send_control(muted?"M\n":"U\n");microphoneMuted_=muted;}
    catch(const std::exception& e){state.status=QString::fromUtf8(e.what());}
    resources_.setSessionState(resourceKind(2),true,state.status,microphoneMuted_);emit changed();
}
void WindowsController::setCameraMirrored(bool mirrored){
    auto& state=impl_->clients[1];
    if(state.stopping){emit changed();return;}
    if(cameraMirrored_==mirrored)return;
    try{
        if(state.child.process)state.child.send_control(mirrored?"MIRROR 1\n":"MIRROR 0\n");
        cameraMirrored_=mirrored;
    }catch(const std::exception& e){
        state.status=QString::fromUtf8(e.what());
        resources_.setSessionState(resourceKind(1),state.child.process!=nullptr,state.status);
    }
    emit changed();
}
QString WindowsController::savedAddress()const{return QSettings("HKEY_CURRENT_USER\\Software\\DM\\Receiver",QSettings::NativeFormat).value("Peer").toString();}
void WindowsController::refreshAddresses(){
    addresses_={QVariantMap{{"label","Automatic route"},{"address","0.0.0.0"},{"index",0}}};
    for(const auto& nic:QNetworkInterface::allInterfaces()){
        auto flags=nic.flags();if(!flags.testFlag(QNetworkInterface::IsUp)||!flags.testFlag(QNetworkInterface::IsRunning)||flags.testFlag(QNetworkInterface::IsLoopBack))continue;
        if(nic.type()!=QNetworkInterface::Ethernet&&nic.type()!=QNetworkInterface::Wifi)continue;
        for(const auto& item:nic.addressEntries())if(item.ip().protocol()==QAbstractSocket::IPv4Protocol&&!item.ip().isLoopback())
            addresses_.push_back(QVariantMap{{"label",nic.humanReadableName()+" - "+item.ip().toString()},{"address",item.ip().toString()},{"index",nic.index()}});
    }emit addressesChanged();
}
void WindowsController::connectTo(QString server,QString local,bool input,unsigned scale,unsigned interfaceIndex,unsigned port,QString code){
    if(!clientReady())return;
    const QHostAddress address(server.trimmed());
    if(address.protocol()!=QAbstractSocket::IPv4Protocol||address.isNull()||address.isMulticast()||address.isLoopback()||address.toIPv4Address()==0xffffffffu||!port||port>65535||scale>2){status_="Enter a valid server IPv4 address and port.";emit changed();return;}
    if(!code.isEmpty()&&(code.size()!=6||std::any_of(code.begin(),code.end(),[](QChar c){return c<'0'||c>'9';}))){status_="Access code must contain six digits.";emit changed();return;}
    bool valid=interfaceIndex==0&&local=="0.0.0.0";
    for(const auto& item:addresses_){const auto entry=item.toMap();if(entry["address"].toString()==local&&entry["index"].toUInt()==interfaceIndex)valid=true;}
    if(!valid){status_="Refresh and select a connected local address.";emit changed();return;}
    impl_->request={address.toString(),local,port,scale,input,{}};
    microphoneMuted_=false;
    if(rememberAddress_)QSettings("HKEY_CURRENT_USER\\Software\\DM\\Receiver",QSettings::NativeFormat).setValue("Peer",address.toString());
    auto* process=new QProcess(this);impl_->admission=process;impl_->admissionOutput.clear();impl_->admissionError.clear();impl_->admissionFailed=false;impl_->sessionReady=false;impl_->sessionAuthenticated=!code.isEmpty();
    status_="Connecting to server...";emit changed();
    connect(process,&QProcess::readyReadStandardOutput,this,[this,process]{
        if(impl_->admission!=process)return;
        impl_->admissionOutput+=process->readAllStandardOutput();
        if(impl_->admissionOutput.size()>16384){impl_->admissionFailed=true;impl_->admissionError="Server session response exceeded its limit.";process->kill();return;}
        while(impl_->admissionOutput.contains('\n')){
            const auto at=impl_->admissionOutput.indexOf('\n');auto line=impl_->admissionOutput.left(at);impl_->admissionOutput.remove(0,at+1);if(line.endsWith('\r'))line.chop(1);
            if(line=="SESSION_READY"&&!impl_->sessionReady){impl_->sessionReady=true;status_="Connected. Check resources to start sharing.";reconcileResources();emit changed();continue;}
            auto fields=line.split('\t');bool valid=impl_->sessionReady&&fields.size()==3&&fields[0]=="SESSION_TICKET_V1";
            const auto i=valid?resourceIndex(QString::fromLatin1(fields[1])):-1;valid=valid&&i>=0&&impl_->clients[i].ticketPending;
            if(valid&&fields[2]=="-"&&impl_->sessionAuthenticated)valid=false;
            if(valid&&fields[2]!="-"){
                const auto ticket=fields[2].split(':');
                auto hex=[](const QByteArray& value,int size){return value.size()==size&&std::all_of(value.begin(),value.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});};
                valid=ticket.size()==4&&ticket[0]=="EPCT1"&&ticket[1]==QByteArray::number(i)&&hex(ticket[2],32)&&hex(ticket[3],64);
            }
            if(!valid){line.fill('\0');impl_->admissionFailed=true;impl_->admissionError="Invalid server session response. Use matching builds on both PCs.";process->kill();return;}
            auto& state=impl_->clients[i];state.ticketPending=false;
            if(!impl_->disconnectPending&&resources_.selected(resourceKind(i))&&!state.child.process&&!state.stopping){
                impl_->request.selected.fill(false);impl_->request.selected[i]=true;
                std::array<std::string,4> credentials;if(fields[2]!="-")credentials[i]=fields[2].toStdString();
                startClients(std::move(credentials));
            }else resources_.setSessionState(resourceKind(i),state.child.process!=nullptr,state.child.process?state.status:"Disconnected");
            fields[2].fill('\0');line.fill('\0');
        }
    });
    connect(process,&QProcess::readyReadStandardError,this,[this,process]{if(impl_->admission!=process)return;impl_->admissionError+=process->readAllStandardError();if(impl_->admissionError.size()>4096)impl_->admissionError=impl_->admissionError.right(4096);});
    auto finish=[this,process]{
        if(impl_->admission!=process)return;
        impl_->admissionError+=process->readAllStandardError();
        const auto detail=QString::fromUtf8(impl_->admissionError).trimmed().left(1000);
        impl_->admission=nullptr;impl_->sessionReady=false;impl_->admissionOutput.fill('\0');impl_->admissionOutput.clear();process->deleteLater();
        impl_->disconnectPending=false;
        auto& usb=impl_->clients[3];
        if(usb.child.process){
            usb.stopping=true;usb.status="Server session lost; USB is stopping. Recovery may be required.";
            try{usb.child.send_control("X\n");}catch(const std::exception& error){usb.failed=true;usb.status=QString::fromUtf8(error.what());}
        }
        for(int i=0;i<4;++i){impl_->clients[i].ticketPending=false;resources_.select(i,false);stopResource(resourceNames[i]);}
        status_=detail.isEmpty()?"Server connection ended. Connect again to resume sharing.":detail;emit changed();
    };
    connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[finish](int,QProcess::ExitStatus){finish();});
    connect(process,&QProcess::errorOccurred,this,[finish](QProcess::ProcessError error){if(error==QProcess::FailedToStart)finish();});
    QTimer::singleShot(25000,process,[this,process]{if(impl_->admission==process&&!impl_->sessionReady){impl_->admissionError="Server connection timed out.";process->kill();}});
    QStringList args{"--session","--access-stdin","--bind",local=="0.0.0.0"?"auto":local,"--peer",address.toString(),"--port",QString::number(port),"--parent-pid",QString::number(GetCurrentProcessId())};
    process->start(QCoreApplication::applicationDirPath()+"/ExtraPane.Session.exe",args);
    auto bytes=code.toLatin1();bytes+='\n';process->write(bytes);bytes.fill('\0');code.fill(QChar(0));
}
void WindowsController::reconcileResources(){
    if(impl_->reconciling)return;impl_->reconciling=true;
    for(int i=0;i<4;++i){
        auto& state=impl_->clients[i];
        if(!resources_.selected(resourceKind(i))){if(!state.stopping)stopResource(resourceNames[i]);continue;}
        if(impl_->disconnectPending||!impl_->sessionReady||!impl_->admission||state.child.process||state.ticketPending||state.stopping)continue;
        if(i&& (resources_.selectedDevice(resourceKind(i)).isEmpty()||resources_.scanning(resourceKind(i)))){
            state.status="Choose a source device to start sharing.";resources_.setSessionState(resourceKind(i),false,state.status);continue;
        }
        state.ticketPending=true;state.status="Authorizing resource...";resources_.setSessionState(resourceKind(i),true,state.status);
        impl_->admission->write("ADD "+QByteArray::number(i)+"\n");
    }
    impl_->reconciling=false;
}
void WindowsController::startClients(std::array<std::string,4> credentials){
    const auto request=impl_->request;
    for(int i=0;i<4;++i){if(!request.selected[i])continue;auto& state=impl_->clients[i];state.failed=false;state.stopping=false;state.returned=false;state.pending.clear();state.format.clear();
        try{
            std::vector<std::wstring> args;
            if(i==0){
                auto renderer=QSettings("HKEY_CURRENT_USER\\Software\\DM\\Receiver",QSettings::NativeFormat).value("Renderer","d3d11").toString();
                if(renderer!="d3d11"&&renderer!="d3d12"&&renderer!="vulkan"&&renderer!="opengl")renderer="d3d11";
                args={L"receive",L"--lan",L"--quality",request.scale==1?L"75":request.scale==2?L"50":L"native",L"--renderer",renderer.toStdWString()};if(request.input)args.push_back(L"--control");
            }else args={i==1?L"send-camera":i==2?L"send-microphone":L"send-usb",L"--device-stdin"};
            args.insert(args.end(),{L"--bind",request.local=="0.0.0.0"?L"auto":request.local.toStdWString(),L"--peer",request.server.toStdWString(),L"--port",std::to_wstring(request.port)});
            if(!credentials[i].empty())args.push_back(L"--access-stdin");
#ifndef EXTRAPANE_CONTROLLER_TEST_CHILDREN
            if(i==3)state.child.start_elevated(L"ExtraPane.Usb.exe",args);
            else
#endif
            {args.insert(args.end(),{L"--parent-pid",std::to_wstring(GetCurrentProcessId())});state.child.start((extrapane::windows_bridge::directory()/helper(i)).wstring(),args,i!=0);}
            if(i)state.child.send_control(QByteArray::fromHex(resources_.selectedDevice(resourceKind(i)).toLatin1()).toStdString()+"\n");
            if(!credentials[i].empty())state.child.send_control(credentials[i]+"\n");
            if(i==1)state.child.send_control(cameraMirrored_?"MIRROR 1\n":"MIRROR 0\n");
            state.status="Connecting...";
        }catch(const std::exception& e){if(i==3)usbDiagnostic("client","LAUNCH_FAILED "+QString::fromUtf8(e.what()));state.child.stop();state.failed=true;state.status=QString::fromUtf8(e.what());resources_.select(i,false);}
        std::fill(credentials[i].begin(),credentials[i].end(),'\0');resources_.setSessionState(resourceKind(i),state.child.process!=nullptr,state.status,false);
    }
    status_="Connected. Use the checkboxes to start or stop sharing.";emit changed();
}
void WindowsController::cancelAdmission(){
    impl_->disconnectPending=false;impl_->sessionReady=false;for(auto& state:impl_->clients)state.ticketPending=false;
    if(!impl_->admission)return;auto* process=impl_->admission;impl_->admission=nullptr;process->disconnect(this);process->write("Q\n");process->closeWriteChannel();if(!process->waitForFinished(1000)){process->kill();process->waitForFinished(1000);}process->deleteLater();impl_->admissionOutput.fill('\0');impl_->admissionOutput.clear();
}
void WindowsController::stopResource(QString key){
    const auto i=resourceIndex(key);if(i<0)return;auto& state=impl_->clients[i];if(!state.child.process){resources_.setSessionState(resourceKind(i),false,(state.failed||state.returned)?state.status:"Disconnected");return;}if(state.stopping)return;
    try{if(i==3)usbDiagnostic("client","RETURN_REQUESTED");if(i==0){state.child.stop();state.status="Disconnected";}else{state.child.send_control("Q\n");state.stopping=true;state.status=i==3?"Returning USB to this PC; waiting for the server to safely eject it...":"Stopping...";}}
    catch(const std::exception& e){state.status=QString::fromUtf8(e.what());}
    resources_.setSessionState(resourceKind(i),state.child.process!=nullptr,state.status,i==2&&microphoneMuted_);emit changed();
}
void WindowsController::disconnectFromServer(){
    impl_->disconnectPending=true;
    for(int i=0;i<4;++i){resources_.select(i,false);stopResource(resourceNames[i]);}
    if(!impl_->clients[3].child.process)cancelAdmission();
    status_=impl_->disconnectPending?"Returning USB before disconnecting...":active()?"Stopping resources...":"Disconnected";emit changed();
}
void WindowsController::startHost(QString address,unsigned port,bool requireCode){
    if(hosting()||cameraSetupRunning())return;
    if(!hostAvailability().isEmpty()){hostStatus_=hostAvailability();emit hostChanged();return;}
    if(!port||port>65535){hostStatus_="Enter a valid TCP port.";emit hostChanged();return;}
    bool local=false;refreshAddresses();
    for(const auto& item:addresses_)if(item.toMap()["address"].toString()==address)local=true;
    if(!local){hostStatus_="Select a connected local address.";emit hostChanged();return;}
    const auto displayProblem=resourceAvailability("display");
    if(!displayProblem.isEmpty()){hostStatus_="Server not started: "+displayProblem;emit hostChanged();return;}
    impl_->hostAddress=address;impl_->hostPort=port;impl_->hostRequireCode=requireCode;
    impl_->preparing=true;impl_->component=0;impl_->setupAttempted=false;prepareHostComponent();
}
void WindowsController::failHostPreparation(QString reason){
    impl_->preparing=false;hostStatus_="Server not started: "+reason;emit hostChanged();
}
void WindowsController::prepareHostComponent(){
    if(!impl_->preparing)return;
    if(impl_->component>=3){launchHost();return;}
    const auto label=impl_->component<2?QString("Camera"):QString("Microphone / USB receiving");
    const auto helperName=impl_->component<2?QString("ExtraPane.CameraSetup.exe"):QString("ExtraPane.UsbSetup.exe");
    const auto exe=QCoreApplication::applicationDirPath()+"/"+helperName;
    if(!QFileInfo::exists(exe)){failHostPreparation(label+" setup is missing from this package. Install the complete Server + Client package.");return;}
    auto* process=new QProcess(this);impl_->probe=process;impl_->probeOutput.clear();
    hostStatus_="Checking "+label.toLower()+" component...";emit hostChanged();
    process->setProcessChannelMode(QProcess::MergedChannels);
    auto finish=[this,process,label,exe](bool success){
        if(impl_->probe!=process)return;impl_->probeOutput+=process->readAll();
        auto detail=QString::fromUtf8(impl_->probeOutput).trimmed().left(1000);
        impl_->probe=nullptr;impl_->probeOutput.clear();process->deleteLater();
        if(!impl_->preparing)return;
        const auto digest=QRegularExpression("(?:^|\\n)sha256=([0-9a-fA-F]{64})(?:\\r?\\n|$)").match(detail).captured(1).toLower();
        if(impl_->component==0){
            if(!success||digest.isEmpty()){failHostPreparation("The bundled camera component failed its integrity check. "+detail);return;}
            impl_->cameraHash=digest;
        }else if(impl_->component==1)success=success&&!digest.isEmpty()&&digest==impl_->cameraHash;
        if(success){++impl_->component;impl_->setupAttempted=false;prepareHostComponent();return;}
        if(impl_->setupAttempted){failHostPreparation(label+" is not ready after setup. "+detail);return;}
        impl_->setupAttempted=true;
        const auto path=exe.toStdWString();SHELLEXECUTEINFOW info{sizeof(info)};
        info.fMask=SEE_MASK_NOCLOSEPROCESS|SEE_MASK_NOASYNC;info.lpVerb=L"runas";
        info.lpFile=path.c_str();info.lpParameters=L"--install";info.nShow=SW_HIDE;
        if(!ShellExecuteExW(&info)){failHostPreparation(label+" setup was canceled or could not start (Windows error "+QString::number(GetLastError())+").");return;}
        impl_->setup=info.hProcess;hostStatus_="Preparing "+label.toLower()+" component...";emit hostChanged();
    };
    connect(process,&QProcess::readyRead,this,[this,process]{if(impl_->probe!=process)return;impl_->probeOutput+=process->readAll();if(impl_->probeOutput.size()>16384)process->kill();});
    connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[finish](int code,QProcess::ExitStatus state){finish(code==0&&state==QProcess::NormalExit);});
    connect(process,&QProcess::errorOccurred,this,[finish](QProcess::ProcessError error){if(error==QProcess::FailedToStart)finish(false);});
    QTimer::singleShot(15000,process,[this,process]{if(impl_->probe==process)process->kill();});
    process->start(exe,QStringList{impl_->component==0?"--check-package":"--status"});
}
void WindowsController::launchHost(){
    impl_->preparing=false;
    try{
        impl_->hostFailed=false;impl_->hostPending.clear();impl_->cameraProblem.clear();
        impl_->host.start_driver(true,false,impl_->hostPort,impl_->hostRequireCode,impl_->hostAddress=="0.0.0.0"?L"auto":impl_->hostAddress.toStdWString());
        hostStatus_="Starting server...";
    }
    catch(const std::exception& e){hostStatus_=QString::fromUtf8(e.what());}
    emit hostChanged();
}
void WindowsController::stopHost(){
    if(!hosting())return;
    try{impl_->host.send_control("Q");hostStatus_="Stopping server; waiting for active USB storage to eject safely...";}
    catch(const std::exception& e){hostStatus_=QString::fromUtf8(e.what());}
    emit hostChanged();
}
void WindowsController::generateAccessCode(){
    if(!hosting())return;
    try{impl_->host.send_control("G");}catch(const std::exception& e){hostStatus_=QString::fromUtf8(e.what());}emit hostChanged();
}
void WindowsController::poll(){
    if(impl_->setup&&WaitForSingleObject(impl_->setup,0)==WAIT_OBJECT_0){
        DWORD result=1;GetExitCodeProcess(impl_->setup,&result);CloseHandle(impl_->setup);impl_->setup=nullptr;
        if(impl_->preparing){
            if(result==0)prepareHostComponent();
            else failHostPreparation(result==3010?"Restart Windows to finish component setup, then click Start server.":"Required component setup failed (exit "+QString::number(result)+"). Retry Start server to repair it.");
        }else{
        hostStatus_=result==0?"Component setup completed.":result==3010?"Component setup completed; restart Windows before using it.":"Component setup failed (exit "+QString::number(result)+").";if(impl_->recovering){recoveryStatus_=result==0?"USB storage restored. Refresh to check remaining records.":"Recovery refused or failed. No forced recovery is available; review the record and USB connection state.";impl_->recovering=false;recoveries_.clear();emit recoveryChanged();}
        status_=hostStatus_;emit changed();emit hostChanged();
        }
    }
    auto drain=[&](Child& child,QString& pending,bool host,int index=-1){
        auto& currentStatus=host?hostStatus_:impl_->clients[index].status;
        auto& failed=host?impl_->hostFailed:impl_->clients[index].failed;
        if(!child.process)return;
        pending+=QString::fromUtf8(child.drain());
        while(pending.contains('\n')){
            auto at=pending.indexOf('\n');auto line=pending.left(at).trimmed();pending.remove(0,at+1);
            if(line.startsWith("USB_")||(!host&&index==3&&line.startsWith("error:")))usbDiagnostic(host?"server":"client",line);
            if(line.startsWith("ACCESS_CODE ")){
                auto digits=line.mid(12);
                if(host&&digits.size()==6&&std::all_of(digits.begin(),digits.end(),[](QChar c){return c>='0'&&c<='9';})){
                    code_=digits+" - one use, expires in five minutes";impl_->codeExpires=GetTickCount64()+300000;emit hostChanged();
                }digits.fill(QChar(0));line.fill(QChar(0));continue;
            }
            if(line.startsWith("ACCESS_STATUS ")){if(host){code_="Code "+line.mid(14)+". Generate a new code.";impl_->codeExpires=0;emit hostChanged();}continue;}
            if(line.startsWith("error:")){failed=true;currentStatus=(!host&&index==3)?"USB sharing failed: "+line.mid(6).trimmed()+" Check USB recovery before reconnecting.":line.mid(6).trimmed();}
            else if(line.startsWith("VIDEO_RECEIVING")||line.startsWith("VIDEO_STARTED")){currentStatus="Connected";}
            else if(line.startsWith("LISTENING")||line.startsWith("HOST_LISTENING")){if(host)hostStatus_="Listening";}
            else if(line.startsWith("SERVER_STOP_BLOCKED")){if(host)hostStatus_=line.mid(19).trimmed();}
            else if(line.startsWith("SERVER_RESOURCE_UNAVAILABLE")){if(host)hostStatus_="Listening; "+line.mid(27).trimmed();}
            else if(line.startsWith("SERVER_STOPPING")){if(host)hostStatus_="Stopping server...";}
            else if(line.startsWith("SERVER_SESSION_ERROR")){if(host)hostStatus_="Listening; a resource connection ended: "+line.mid(21).trimmed();}
            else if(line.startsWith("USB_SESSION_ERROR")){currentStatus="USB session ended; server continues listening. "+line.mid(18);}
            else if(line.startsWith("USB_WAITING")){currentStatus="Waiting for USB storage to appear on the server. "+line.mid(11).trimmed();}
            else if(line.startsWith("USB_NOT_READY")){currentStatus="USB is not available on the server. "+line.mid(13).trimmed();}
            else if(line.startsWith("USB_RETURN_FAILED")){
                currentStatus="USB could not be returned safely. "+line.mid(17).trimmed()+" The transfer remains reserved; this does not mean the disk is usable on the server.";
                if(!host&&index==3&&impl_->sessionReady){impl_->clients[3].stopping=false;impl_->disconnectPending=false;resources_.select(3,true);status_="USB return failed. See the USB details; disconnect was cancelled.";}
            }
            else if(line.startsWith("USB_STARTED")){currentStatus=host?"USB storage connected. Close files and eject before disconnecting.":"USB storage is shared exclusively. Uncheck USB to return it safely to this PC.";}
            else if(line.startsWith("USB_RETURNING")){currentStatus="Returning USB to this PC; waiting for safe eject...";}
            else if(line.startsWith("USB_RETURNED")){currentStatus="USB returned to this PC.";if(!host&&index==3)impl_->clients[3].returned=true;}
            else if(line.startsWith("USB_STOP_BLOCKED")){
                currentStatus="USB is still shared: close its files on the server, then uncheck USB to retry. "+line.mid(16).trimmed();
                if(!host&&index==3&&impl_->sessionReady){const bool all=impl_->disconnectPending;impl_->clients[3].stopping=false;impl_->disconnectPending=false;resources_.select(3,true);status_=all?"Disconnect cancelled because USB is busy. Other resources have stopped.":"USB is busy and remains shared. Close its files on the server and retry.";}
            }
            else if(line.startsWith("USB_EJECT_BUSY")){currentStatus="USB is busy; close files and try eject again. "+line.mid(14);}
            else if(line.startsWith("USB_RECOVERY_REQUIRED")){failed=true;currentStatus="USB recovery required on the source PC. "+line.mid(21);}
            else if(line.startsWith("USB_EJECTED")){currentStatus="USB safely ejected. "+line.mid(11);}
            else if(line.startsWith("USB_DISCONNECTED")){currentStatus=host?"Listening; USB disconnected.":"USB disconnected.";}
            else if(line.startsWith("MICROPHONE_SENDING")){if(!host)currentStatus="Sharing microphone. Select ExtraPane Microphone in an app on the server.";}
            else if(line.startsWith("MICROPHONE_STARTED")){if(host){impl_->cameraProblem.clear();hostStatus_="Microphone connected. Select ExtraPane Microphone in your recording app.";}else currentStatus="Sharing microphone. Select ExtraPane Microphone in an app on the server.";}
            else if(line.startsWith("MICROPHONE_SESSION_ERROR")){if(host){impl_->cameraProblem=line.mid(25).trimmed();hostStatus_="Microphone connection failed; server still listening. "+impl_->cameraProblem;}}
            else if(line.startsWith("MICROPHONE_DISCONNECTED")){if(host)hostStatus_="Listening; microphone disconnected. "+impl_->cameraProblem;}
            else if(line.startsWith("CAMERA_FORMAT ")&&!host&&index==1){impl_->clients[index].format=line.mid(14).left(100);currentStatus="Camera: "+impl_->clients[index].format;}
            else if(line.startsWith("CAMERA_STARTED")){if(!host)currentStatus="Sharing camera "+impl_->clients[index].format+". Select ExtraPane Camera in an app on the server.";if(host){impl_->cameraProblem.clear();hostStatus_="Camera connected â€” select ExtraPane Camera in your application.";}}
            else if(line.startsWith("CAMERA_SESSION_ERROR")){if(host){impl_->cameraProblem=line.section(' ',2);hostStatus_="Camera connection failed; server still listening. "+impl_->cameraProblem;}}
            else if(line.startsWith("CAMERA_DISCONNECTED")){if(host)hostStatus_=impl_->cameraProblem.isEmpty()?"Listening â€” camera client disconnected.":"Listening. Last camera error: "+impl_->cameraProblem;}
            if(host)emit hostChanged();else{if(line.startsWith("VIDEO_RECEIVING")||line.startsWith("VIDEO_STARTED")||line.startsWith("CAMERA_STARTED")||line.startsWith("MICROPHONE_STARTED")||line.startsWith("USB_STARTED"))status_="Resources are active. Manage each one below.";resources_.setSessionState(resourceKind(index),true,currentStatus,index==2&&microphoneMuted_);emit changed();}
        }
        if(pending.size()>16384)pending.clear();
        if(WaitForSingleObject(child.process,0)==WAIT_OBJECT_0){if(host||index==3){DWORD exit=0;if(GetExitCodeProcess(child.process,&exit))usbDiagnostic(host?"server":"client","HELPER_EXIT "+QString::number(exit));}child.stop();if(host){impl_->codeExpires=0;code_.clear();if(!impl_->hostFailed)hostStatus_="Server stopped";emit hostChanged();}else{if(!failed&&index!=3)currentStatus="Disconnected";auto& state=impl_->clients[index];const bool restart=state.stopping&&resources_.selected(resourceKind(index))&&impl_->sessionReady&&!impl_->disconnectPending;state.stopping=false;resources_.setSessionState(resourceKind(index),false,currentStatus,false);if(!restart)resources_.select(index,false);if(index==3&&state.returned)refreshDevices("usb");if(restart)reconcileResources();status_=impl_->sessionReady?"Connected. Check resources to start sharing.":"All resources have stopped.";emit changed();}}
    };
    drain(impl_->host,impl_->hostPending,true);for(int i=0;i<4;++i)drain(impl_->clients[i].child,impl_->clients[i].pending,false,i);
    if(impl_->disconnectPending&&!impl_->clients[3].child.process){cancelAdmission();status_=active()?"Stopping resources...":"Disconnected";emit changed();}
    if(impl_->codeExpires&&GetTickCount64()>=impl_->codeExpires){impl_->codeExpires=0;code_="Code expired. Generate a new code.";emit hostChanged();}
}
void WindowsController::shutdown(){
    impl_->preparing=false;
    if(impl_->probe){auto* process=impl_->probe;impl_->probe=nullptr;process->disconnect(this);process->kill();process->waitForFinished(1000);process->deleteLater();}
    cancelAdmission();
    if(impl_->recoveryScan){auto* process=impl_->recoveryScan;impl_->recoveryScan=nullptr;process->disconnect(this);process->kill();process->waitForFinished(1000);process->deleteLater();}
    for(auto& state:impl_->clients){if(state.inventory){auto* process=state.inventory;state.inventory=nullptr;process->disconnect(this);process->kill();process->waitForFinished(1000);process->deleteLater();}state.child.stop();}
    impl_->host.stop();resources_.setLocked(false);
}

void WindowsController::ejectUsb(){
    if(!hosting())return;
    try{impl_->host.send_control("E");hostStatus_="Requesting safe eject...";}
    catch(const std::exception& e){hostStatus_=QString::fromUtf8(e.what());}emit hostChanged();
}
bool WindowsController::recoveryBusy()const{return impl_->recoveryScan||cameraSetupRunning();}
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
