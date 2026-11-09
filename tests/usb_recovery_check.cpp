#include <dm/usb_recovery.hpp>
#include <dm/usb_runner.hpp>
#include <iostream>
#include <algorithm>
#include <limits>
#include <stdexcept>
using namespace dm::usb;
void require(bool value){if(!value)throw std::runtime_error("USB recovery policy check failed");}
template<class F>void rejects(F f){try{f();}catch(const std::exception&){return;}throw std::runtime_error("Invalid recovery record accepted");}
int main(){try{
    const std::string activePort="Port 01: device in use at High Speed\n -> usbip://127.0.0.1:1234/1-2\n -> serial: ABC123\n";
    const std::string stoppedPort="Port 01: device disconnected\n -> usbip://127.0.0.1:1234/1-2\n -> serial: ABC123\n";
    require(matches_import_port(activePort,1,1234,"1-2","ABC123",true));
    require(matches_import_port(stoppedPort,1,1234,"1-2","ABC123",false));
    require(!matches_import_port(stoppedPort,1,1234,"1-2","ABC123",true));
    require(!matches_import_port(stoppedPort,2,1234,"1-2","ABC123",false));
    require(!matches_import_port(stoppedPort,1,1235,"1-2","ABC123",false));
    require(!matches_import_port(stoppedPort,1,1234,"1-3","ABC123",false));
    require(!matches_import_port(stoppedPort,1,1234,"1-2","OTHER",false));
    require(!matches_import_port(stoppedPort+stoppedPort,1,1234,"1-2","ABC123",false));
    require(!matches_import_port("Port 01: invalid device\n",1,1234,"1-2","ABC123",false));
    std::vector<std::uint8_t> medium(512*256);
    auto put=[&](std::size_t offset,std::uint64_t value,unsigned bytes){for(unsigned i=0;i<bytes;++i)medium[offset+i]=static_cast<std::uint8_t>(value>>(8*i));};
    medium[510]=0x55;medium[511]=0xaa;medium[450]=7;put(454,32,4);put(458,224,4);put(440,0x12345678,4);
    auto read=[&](std::uint64_t offset,std::uint32_t length){require(offset<=medium.size()&&length<=medium.size()-offset);return std::vector<std::uint8_t>(medium.begin()+offset,medium.begin()+offset+length);};
    const auto identity=detail::partition_metadata(read,512,medium.size());
    medium[32*512+123]=1;require(detail::partition_metadata(read,512,medium.size())==identity); // Writable filesystem data is excluded.
    medium[440]^=1;require(detail::partition_metadata(read,512,medium.size())!=identity);medium[440]^=1;
    medium[450]=5;rejects([&]{detail::partition_metadata(read,512,medium.size());});medium[450]=7;
    medium[510]=0;rejects([&]{detail::partition_metadata(read,512,medium.size());});medium[510]=0x55;
    rejects([&]{detail::partition_metadata(read,511,medium.size());});
    auto shortRead=[](std::uint64_t,std::uint32_t){return std::vector<std::uint8_t>{};};rejects([&]{detail::partition_metadata(shortRead,512,medium.size());});
    // GPT identity includes the checked primary table; writable partition data
    // is excluded. Both headers/tables and all ranges must be internally sound.
    {
        auto crc=[](std::span<const std::uint8_t> bytes){std::uint32_t value=~0u;for(auto byte:bytes){value^=byte;for(unsigned bit=0;bit<8;++bit)value=(value>>1)^(0xedb88320u&static_cast<std::uint32_t>(-static_cast<int>(value&1)));}return ~value;};
        auto headerCrc=[&](std::size_t base){put(base+16,0,4);put(base+16,crc(std::span(medium).subspan(base,92)),4);};
        auto tableCrc=[&]{const auto value=crc(std::span(medium).subspan(2*512,128*128));put(512+88,value,4);put(255*512+88,value,4);headerCrc(512);headerCrc(255*512);};
        medium.assign(512*256,0);medium[510]=0x55;medium[511]=0xaa;medium[450]=0xee;put(454,1,4);put(458,255,4);put(440,0x12345678,4);
        for(const auto base:{std::size_t(512),std::size_t(255*512)}){
            std::copy_n("EFI PART",8,medium.begin()+base);put(base+8,0x10000,4);put(base+12,92,4);
            put(base+24,base==512?1:255,8);put(base+32,base==512?255:1,8);put(base+40,34,8);put(base+48,222,8);
            medium[base+56]=0x19;put(base+72,base==512?2:223,8);put(base+80,128,4);put(base+84,128,4);
        }
        medium[2*512]=0xab;medium[2*512+16]=0xcd;put(2*512+32,34,8);put(2*512+40,200,8);
        std::copy_n(medium.begin()+2*512,128*128,medium.begin()+223*512);tableCrc();
        const auto pristine=medium;const auto identity=detail::partition_metadata(read,512,medium.size());
        medium[40*512+100]^=1;require(detail::partition_metadata(read,512,medium.size())==identity);medium=pristine;
        medium[512+16]^=1;rejects([&]{detail::partition_metadata(read,512,medium.size());});medium=pristine;
        medium[2*512+16]^=1;rejects([&]{detail::partition_metadata(read,512,medium.size());});medium=pristine;
        medium[223*512+16]^=1;rejects([&]{detail::partition_metadata(read,512,medium.size());});medium=pristine;
        // Correct CRCs cannot conceal wrong backup identity or out-of-range LBAs.
        medium[255*512+56]^=1;headerCrc(255*512);rejects([&]{detail::partition_metadata(read,512,medium.size());});medium=pristine;
        put(255*512+72,256,8);headerCrc(255*512);rejects([&]{detail::partition_metadata(read,512,medium.size());});medium=pristine;
        put(512+72,std::numeric_limits<std::uint64_t>::max(),8);headerCrc(512);rejects([&]{detail::partition_metadata(read,512,medium.size());});medium=pristine;
        put(512+80,0xffffffffu,4);put(512+84,0xffffffffu,4);headerCrc(512);rejects([&]{detail::partition_metadata(read,512,medium.size());});medium=pristine;
        // Overlapping partitions with valid checksums must still be refused.
        medium[2*512+128]=0xac;put(2*512+128+32,100,8);put(2*512+128+40,210,8);
        std::copy_n(medium.begin()+2*512,128*128,medium.begin()+223*512);tableCrc();rejects([&]{detail::partition_metadata(read,512,medium.size());});medium=pristine;
        put(2*512+40,std::numeric_limits<std::uint64_t>::max(),8);
        std::copy_n(medium.begin()+2*512,128*128,medium.begin()+223*512);tableCrc();rejects([&]{detail::partition_metadata(read,512,medium.size());});medium=pristine;
        put(255*512+72,222,8);headerCrc(255*512);rejects([&]{detail::partition_metadata(read,512,medium.size());});medium=pristine;
        // An actual partition identity change is detected after both valid table
        // checksums are updated; the old digest cannot silently be retained.
        medium[2*512+16]^=1;std::copy_n(medium.begin()+2*512,128*128,medium.begin()+223*512);tableCrc();
        require(detail::partition_metadata(read,512,medium.size())!=identity);
    }
    Disk disk;disk.instance_id=L"USBSTOR\\TEST";
    PendingRecovery pending;pending.journal_id=L"6ecf2fd1bbf55edf2e08cfb90b404a0d892941475423823e88c58a1469ef469f";
    require(has_pending_recovery(disk,std::span(&pending,1))); // Corrupt records still block export.
    pending.record=RecoveryRecord{};pending.record->disk_number=23;
    disk.disk_number=7;disk.layout_bytes={1,2,3};
    require(has_pending_recovery(disk,std::span(&pending,1))); // Changed facts cannot bypass a journal.
    pending.journal_id[0]=L'0';require(!has_pending_recovery(disk,std::span(&pending,1)));
    require(!has_pending_recovery(disk,{}));
    RecoveryRecord record;record.disk_number=7;record.owner[0]=5;record.fingerprint[0]=8;record.prior_attributes=2;
    auto bytes=encode_recovery(record);require(bytes.size()==72);require(decode_recovery(bytes)==record);
    auto v2=record;v2.version=2;require(decode_recovery(encode_recovery(v2))==v2);
    require(live_interrupted_return_policy(v2).available);v2.state=RecoveryState::offline;require(live_interrupted_return_policy(v2).available);
    v2.state=RecoveryState::binding_intent;v2.bus_id="1-2";require(live_interrupted_return_policy(v2).available);
    v2.state=RecoveryState::bound;v2.binding_id="{01234567-89ab-cdef-0123-456789abcdef}";require(live_interrupted_return_policy(v2).available);
    v2.state=RecoveryState::ejected;require(!live_interrupted_return_policy(v2).available);v2.state=RecoveryState::released;require(!live_interrupted_return_policy(v2).available);
    v2.version=1;v2.state=RecoveryState::bound;require(!live_interrupted_return_policy(v2).available);
    require(recovery_state_allows_restore(record).available);
    require(preexport_retirement_policy(record).available);
    auto preexport=record;preexport.state=RecoveryState::offline;require(preexport_retirement_policy(preexport).available);
    preexport.prior_attributes|=1;require(!preexport_retirement_policy(preexport).available);
    preexport=record;preexport.bus_id="1-2";require(!preexport_retirement_policy(preexport).available);
    record.state=RecoveryState::binding_intent;record.bus_id="1-2";require(decode_recovery(encode_recovery(record))==record);
    require(!recovery_state_allows_restore(record).available);
    require(!preexport_retirement_policy(record).available);
    record.state=RecoveryState::bound;record.binding_id="{01234567-89ab-cdef-0123-456789abcdef}";require(decode_recovery(encode_recovery(record))==record);
    require(!recovery_state_allows_restore(record).available);
    auto released=record;released.state=RecoveryState::released;require(recovery_state_allows_restore(released).available);
    require(!preexport_retirement_policy(released).available);
    released.state=RecoveryState::restored;require(recovery_state_allows_restore(released).available);
    released.state=RecoveryState::offline;require(!recovery_state_allows_restore(released).available);
    require(valid_recovery_transition(RecoveryState::prepared,RecoveryState::offline));
    require(valid_recovery_transition(RecoveryState::bound,RecoveryState::ejected));
    require(valid_recovery_transition(RecoveryState::ejected,RecoveryState::released));
    require(!valid_recovery_transition(RecoveryState::bound,RecoveryState::released));
    auto ejected=record;ejected.state=RecoveryState::ejected;
    PendingRecovery selected;selected.journal_id=L"protected-journal";selected.record=ejected;
    HostDevice bound{L"USB\\TEST","1-2",L"{01234567-89ab-cdef-0123-456789abcdef}",false,false};
    require(ejected_binding_adoption_policy(selected,selected,bound,L"usb\\test").available);
    auto changed=selected;changed.record->state=RecoveryState::bound;require(!ejected_binding_adoption_policy(changed,changed,bound,L"USB\\TEST").available);
    changed=selected;changed.record->owner[0]^=1;require(!ejected_binding_adoption_policy(selected,changed,bound,L"USB\\TEST").available);
    changed=selected;changed.record.reset();require(!ejected_binding_adoption_policy(selected,changed,bound,L"USB\\TEST").available);
    auto foreign=bound;foreign.binding_id=L"{ffffffff-89ab-cdef-0123-456789abcdef}";require(!ejected_binding_adoption_policy(selected,selected,foreign,L"USB\\TEST").available);
    foreign=bound;foreign.attached=true;require(!ejected_binding_adoption_policy(selected,selected,foreign,L"USB\\TEST").available);
    foreign=bound;foreign.forced=true;require(!ejected_binding_adoption_policy(selected,selected,foreign,L"USB\\TEST").available);
    foreign=bound;foreign.bus_id="1-3";require(!ejected_binding_adoption_policy(selected,selected,foreign,L"USB\\TEST").available);
    require(!ejected_binding_adoption_policy(selected,selected,bound,L"USB\\OTHER").available);
    auto interrupted=selected;interrupted.record->state=RecoveryState::bound;interrupted.record->version=2;
    require(interrupted_return_policy(*interrupted.record).available);
    require(absent_release_policy(*interrupted.record).available);
    auto invalidAbsent=*interrupted.record;invalidAbsent.version=1;require(!absent_release_policy(invalidAbsent).available);
    invalidAbsent=*interrupted.record;invalidAbsent.state=RecoveryState::ejected;require(!absent_release_policy(invalidAbsent).available);
    invalidAbsent=*interrupted.record;invalidAbsent.binding_id.clear();require(!absent_release_policy(invalidAbsent).available);
    invalidAbsent=*interrupted.record;invalidAbsent.owner.fill(0);require(!absent_release_policy(invalidAbsent).available);
    require(interrupted_binding_adoption_policy(interrupted,interrupted,bound,L"USB\\TEST",true).available);
    require(!interrupted_binding_adoption_policy(interrupted,interrupted,bound,L"USB\\TEST",false).available);
    require(!interrupted_binding_adoption_policy(interrupted,selected,bound,L"USB\\TEST",true).available);
    require(!interrupted_binding_adoption_policy(interrupted,interrupted,foreign,L"USB\\TEST",true).available);
    require(!interrupted_binding_adoption_policy(interrupted,interrupted,bound,L"USB\\OTHER",true).available);
    auto invalidInterrupted=*interrupted.record;invalidInterrupted.version=1;require(!interrupted_return_policy(invalidInterrupted).available);
    invalidInterrupted=*interrupted.record;invalidInterrupted.state=RecoveryState::ejected;require(!interrupted_return_policy(invalidInterrupted).available);
    invalidInterrupted=*interrupted.record;invalidInterrupted.prior_attributes|=1;require(!interrupted_return_policy(invalidInterrupted).available);
    invalidInterrupted=*interrupted.record;invalidInterrupted.fingerprint.fill(0);require(!interrupted_return_policy(invalidInterrupted).available);
    invalidInterrupted=*interrupted.record;invalidInterrupted.binding_id.clear();require(!interrupted_return_policy(invalidInterrupted).available);
    // A crash after verified eject but before unbind preserves sufficient proof
    // for explicit recovery to finish only the journal's exact owned GUID.
    require(decode_recovery(encode_recovery(ejected))==ejected);
    require(recovery_state_allows_restore(ejected).available);
    auto invalidEject=ejected;invalidEject.binding_id.clear();rejects([&]{encode_recovery(invalidEject);});
    require(!valid_recovery_transition(RecoveryState::binding_intent,RecoveryState::restored));
    require(!valid_recovery_transition(RecoveryState::bound,RecoveryState::restored));
    require(!valid_recovery_transition(RecoveryState::prepared,RecoveryState::bound));
    require(!valid_recovery_transition(RecoveryState::released,RecoveryState::bound));
    require(!valid_recovery_transition(static_cast<RecoveryState>(200),RecoveryState::restored));
    bytes[5]=3;rejects([&]{decode_recovery(bytes);});bytes=encode_recovery(record);bytes[70]=1;rejects([&]{decode_recovery(bytes);});
    bytes=encode_recovery(record);bytes.pop_back();rejects([&]{decode_recovery(bytes);});
    auto bad=record;bad.bus_id="../../disk";rejects([&]{encode_recovery(bad);});bad=record;bad.binding_id="arbitrary-path";rejects([&]{encode_recovery(bad);});
    bad=record;bad.owner.fill(0);rejects([&]{encode_recovery(bad);});bad=record;bad.fingerprint.fill(0);rejects([&]{encode_recovery(bad);});
    bad=record;bad.state=RecoveryState::prepared;rejects([&]{encode_recovery(bad);});bad=record;bad.binding_id.clear();rejects([&]{encode_recovery(bad);});
    std::cout<<"USB recovery serialization/policy checks passed (no registry or devices)\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
