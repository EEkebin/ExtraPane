#include <extrapane/store.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#ifndef _WIN32
#include <sys/stat.h>
#endif
using namespace extrapane::peers;
namespace {
void check(bool value){if(!value)throw std::runtime_error("Trust store check failed");}
template<class F>void rejected(F fn){bool failed=false;try{fn();}catch(const std::exception&){failed=true;}check(failed);}
DeviceId id(unsigned n){DeviceId result{};result[0]=static_cast<std::uint8_t>(n);return result;}
std::string bytes(const std::filesystem::path& p){std::ifstream file(p,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};}
void overwrite(const std::filesystem::path& p,const std::string& value){std::ofstream out(p,std::ios::binary|std::ios::trunc);out.write(value.data(),value.size());check(out.good());}
}
int main(){
    const auto base=std::filesystem::temp_directory_path()/("extrapane-peers-store-check-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(base);
    try{
        const auto folder=base/"private",file=folder/"trust-v1.bin";TrustStore store(folder);const auto self=store.identity();check(valid_id(self));check(store.devices().empty());TrustStore reopened(folder);check(reopened.identity()==self);
        PairSecret secret=id(77);RememberedDevice peer{id(1),"Surface Book","192.0.2.1",43824};store.remember(peer,secret);check(store.secret(id(1))==secret&&reopened.secret(id(1))==secret);check(reopened.devices()[0].name==peer.name);
        peer.address_hint="192.0.2.2";reopened.update_hint(peer);check(store.devices()[0].address_hint=="192.0.2.2");rejected([&]{reopened.remember(peer,secret);});
        const auto original=bytes(file);rejected([&]{store.remember(peer,id(78));});check(bytes(file)==original&&store.secret(id(1))==secret);
        rejected([&]{store.remember({self,"Self","",0},secret);});check(bytes(file)==original);
        store.remember({id(2),"Phone","",0},id(88));check(reopened.devices().size()==2);check(reopened.forget(id(1)));check(!store.secret(id(1))&&store.secret(id(2)).has_value());check(!store.forget(id(1)));check(store.identity()==self);
        const auto valid=bytes(file);auto corrupt=valid;corrupt[corrupt.size()/2]^=1;overwrite(file,corrupt);rejected([&]{TrustStore bad(folder);});rejected([&]{store.remember(peer,secret);});check(bytes(file)==corrupt);overwrite(file,valid);
        overwrite(file,valid.substr(0,valid.size()/2));rejected([&]{TrustStore truncated(folder);});overwrite(file,valid);
        // File aliases are rejected before an operation can change the original.
        const auto alias=base/"hardlink";std::filesystem::create_hard_link(file,alias);rejected([&]{store.forget(id(2));});check(bytes(file)==valid);std::filesystem::remove(alias);
        const auto moved=folder/"preserved.bin";std::filesystem::rename(file,moved);std::filesystem::create_directory(file);rejected([&]{store.remember(peer,secret);});check(bytes(moved)==valid);std::filesystem::remove(file);std::filesystem::rename(moved,file);
#ifndef _WIN32
        struct stat permissions{};check(stat(folder.c_str(),&permissions)==0&&(permissions.st_mode&0777)==0700);check(stat(file.c_str(),&permissions)==0&&(permissions.st_mode&0777)==0600);
        chmod(file.c_str(),0644);rejected([&]{TrustStore broad(folder);});chmod(file.c_str(),0600);
        std::filesystem::rename(file,moved);std::filesystem::create_symlink(moved,file);rejected([&]{store.secret(id(2));});std::filesystem::remove(file);std::filesystem::rename(moved,file);
#endif
        check(store.secret(id(2))==id(88));std::filesystem::remove_all(base);std::cout<<"Private trust identity, atomic persistence, key replacement, corruption and path checks passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';std::error_code ignored;std::filesystem::remove_all(base,ignored);return 1;}
}
