#include <windows.h>
#include <dm/tls.hpp>
#include <dm/group.hpp>
#include <dm/control.hpp>
#include <charconv>
#include <iostream>
#include <map>
#include <thread>
#include <atomic>
#include <algorithm>
namespace {
unsigned number(const std::string& text,unsigned max){unsigned value=0;const auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),value);if(error!=std::errc{}||end!=text.data()+text.size()||!value||value>max)throw std::invalid_argument("Invalid numeric option");return value;}
void ipv4(const std::string& text){if(_stricmp(text.c_str(),"localhost")==0)return;std::size_t offset=0;for(unsigned i=0;i<4;++i){const auto dot=text.find('.',offset);if((i<3)==(dot==text.npos))throw std::invalid_argument("Expected IPv4 literal");const auto part=text.substr(offset,(dot==text.npos?text.size():dot)-offset);unsigned value=0;const auto [end,error]=std::from_chars(part.data(),part.data()+part.size(),value);if(error!=std::errc{}||end!=part.data()+part.size()||part.empty()||value>255||(i==0&&(!value||value>=224)))throw std::invalid_argument("Expected reachable unicast IPv4");offset=dot+1;}}
struct Handle{HANDLE value=nullptr;~Handle(){if(value)CloseHandle(value);}};
const char* name(unsigned kind){constexpr const char* names[]={"display","camera","microphone","usb","speaker"};return names[kind];}
}
int main(int argc,char** argv){try{
    std::map<std::string,std::string> values;bool input=false,check=false,session=false;
    for(int i=1;i<argc;++i){std::string arg=argv[i];if(arg=="--session"){if(session)throw std::invalid_argument("Duplicate session option");session=true;continue;}if(arg=="--access-stdin"){if(input)throw std::invalid_argument("Duplicate input option");input=true;continue;}if(arg=="--check-config"){if(check)throw std::invalid_argument("Duplicate check option");check=true;continue;}
        if(arg!="--bind"&&arg!="--peer"&&arg!="--port"&&arg!="--resources"&&arg!="--parent-pid")throw std::invalid_argument("Unknown admission option");if(++i>=argc||!values.emplace(arg,argv[i]).second)throw std::invalid_argument("Missing or duplicate admission option");}
    if(!input||!values.contains("--bind")||!values.contains("--peer")||!values.contains("--port")||(!session&&!values.contains("--resources")))throw std::invalid_argument("Admission requires interface, peer, port, resources and --access-stdin");
    dm::net::Options options{values.at("--bind"),values.at("--peer"),static_cast<uint16_t>(number(values.at("--port"),65535)),std::chrono::seconds(5)};
    if(options.bind_address!="auto")ipv4(options.bind_address);ipv4(options.peer);const auto mask=values.contains("--resources")?number(values.at("--resources"),15):0;
    const auto parent_pid=values.contains("--parent-pid")?number(values.at("--parent-pid"),0xffffffffu):0;
    if(check){std::cout<<"CONFIG_OK grouped-admission resources="<<mask<<'\n';return 0;}
    const HANDLE input_handle=GetStdHandle(STD_INPUT_HANDLE);
    std::string code;bool ended=false;for(unsigned i=0;i<7;++i){char value=0;DWORD read=0;if(!ReadFile(input_handle,&value,1,&read,nullptr)||read!=1)throw std::invalid_argument("Missing access code");if(value=='\n'){ended=true;break;}code+=value;}if(!ended||(!dm::access::validCode(code)&&!(session&&code.empty())))throw std::invalid_argument("Access code must contain six digits");
    const bool protected_mode=!code.empty();if(protected_mode)dm::security::set_access_client(code);SecureZeroMemory(code.data(),code.size());code.clear();
    Handle parent;if(parent_pid){parent.value=OpenProcess(SYNCHRONIZE,FALSE,parent_pid);if(!parent.value)throw std::runtime_error("Launcher unavailable");}
    dm::security::TlsStream stream(dm::net::connect_stream(options),false,"");
    std::jthread watcher([&](std::stop_token token){while(!token.stop_requested()){if(parent.value&&WaitForSingleObject(parent.value,50)!=WAIT_TIMEOUT){stream.cancel();return;}if(!parent.value)Sleep(50);}});
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);stream.handshake(deadline);
    if(session){
        using namespace dm::access;using Clock=std::chrono::steady_clock;
        auto exchange=[&](ControlOperation op,unsigned kind=0){const auto until=Clock::now()+std::chrono::seconds(5);stream.write(control_request(op,kind),until);ControlReply reply{};stream.read(reply,until);auto credential=control_credential(reply,op,kind,protected_mode);SecureZeroMemory(reply.data(),reply.size());return credential;};
        exchange(ControlOperation::open);std::cout<<"SESSION_READY"<<std::endl;
        auto heartbeat=Clock::now()+std::chrono::seconds(5);std::string pending;
        while(true){
            if(parent.value&&WaitForSingleObject(parent.value,0)!=WAIT_TIMEOUT)break;
            DWORD available=0;if(!PeekNamedPipe(input_handle,nullptr,0,nullptr,&available,nullptr)){if(GetLastError()==ERROR_BROKEN_PIPE)break;throw std::runtime_error("Control input pipe unavailable");}
            if(available){char value=0;DWORD read=0;if(!ReadFile(input_handle,&value,1,&read,nullptr)||read!=1)break;
                if(value=='\n'){
                    if(pending=="Q")break;
                    if((pending.size()!=5&&pending.size()!=7)||pending.substr(0,4)!="ADD "||pending[4]<'0'||pending[4]>'4'||(pending.size()==7&&(pending[4]!='0'||pending[5]!=' '||(pending[6]!='0'&&pending[6]!='1'))))throw std::invalid_argument("Invalid session command");
                    const unsigned kind=pending[4]-'0';const auto operation=pending.size()==7?(pending[6]=='1'?ControlOperation::add_display_control:ControlOperation::add_display_view):ControlOperation::add;auto credential=exchange(operation,kind);
                    std::cout<<"SESSION_TICKET_V1\t"<<name(kind)<<'\t'<<(protected_mode?credential:"-")<<std::endl;SecureZeroMemory(credential.data(),credential.size());pending.clear();
                }else{if(pending.size()>=16)throw std::invalid_argument("Session command too long");pending+=value;}
            }else Sleep(25);
            if(Clock::now()>=heartbeat){exchange(ControlOperation::ping);heartbeat=Clock::now()+std::chrono::seconds(5);}
        }
        stream.write(control_request(ControlOperation::close),Clock::now()+std::chrono::seconds(2));return 0;
    }
    stream.write(dm::access::group_request(mask),deadline);dm::access::GroupReply reply{};stream.read(reply,deadline);
    auto tickets=dm::access::group_tickets(reply,mask);SecureZeroMemory(reply.data(),reply.size());
    for(auto& ticket:tickets){auto credential=dm::access::formatCredential(ticket);std::cout<<"SESSION_TICKET_V1\t"<<name(ticket.kind)<<'\t'<<credential<<'\n';SecureZeroMemory(ticket.secret.data(),ticket.secret.size());SecureZeroMemory(credential.data(),credential.size());}
    std::cout<<"SESSION_READY"<<std::endl;return 0;
}catch(const std::exception& error){std::cerr<<"error: "<<error.what()<<'\n';return 1;}}
