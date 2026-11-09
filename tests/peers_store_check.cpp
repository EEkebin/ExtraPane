#include <extrapane/store.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <dpapi.h>
#else
#include <sys/stat.h>
#include <openssl/sha.h>
#endif
using namespace extrapane::peers;
namespace {
void check(bool value){if(!value)throw std::runtime_error("Trust store check failed");}
template<class F>void rejected(F fn){bool failed=false;try{fn();}catch(const std::exception&){failed=true;}check(failed);}
DeviceId id(unsigned n){DeviceId result{};result[0]=static_cast<std::uint8_t>(n);return result;}
std::string bytes(const std::filesystem::path& p){std::ifstream file(p,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};}
void overwrite(const std::filesystem::path& p,const std::string& value){std::ofstream out(p,std::ios::binary|std::ios::trunc);out.write(value.data(),value.size());check(out.good());}
std::string legacy(const DeviceId& self,const RememberedDevice& peer,const PairSecret& secret){
    Bytes out{'E','P','T','S',0,1,0,0};out.insert(out.end(),self.begin(),self.end());out.push_back(0);out.push_back(1);out.insert(out.end(),peer.id.begin(),peer.id.end());out.insert(out.end(),secret.begin(),secret.end());
    for(const auto& value:{peer.name,peer.address_hint}){out.push_back(static_cast<std::uint8_t>(value.size()>>8));out.push_back(static_cast<std::uint8_t>(value.size()));out.insert(out.end(),value.begin(),value.end());}out.push_back(static_cast<std::uint8_t>(peer.port_hint>>8));out.push_back(static_cast<std::uint8_t>(peer.port_hint));Bytes sum(32);
#ifdef _WIN32
    BCRYPT_ALG_HANDLE algorithm=nullptr;check(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0);check(BCryptHash(algorithm,nullptr,0,out.data(),static_cast<ULONG>(out.size()),sum.data(),static_cast<ULONG>(sum.size()))>=0);BCryptCloseAlgorithmProvider(algorithm,0);
#else
    check(SHA256(out.data(),out.size(),sum.data())!=nullptr);
#endif
    out.insert(out.end(),sum.begin(),sum.end());
#ifdef _WIN32
    DATA_BLOB in{static_cast<DWORD>(out.size()),out.data()},encrypted{};check(CryptProtectData(&in,L"ExtraPane test legacy fixture",nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&encrypted));std::string bytes(reinterpret_cast<char*>(encrypted.pbData),encrypted.cbData);LocalFree(encrypted.pbData);SecureZeroMemory(out.data(),out.size());return bytes;
#else
    return {reinterpret_cast<const char*>(out.data()),out.size()};
#endif
}

}
int main(){
    const auto base=std::filesystem::temp_directory_path()/("extrapane-peers-store-check-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(base);
    try{
        const auto folder=base/"private",file=folder/"trust-v1.bin";TrustStore store(folder);const auto self=store.identity();check(valid_id(self));check(store.devices().empty());TrustStore reopened(folder);check(reopened.identity()==self);
        check(store.pairing_password().empty());const std::string password=" secret ! \xc3\xa9 ";store.set_pairing_password(password);check(reopened.pairing_password()==password&&store.identity()==self);
#ifdef _WIN32
        check(bytes(folder/"pairing-v1.bin").find(password)==std::string::npos);
#endif
        const auto savedPairing=bytes(folder/"pairing-v1.bin");rejected([&]{store.set_pairing_password(std::string(1025,'x'));});check(bytes(folder/"pairing-v1.bin")==savedPairing);
        store.set_pairing_password(std::string(1024,'x'));check(reopened.pairing_password()==std::string(1024,'x'));store.set_pairing_password("");check(reopened.pairing_password().empty());
        auto badPairing=bytes(folder/"pairing-v1.bin");badPairing.back()^=1;overwrite(folder/"pairing-v1.bin",badPairing);rejected([&]{store.pairing_password();});overwrite(folder/"pairing-v1.bin",savedPairing);check(reopened.pairing_password()==password);
        PairSecret secret=id(77);RememberedDevice peer{id(1),"Surface Book","192.0.2.1",43824};store.remember(peer,secret);check(store.secret(id(1))==secret&&reopened.secret(id(1))==secret);check(reopened.devices()[0].name==peer.name);
        peer.address_hint="192.0.2.2";reopened.update_hint(peer);check(store.devices()[0].address_hint=="192.0.2.2");rejected([&]{reopened.remember(peer,secret);});
        const auto original=bytes(file);rejected([&]{store.remember(peer,id(78));});check(bytes(file)==original&&store.secret(id(1))==secret);
        rejected([&]{store.remember({self,"Self","",0},secret);});check(bytes(file)==original);
        store.remember({id(2),"Phone","",0},id(88));check(reopened.devices().size()==2);check(reopened.forget(id(1)));check(!store.secret(id(1))&&store.secret(id(2)).has_value());check(!store.forget(id(1)));check(store.identity()==self);
        // Migrate a real version-one payload without changing identity or pairing.
        const auto previous=bytes(file);overwrite(file,legacy(self,peer,secret));TrustStore migrated(folder);check(migrated.identity()==self&&migrated.secret(peer.id)==secret);check(migrated.begin_forget(peer.id));TrustStore migratedAgain(folder);check(migratedAgain.identity()==self&&!migratedAgain.secret(peer.id)&&migratedAgain.pending_forgets().size()==1);overwrite(file,previous);
        RememberedDevice revoke{id(3),"Revoked laptop","192.0.2.3",43825};PairSecret revokeKey=id(99);store.remember(revoke,revokeKey);check(!store.revocation_secret(revoke.id));check(store.begin_forget(revoke.id));check(store.begin_forget(revoke.id));check(!store.secret(revoke.id)&&store.devices().size()==1);check(reopened.pending_forgets().size()==1&&reopened.revocation_secret(revoke.id)==revokeKey);
        rejected([&]{store.remember(revoke,id(100));});rejected([&]{store.update_hint(revoke);});const auto pendingBytes=bytes(file);check(!store.acknowledge_forget(revoke.id,id(100))&&!store.receive_forget(revoke.id,id(100))&&bytes(file)==pendingBytes);
        // ACK persistence survives restart and permits retry after a lost ACK.
        check(store.acknowledge_forget(revoke.id,revokeKey));check(reopened.pending_forgets().empty()&&!reopened.secret(revoke.id)&&reopened.revocation_secret(revoke.id)==revokeKey);check(store.receive_forget(revoke.id,revokeKey)&&store.acknowledge_forget(revoke.id,revokeKey));
        store.remember(revoke,id(100));check(store.secret(revoke.id)==id(100)&&!store.revocation_secret(revoke.id));check(!store.receive_forget(revoke.id,revokeKey)&&!store.acknowledge_forget(revoke.id,revokeKey));check(store.secret(revoke.id)==id(100));
        // Receiving a request commits removal before the reply, independently of
        // whether this side already initiated Forget (simultaneous requests).
        check(store.receive_forget(revoke.id,id(100)));check(!reopened.secret(revoke.id)&&reopened.revocation_secret(revoke.id)==id(100));
        store.remember(revoke,id(101));check(store.begin_forget(revoke.id));check(store.receive_forget(revoke.id,id(101))&&store.pending_forgets().empty());check(store.acknowledge_forget(revoke.id,id(101)));check(!store.receive_forget(id(55),id(55))&&!store.begin_forget(id(55)));
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
        RememberedDevice dismissed{id(4),"Unavailable old peer","192.0.2.4",43825};store.remember(dismissed,id(102));check(store.begin_forget(dismissed.id));check(store.dismiss_forget(dismissed.id));check(!store.secret(dismissed.id)&&store.revocation_secret(dismissed.id)==id(102)&&store.pending_forgets().empty());check(!store.dismiss_forget(dismissed.id));store.remember(dismissed,id(103));check(store.secret(dismissed.id)==id(103));
        // Replay tombstones are bounded and eviction never grants normal access.
        TrustStore bounded(base/"bounded");DeviceId first{},last{};for(unsigned i=1;i<=258;++i){DeviceId device{};device[0]=240;device[1]=static_cast<std::uint8_t>(i>>8);device[2]=static_cast<std::uint8_t>(i);if(i==1)first=device;last=device;bounded.remember({device,"Replay fixture","",0},id(111));check(bounded.receive_forget(device,id(111)));}
        check(!bounded.revocation_secret(first)&&bounded.revocation_secret(last)==id(111));check(bounded.devices().empty()&&bounded.pending_forgets().empty()&&!bounded.secret(last));
        check(store.secret(id(2))==id(88));std::filesystem::remove_all(base);std::cout<<"Private trust identity, atomic persistence, key replacement, corruption and path checks passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';std::error_code ignored;std::filesystem::remove_all(base,ignored);return 1;}
}
