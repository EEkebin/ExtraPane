#include "../apps/camera-setup/registration.hpp"
#include <iostream>
using namespace extrapane::camera_setup;
void check(bool value){if(!value)throw std::runtime_error("Camera setup policy check failed");}
struct MemoryStore final:Store {
    Snapshot state{};unsigned writes=0,fail_at=0;
    Text read(Value value)override{return state[static_cast<unsigned>(value)];}
    void write(Value value,const Text& text)override{if(++writes==fail_at)throw std::runtime_error("Simulated registry write failure");state[static_cast<unsigned>(value)]=text;}
};
int main(){try{
    const auto initial=registration(L"C:\\protected\\old.dll");const auto updated=registration(L"C:\\protected\\new.dll");
    for(unsigned failure=1;failure<=4;++failure){
        MemoryStore store;store.state=initial;store.fail_at=failure;
        bool rejected=false;try{replace(store,updated);}catch(...){rejected=true;}
        check(rejected&&store.state==initial);
        MemoryStore fresh;fresh.fail_at=failure;try{replace(fresh,updated);}catch(...){}check(absent(fresh.state));
        MemoryStore removal;removal.state=initial;removal.fail_at=failure;try{replace(removal,{});}catch(...){}check(removal.state==initial);
    }
    MemoryStore store;replace(store,updated);check(owner_matches(store.state));replace(store,{});check(absent(store.state));
    check(payload_name(L"ExtraPane.CameraSource."+std::wstring(64,L'a')+L".dll"));
    check(!payload_name(L"Other.dll")&&!payload_name(L"ExtraPane.CameraSource."+std::wstring(64,L'g')+L".dll"));
    std::cout<<"Camera setup ownership and rollback policy checks passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
