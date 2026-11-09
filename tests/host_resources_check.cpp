#include "../core/session/include/extrapane/host_resources.hpp"
#include <vector>
#include <thread>
#include <stdexcept>
void check(bool value){if(!value)throw std::runtime_error("host resource lifecycle invariant");}
int main(){
    extrapane::HostResources resources;std::atomic_bool quit=false,stop=false;
    std::vector<unsigned> usb;
    for(unsigned i=1;i<=80;++i){check(resources.acquire(0,quit,stop)==i);usb.push_back(resources.acquire(3,quit,stop));}
    check(resources.acquire(4,quit,stop)==1);check(resources.acquire(4,quit,stop)==2);
    resources.release(4,1);check(resources.acquire(4,quit,stop)==1);
    auto first=resources.eject_signal(usb[0]),second=resources.eject_signal(usb[1]);
    resources.eject_all();check(first->exchange(false));check(second->exchange(false));
    check(!first->exchange(false)); // one consumer cannot steal another's request
    resources.release(3,usb[0]);check(!resources.stop_if_no_usb(stop));check(!stop);
    check(resources.acquire(3,quit,stop)==usb[0]);
    auto replacement=resources.eject_signal(usb[0]);check(replacement!=first&&!replacement->load());
    resources.eject_all();check(replacement->load()&&second->load());
    quit=true;bool rejected=false;try{resources.acquire(3,quit,stop);}catch(...){rejected=true;}check(rejected);
    for(auto id:usb)resources.release(3,id);check(resources.stop_if_no_usb(stop)&&stop);
    // Concurrent allocations must stay distinct until released.
    extrapane::HostResources parallel;quit=false;stop=false;std::vector<std::thread> threads;std::vector<unsigned> ids(64);
    for(unsigned i=0;i<ids.size();++i)threads.emplace_back([&,i]{ids[i]=parallel.acquire(2,quit,stop);});
    for(auto& thread:threads)thread.join();
    for(unsigned i=0;i<ids.size();++i)for(unsigned j=i+1;j<ids.size();++j)check(ids[i]!=ids[j]);
}
