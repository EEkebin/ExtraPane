#include "windows_connection_runtime.hpp"
#include "windows_child.hpp"
#include <QCoreApplication>
#include <QProcess>
#include <QTimer>
#include <QRegularExpression>
#include <QSettings>
#include <QSet>
#include <map>
#include <deque>

namespace {
using State=ConnectionModel::State;
int kindIndex(QString kind){return kind=="display"?0:kind=="camera"?1:kind=="microphone"?2:kind=="usb"?3:-1;}
QString executable(QString kind){return kind=="display"?"dm-view.exe":kind=="camera"?"ExtraPane.Resources.exe":kind=="microphone"?"ExtraPane.Microphone.exe":"ExtraPane.Usb.exe";}
QString targetKey(QVariantMap target){return target["address"].toString()+":"+target["port"].toString()+"@"+target["localAddress"].toString()+"#"+target["interfaceIndex"].toString();}
bool codeValid(const QString& code){return code.isEmpty()||QRegularExpression("^[0-9]{6}$").match(code).hasMatch();}
bool ticketValid(const QByteArray& ticket,int kind){return QRegularExpression("^EPCT1:"+QString::number(kind)+":[0-9a-f]{32}:[0-9a-f]{64}$").match(QString::fromLatin1(ticket)).hasMatch();}
}
struct WindowsConnectionRuntime::Impl {
    struct Broker {QProcess* process=nullptr;QVariantMap target;QByteArray output,error;QString inflight;std::deque<QString> queue;bool ready=false,authenticated=false,closing=false,awaitingTicket=false;};
    struct Source {extrapane::windows_bridge::Child child;QString pending,kind,device;bool closing=false,mirror=false;};
    std::map<QString,std::unique_ptr<Source>> sources;
    struct Row {extrapane::windows_bridge::Child child;QString broker,kind,pending,sharedSource,returnedStatus;bool sharedActive=false,ended=false, stopping=false,failed=false,recovery=false,returned=false,input=true,mirror=false,muted=false;};
    std::map<QString,std::unique_ptr<Broker>> brokers;
    std::map<QString,std::unique_ptr<Row>> rows;
    QSet<QString> scanning;
};
WindowsConnectionRuntime::WindowsConnectionRuntime(QObject* parent):QObject(parent),impl_(std::make_unique<Impl>()){
    auto* timer=new QTimer(this);connect(timer,&QTimer::timeout,this,&WindowsConnectionRuntime::poll);timer->start(100);
}
WindowsConnectionRuntime::~WindowsConnectionRuntime(){shutdown();}
QVariantMap WindowsConnectionRuntime::mirrorStates()const{QVariantMap result;for(const auto& [key,s]:impl_->sources)if(s->kind=="camera")result[s->device]=s->mirror;return result;}
bool WindowsConnectionRuntime::active()const{for(const auto& [key,s]:impl_->sources)if(s->child.process)return true;for(const auto& [key,b]:impl_->brokers)if(b->process)return true;for(const auto& [id,r]:impl_->rows)if(r->child.process)return true;return false;}
QVariantList WindowsConnectionRuntime::monitors()const{
    QVariantList result;
    for(DWORD i=0;i<64;++i){DISPLAY_DEVICEW device{};device.cb=sizeof(device);if(!EnumDisplayDevicesW(nullptr,i,&device,0))break;if(!(device.StateFlags&DISPLAY_DEVICE_ATTACHED_TO_DESKTOP))continue;
        DEVMODEW mode{};mode.dmSize=sizeof(mode);if(!EnumDisplaySettingsW(device.DeviceName,ENUM_CURRENT_SETTINGS,&mode))continue;
        result.push_back(QVariantMap{{"id",QString::fromWCharArray(device.DeviceName)},{"label",QString::fromWCharArray(device.DeviceName)+" — "+QString::number(mode.dmPelsWidth)+" × "+QString::number(mode.dmPelsHeight)+" @ "+QString::number(mode.dmDisplayFrequency)+" Hz"},{"width",static_cast<unsigned>(mode.dmPelsWidth)},{"height",static_cast<unsigned>(mode.dmPelsHeight)},{"fps",static_cast<unsigned>(mode.dmDisplayFrequency)}});
    }return result;
}
QString WindowsConnectionRuntime::broker(QVariantMap target,QString code){
    if(!codeValid(code)){status_="Access code must contain six digits.";emit changed();return {};}
    const auto key=targetKey(target);auto found=impl_->brokers.find(key);
    if(found!=impl_->brokers.end()&&found->second->process){if(found->second->closing){status_="That server is disconnecting; wait for USB return.";emit changed();code.fill(QChar(0));return {};}code.fill(QChar(0));return key;}
    auto state=std::make_unique<Impl::Broker>();state->target=target;state->authenticated=!code.isEmpty();state->process=new QProcess(this);auto* process=state->process;impl_->brokers[key]=std::move(state);
    connect(process,&QProcess::readyReadStandardOutput,this,[this,key,process]{
        auto found=impl_->brokers.find(key);if(found==impl_->brokers.end()||found->second->process!=process)return;auto& b=*found->second;b.output+=process->readAllStandardOutput();
        if(b.output.size()>16384){lost(key,"Server control response exceeded its limit.");return;}
        while(b.output.contains('\n')){
            const auto at=b.output.indexOf('\n');auto line=b.output.left(at).trimmed();b.output.remove(0,at+1);
            if(line=="SESSION_READY"&&!b.ready){b.ready=true;status_="Connected. Enable connections to start their resources.";advance(key);emit changed();continue;}
            const auto fields=line.split('\t');const auto row=impl_->rows.find(b.inflight);
            if(!b.ready||!b.awaitingTicket||fields.size()!=3||fields[0]!="SESSION_TICKET_V1"||row==impl_->rows.end()||fields[1]!=row->second->kind.toLatin1()||!((fields[2]=="-"&&!b.authenticated)||ticketValid(fields[2],kindIndex(row->second->kind)))){line.fill('\0');lost(key,"Invalid server admission response; use matching builds.");return;}
            const auto id=b.inflight;b.awaitingTicket=false;
            if(model_.connection(id)["connectionEnabled"].toBool()&&!b.closing)launch(id,fields[2]=="-"?QByteArray{}:fields[2]);
            else{model_.setState(id,State::idle);b.inflight.clear();advance(key);}
            line.fill('\0');
        }
    });
    connect(process,&QProcess::readyReadStandardError,this,[this,key,process]{auto found=impl_->brokers.find(key);if(found!=impl_->brokers.end()&&found->second->process==process){auto& error=found->second->error;error+=process->readAllStandardError();if(error.size()>4096)error=error.right(4096);}});
    auto finished=[this,key,process]{auto found=impl_->brokers.find(key);if(found==impl_->brokers.end()||found->second->process!=process)return;const auto error=QString::fromUtf8(found->second->error).trimmed();lost(key,error.isEmpty()?"Server control connection ended.":error);};
    connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[finished](int,QProcess::ExitStatus){finished();});
    connect(process,&QProcess::errorOccurred,this,[finished](QProcess::ProcessError error){if(error==QProcess::FailedToStart)finished();});
    QTimer::singleShot(25000,process,[this,key,process]{auto f=impl_->brokers.find(key);if(f!=impl_->brokers.end()&&f->second->process==process&&!f->second->ready)lost(key,"Server connection timed out.");});
    QStringList args{"--session","--access-stdin","--bind",target["localAddress"].toString()=="0.0.0.0"?"auto":target["localAddress"].toString(),"--peer",target["address"].toString(),"--port",target["port"].toString(),"--parent-pid",QString::number(GetCurrentProcessId())};
    process->start(QCoreApplication::applicationDirPath()+"/ExtraPane.Session.exe",args);auto secret=code.toLatin1();secret+='\n';process->write(secret);secret.fill('\0');code.fill(QChar(0));emit changed();return key;
}
void WindowsConnectionRuntime::connectShared(QString code){
    // Validate a target without acquiring a resource or inventing a connection.
    ConnectionModel validator;const auto id=validator.addConnection("camera");validator.setEnabled(id,true);validator.setSource(id,"validation");
    if(!validator.setSharedTarget(model_.sharedTarget())||!validator.beginConnection(id)){status_="Enter a valid server IPv4 address, port and local interface.";emit changed();return;}
    broker(model_.sharedTarget(),std::move(code));
}
void WindowsConnectionRuntime::toggle(QString id,bool enabled,QString code,bool input){
    if(model_.connection(id).isEmpty())return;
    model_.setEnabled(id,enabled);if(!enabled){stop(id);return;}
    if(model_.connection(id)["busy"].toBool())return;
    if(!model_.beginConnection(id)){model_.setEnabled(id,false);emit changed();return;}
    auto& row=impl_->rows[id];if(!row)row=std::make_unique<Impl::Row>();
    const auto config=model_.connection(id);row->kind=config["resourceKind"].toString();row->input=input;row->pending.clear();row->returnedStatus.clear();row->failed=false;row->recovery=false;row->returned=false;row->stopping=false;
    row->broker=broker(config["effectiveTarget"].toMap(),std::move(code));if(row->broker.isEmpty()){model_.setEnabled(id,false);model_.setState(id,State::failed,status_);return;}
    if(row->kind=="display")emit displayEvent("client","DISPLAY_ADMISSION_QUEUED row="+id);
    impl_->brokers[row->broker]->queue.push_back(id);advance(row->broker);emit changed();
}
void WindowsConnectionRuntime::advance(QString key){
    auto f=impl_->brokers.find(key);if(f==impl_->brokers.end())return;auto& b=*f->second;
    if(!b.process||!b.ready||!b.inflight.isEmpty()||b.closing)return;
    while(!b.queue.empty()){const auto id=b.queue.front();b.queue.pop_front();if(!model_.connection(id)["connectionEnabled"].toBool()){model_.setState(id,State::idle);continue;}
        b.inflight=id;b.awaitingTicket=true;QTimer::singleShot(30000,this,[this,key,id]{auto f=impl_->brokers.find(key);if(f!=impl_->brokers.end()&&f->second->inflight==id){auto r=impl_->rows.find(id);if(r==impl_->rows.end())return;model_.setEnabled(id,false);stop(id,true);r->second->failed=true;if(r->second->kind=="usb")r->second->recovery=true;model_.setState(id,r->second->kind=="usb"?State::recovery:State::returning,"Resource connection timed out.");if(!r->second->child.process&&!r->second->sharedActive){model_.setState(id,State::failed,"Resource admission timed out.");lost(key,"Resource admission timed out; reconnect this server.");}}});b.process->write("ADD "+QByteArray::number(kindIndex(impl_->rows[id]->kind))+"\n");return;}
}
void WindowsConnectionRuntime::launch(QString id,QByteArray credential){
    auto& row=*impl_->rows.at(id);const auto config=model_.connection(id);const auto target=config["effectiveTarget"].toMap();std::vector<std::wstring> args;
    try{
        if(row.kind!="display"){
            const auto source=config["sourceId"].toString().toLatin1();const auto decoded=QByteArray::fromHex(source);
            if(source.size()>8192||source.size()%2||!QRegularExpression("^[0-9a-f]+$").match(QString::fromLatin1(source)).hasMatch()||decoded.contains('\n')||decoded.contains('\r')||decoded.contains('\0')||QString::fromUtf8(decoded).toUtf8()!=decoded)throw std::runtime_error("Invalid source identifier; reopen the source list.");
        }
        if(row.kind=="camera"||row.kind=="microphone"){
            const auto source=config["sourceId"].toString();const auto sourceKey=row.kind+":"+source;
            auto& shared=impl_->sources[sourceKey];if(!shared)shared=std::make_unique<Impl::Source>();
            if(shared->closing){shared->child.stop();shared->closing=false;}
            if(!shared->child.process){shared->kind=row.kind;shared->device=source;
                shared->child.start((extrapane::windows_bridge::directory()/executable(row.kind).toStdWString()).wstring(),{row.kind=="camera"?L"broker-camera":L"broker-microphone",L"--device-stdin",L"--parent-pid",std::to_wstring(GetCurrentProcessId())},true);
                shared->child.send_control(QByteArray::fromHex(source.toLatin1()).toStdString()+"\n");
            }
            shared->child.send_control("ADD\t"+id.toStdString()+"\t"+(target["localAddress"].toString()=="0.0.0.0"?std::string("auto"):target["localAddress"].toString().toStdString())+"\t"+target["address"].toString().toStdString()+"\t"+target["port"].toString().toStdString()+"\t"+(credential.isEmpty()?std::string("-"):credential.toStdString())+"\n");
            row.sharedSource=sourceKey;row.sharedActive=true;row.ended=false;
            if(row.kind=="microphone"&&row.muted)shared->child.send_control("MUTE\t"+id.toStdString()+"\t1\n");credential.fill('\0');model_.setState(id,State::connecting,"Connecting...");emit changed();return;
        }
        if(row.kind=="display"){
            auto renderer=QSettings("HKEY_CURRENT_USER\\Software\\DM\\Receiver",QSettings::NativeFormat).value("Renderer","d3d11").toString();if(renderer!="d3d11"&&renderer!="d3d12"&&renderer!="vulkan"&&renderer!="opengl")renderer="d3d11";
            args={L"receive",L"--lan",L"--renderer",renderer.toStdWString()};if(row.input)args.push_back(L"--control");const auto mode=config["displayMode"].toMap();
            if(mode["mode"]=="native"&&!mode["monitorId"].toString().isEmpty())args.insert(args.end(),{L"--monitor",mode["monitorId"].toString().toStdWString()});
            emit displayEvent("client","DISPLAY_LAUNCH row="+id+" mode="+mode["mode"].toString()+" width="+mode["width"].toString()+" height="+mode["height"].toString()+" fps="+mode["fps"].toString());
            if(mode["mode"]=="custom")args.insert(args.end(),{L"--width",mode["width"].toString().toStdWString(),L"--height",mode["height"].toString().toStdWString(),L"--fps",mode["fps"].toString().toStdWString()});
        }else args={row.kind=="camera"?L"send-camera":row.kind=="microphone"?L"send-microphone":L"send-usb",L"--device-stdin"};
        args.insert(args.end(),{L"--bind",target["localAddress"].toString()=="0.0.0.0"?L"auto":target["localAddress"].toString().toStdWString(),L"--peer",target["address"].toString().toStdWString(),L"--port",target["port"].toString().toStdWString()});
        if(!credential.isEmpty())args.push_back(L"--access-stdin");
#ifndef EXTRAPANE_CONTROLLER_TEST_CHILDREN
        if(row.kind=="usb")row.child.start_elevated(L"ExtraPane.Usb.exe",args);
        else
#endif
        {args.insert(args.end(),{L"--parent-pid",std::to_wstring(GetCurrentProcessId())});row.child.start((extrapane::windows_bridge::directory()/executable(row.kind).toStdWString()).wstring(),args,row.kind!="display");}
        if(row.kind!="display")row.child.send_control(QByteArray::fromHex(config["sourceId"].toString().toLatin1()).toStdString()+"\n");
        if(!credential.isEmpty())row.child.send_control(credential.toStdString()+"\n");
        if(row.kind=="camera")row.child.send_control(row.mirror?"MIRROR 1\n":"MIRROR 0\n");
        model_.setState(id,State::connecting,"Connecting...");
    }catch(const std::exception& error){if(row.kind=="display")emit displayEvent("client","DISPLAY_LAUNCH_FAILED row="+id+" "+QString::fromUtf8(error.what()));row.failed=true;if(row.sharedActive){try{impl_->sources.at(row.sharedSource)->child.send_control("REMOVE\t"+id.toStdString()+"\n");}catch(...){}row.sharedActive=false;}row.child.stop();model_.setEnabled(id,false);model_.setState(id,State::failed,QString::fromUtf8(error.what()));auto& b=*impl_->brokers[row.broker];b.inflight.clear();advance(row.broker);}
    credential.fill('\0');emit changed();
}
void WindowsConnectionRuntime::stop(QString id,bool forced){
    auto found=impl_->rows.find(id);if(found==impl_->rows.end())return;auto& row=*found->second;
    if(!row.child.process&&!row.sharedActive){
        auto b=impl_->brokers.find(row.broker);
        if(b!=impl_->brokers.end()&&b->second->process&&b->second->inflight==id){model_.setState(id,State::returning,"Cancelling pending admission...");return;}
        if(b!=impl_->brokers.end())std::erase(b->second->queue,id);
        if(model_.connection(id)["phase"]!="recovery")model_.setState(id,State::idle);return;
    }if(row.stopping&&!forced)return;
    try{if(row.sharedActive){impl_->sources.at(row.sharedSource)->child.send_control("REMOVE\t"+id.toStdString()+"\n");row.stopping=true;model_.setState(id,State::returning,"Stopping...");}else if(row.kind=="display"){row.child.stop();model_.setState(id,State::idle);auto& b=*impl_->brokers.at(row.broker);if(b.inflight==id){b.inflight.clear();advance(row.broker);}}else{row.child.send_control(forced&&row.kind=="usb"?"X\n":"Q\n");row.stopping=true;model_.setState(id,State::returning,row.kind=="usb"?"Returning USB to this PC...":"Stopping...");}}
    catch(const std::exception& error){model_.setState(id,row.kind=="usb"?State::recovery:State::failed,QString::fromUtf8(error.what()));}
    emit changed();
}
void WindowsConnectionRuntime::disconnectAll(){
    for(auto& [key,b]:impl_->brokers)b->closing=true;
    for(int i=0;i<model_.rowCount();++i){const auto id=model_.data(model_.index(i),ConnectionModel::Id).toString();model_.setEnabled(id,false);stop(id);}
    for(auto& [key,b]:impl_->brokers)closeIfIdle(key);emit changed();
}
void WindowsConnectionRuntime::closeIfIdle(QString key){
    auto found=impl_->brokers.find(key);if(found==impl_->brokers.end())return;auto& b=*found->second;if(!b.closing||!b.process)return;
    for(const auto& [id,row]:impl_->rows)if(row->broker==key&&(row->child.process||row->sharedActive))return;
    for(auto& [id,row]:impl_->rows)if(row->broker==key&&!row->child.process&&!row->sharedActive&&(model_.connection(id)["phase"]=="connecting"||model_.connection(id)["phase"]=="returning"))model_.setState(id,State::idle,model_.connection(id)["status"].toString());
    auto* process=b.process;b.process=nullptr;b.queue.clear();b.inflight.clear();process->disconnect(this);process->write("Q\n");process->closeWriteChannel();connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),process,&QObject::deleteLater);QTimer::singleShot(2000,process,[process]{if(process->state()!=QProcess::NotRunning)process->kill();});emit changed();
}
void WindowsConnectionRuntime::lost(QString key,QString error){
    auto found=impl_->brokers.find(key);if(found==impl_->brokers.end())return;auto& b=*found->second;
    if(b.process){auto* process=b.process;b.process=nullptr;process->disconnect(this);process->kill();process->deleteLater();}b.ready=false;b.queue.clear();b.inflight.clear();
    for(auto& [id,row]:impl_->rows)if(row->broker==key){model_.setEnabled(id,false);stop(id,true);if(!row->child.process&&!row->sharedActive)model_.setState(id,State::failed,error);}
    status_=error;emit changed();
}
void WindowsConnectionRuntime::poll(){
    for(auto& [key,source]:impl_->sources){if(!source->child.process)continue;source->pending+=QString::fromUtf8(source->child.drain());
        while(source->pending.contains('\n')){const auto at=source->pending.indexOf('\n');const auto line=source->pending.left(at).trimmed();source->pending.remove(0,at+1);const auto fields=line.split('\t');
            if(fields.size()>=3&&fields[0]=="RESOURCE_EVENT"){
                auto f=impl_->rows.find(fields[1]);if(f==impl_->rows.end()||f->second->sharedSource!=key||!f->second->sharedActive)continue;auto& row=*f->second;
                if(fields[2]=="STARTED")row.pending+=(row.kind=="camera"?"CAMERA_STARTED":"MICROPHONE_STARTED")+QString("\n");
                else if(fields[2]=="ERROR"){row.pending+="error: "+fields.mid(3).join(' ')+'\n';row.ended=true;}
                else if(fields[2]=="STOPPED")row.ended=true;
            }else if(line.startsWith("CAMERA_MIRROR")){source->mirror=line.endsWith("on")||line.endsWith("1");emit changed();}
            else if(line.startsWith("error:")){for(auto& [id,row]:impl_->rows)if(row->sharedSource==key&&row->sharedActive){row->pending+=line+'\n';row->ended=true;}}
        }
        if(source->pending.size()>16384)source->pending.clear();
        if(WaitForSingleObject(source->child.process,0)==WAIT_OBJECT_0){source->child.stop();for(auto& [id,row]:impl_->rows)if(row->sharedSource==key&&row->sharedActive){row->pending+="error: Source capture engine ended.\n";row->ended=true;}emit changed();}
    }
    for(auto& [id,ptr]:impl_->rows){auto& row=*ptr;if(!row.child.process&&!row.sharedActive&&!row.ended)continue;
        if(row.child.process)row.pending+=QString::fromUtf8(row.child.drain());
        while(row.pending.contains('\n')){
            const auto at=row.pending.indexOf('\n');const auto line=row.pending.left(at).trimmed();row.pending.remove(0,at+1);
            if(row.kind=="usb"&&(line.startsWith("USB_")||line.startsWith("error:")))emit usbEvent("client",line);
            if(row.kind=="display"&&(line.startsWith("VIDEO_")||line.startsWith("DISPLAY_")||line.startsWith("KEYBOARD_")||line.startsWith("error:")))emit displayEvent("client","row="+id+" "+line.left(2048));
            auto& b=*impl_->brokers[row.broker];
            const bool consumed=line.startsWith("VIDEO_RECEIVING")||line.startsWith("VIDEO_STARTED")||line.startsWith("CAMERA_STARTED")||line.startsWith("MICROPHONE_STARTED")||line.startsWith("USB_WAITING")||line.startsWith("USB_STARTED");
            if(consumed){if(!row.stopping)model_.setState(id,State::active,line.startsWith("USB_WAITING")?"Waiting for USB storage on the server...":"Connected");if(b.inflight==id){b.inflight.clear();advance(row.broker);}}
            else if(line.startsWith("error:")){row.failed=true;model_.setState(id,State::returning,line.mid(6).trimmed());}
            else if(line.startsWith("USB_RECOVERY_REQUIRED")){row.recovery=true;model_.setState(id,State::recovery,line);}
            else if(line.startsWith("USB_RETURNED")){row.returned=true;row.recovery=false;row.returnedStatus=line.startsWith("USB_RETURNED_INTERRUPTED")?line.mid(24).trimmed():QString("USB returned to this PC.");model_.setState(id,State::returning,row.returnedStatus);}
            else if(line.startsWith("USB_REVOKING")){row.stopping=true;model_.setEnabled(id,false);model_.setState(id,State::returning,line.mid(12).trimmed());}
            else if(line.startsWith("USB_NOT_READY"))model_.setState(id,State::active,"USB is not available on the server. "+line.mid(13).trimmed());
            else if(line.startsWith("USB_STOP_BLOCKED")||line.startsWith("USB_RETURN_FAILED")){
                if(b.process&&b.ready){row.stopping=false;b.closing=false;model_.setEnabled(id,true);model_.setState(id,State::active,line.startsWith("USB_STOP_BLOCKED")?"USB is busy. Close its files on the server, then uncheck to retry.":"USB return failed; transfer remains reserved. "+line.mid(17).trimmed());}
            }
        }
        if(row.pending.size()>16384)row.pending.clear();
        if(row.ended||(row.child.process&&WaitForSingleObject(row.child.process,0)==WAIT_OBJECT_0)){
            if(row.kind=="display"&&row.child.process){DWORD code=0;const bool known=GetExitCodeProcess(row.child.process,&code)!=FALSE;const auto detail="Display engine exited (code "+(known?QString::number(code):QString("unknown"))+").";emit displayEvent("client","row="+id+" "+detail);if(!row.stopping&&!row.failed){row.failed=true;model_.setState(id,State::returning,detail);}}
            row.child.stop();row.sharedActive=false;row.ended=false;const auto detail=model_.connection(id)["status"].toString();const bool restart=row.stopping&&model_.connection(id)["connectionEnabled"].toBool();row.stopping=false;
            model_.setState(id,row.recovery?State::recovery:row.failed?State::failed:State::idle,row.returned?row.returnedStatus:row.failed||row.recovery?detail:QString{});
            if(!restart)model_.setEnabled(id,false);
            auto& b=*impl_->brokers[row.broker];if(b.inflight==id){b.inflight.clear();advance(row.broker);}closeIfIdle(row.broker);
            if(row.returned)refreshDevices("usb");if(restart&&!row.recovery&&!b.closing&&b.process)toggle(id,true,{},row.input);emit changed();
        }
    }
    for(auto& [key,source]:impl_->sources){if(!source->child.process||source->closing)continue;bool used=false;for(const auto& [id,row]:impl_->rows)if(row->sharedSource==key&&row->sharedActive)used=true;if(!used){try{source->child.send_control("Q\n");source->closing=true;}catch(...){source->child.stop();}}}
}
void WindowsConnectionRuntime::setMirror(QString id,bool enabled){auto f=impl_->rows.find(id);if(f==impl_->rows.end()||f->second->kind!="camera"||!f->second->sharedActive)return;auto& source=*impl_->sources.at(f->second->sharedSource);try{source.child.send_control(enabled?"MIRROR\t1\n":"MIRROR\t0\n");source.mirror=enabled;emit changed();}catch(...){} }
void WindowsConnectionRuntime::setMuted(QString id,bool enabled){auto f=impl_->rows.find(id);if(f==impl_->rows.end()||f->second->kind!="microphone"||!f->second->sharedActive)return;auto& row=*f->second;try{impl_->sources.at(row.sharedSource)->child.send_control("MUTE\t"+id.toStdString()+(enabled?"\t1\n":"\t0\n"));row.muted=enabled;}catch(...){} }
void WindowsConnectionRuntime::refreshDevices(QString kind){
    if(kindIndex(kind)<1||impl_->scanning.contains(kind))return;impl_->scanning.insert(kind);auto* process=new QProcess(this);auto output=std::make_shared<QByteArray>();
    connect(process,&QProcess::readyReadStandardOutput,this,[process,output]{*output+=process->readAllStandardOutput();if(output->size()>1024*1024)process->kill();});
    auto done=[this,process,output,kind](bool success){if(!impl_->scanning.remove(kind))return;*output+=process->readAllStandardOutput();QVariantList list;bool complete=false,valid=success&&output->size()<=1024*1024;QSet<QString> ids;
        const auto prefix=kind=="camera"?"CAMERA_DEVICE_V1":kind=="microphone"?"MICROPHONE_DEVICE_V1":"USB_DEVICE_V1";const auto end=kind=="camera"?"CAMERA_LIST_DONE":kind=="microphone"?"MICROPHONE_LIST_DONE":"USB_LIST_DONE";
        for(auto line:output->split('\n')){line=line.trimmed();if(line.isEmpty())continue;if(line==end){if(complete)valid=false;complete=true;continue;}const auto fields=line.split('\t');if(complete||fields.size()!=4||fields[0]!=prefix||list.size()>=128||fields[2].isEmpty()||fields[2].size()>2048){valid=false;break;}
            const auto id=kind=="usb"?fields[1]:fields[3];const bool available=kind!="usb"||fields[3]=="available";
            if(id.size()>8192||id.size()%2||!QRegularExpression("^[0-9a-f]+$").match(QString::fromLatin1(id)).hasMatch()||ids.contains(id)){valid=false;break;}ids.insert(id);
            list.push_back(QVariantMap{{"id",QString::fromLatin1(id)},{"label",QString::fromUtf8(fields[2])+(available?QString{}:" — "+QString::fromUtf8(fields[3]))},{"available",available}});
        }
        if(kind=="usb"&&valid&&complete)for(auto& [id,row]:impl_->rows)if(row->kind=="usb"&&!row->child.process&&model_.connection(id)["phase"]=="recovery"){
            const auto source=model_.connection(id)["sourceId"].toString();for(const auto& device:list)if(device.toMap()["id"]==source&&device.toMap()["available"].toBool()){row->recovery=false;model_.setEnabled(id,false);model_.setState(id,State::idle,"USB is available on this PC again.");break;}
        }
        devices_[kind]=valid&&complete?list:QVariantList{};if(!valid||!complete)status_="Could not list "+kind+" devices. Check installation and permissions.";process->deleteLater();emit devicesChanged();emit changed();};
    connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[done](int code,QProcess::ExitStatus status){done(code==0&&status==QProcess::NormalExit);});connect(process,&QProcess::errorOccurred,this,[done](QProcess::ProcessError error){if(error==QProcess::FailedToStart)done(false);});QTimer::singleShot(10000,process,[process]{process->kill();});process->start(QCoreApplication::applicationDirPath()+"/"+executable(kind),{kind=="camera"?"list-cameras":kind=="microphone"?"list-microphones":"list-storage"});
}
void WindowsConnectionRuntime::shutdown(){
    if(!impl_)return;for(auto& [key,b]:impl_->brokers)if(b->process){b->process->disconnect(this);b->process->kill();b->process->waitForFinished(1000);b->process->deleteLater();b->process=nullptr;}for(auto& [id,row]:impl_->rows)row->child.stop();for(auto& [key,source]:impl_->sources)source->child.stop();
}
