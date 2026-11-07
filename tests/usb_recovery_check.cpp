#include <dm/usb_recovery.hpp>
#include <iostream>
#include <stdexcept>
using namespace dm::usb;
void require(bool value){if(!value)throw std::runtime_error("USB recovery policy check failed");}
template<class F>void rejects(F f){try{f();}catch(const std::exception&){return;}throw std::runtime_error("Invalid recovery record accepted");}
int main(){try{
    RecoveryRecord record;record.disk_number=7;record.owner[0]=5;record.fingerprint[0]=8;record.prior_attributes=2;
    auto bytes=encode_recovery(record);require(bytes.size()==72);require(decode_recovery(bytes)==record);
    require(recovery_state_allows_restore(record).available);
    record.state=RecoveryState::binding_intent;record.bus_id="1-2";require(decode_recovery(encode_recovery(record))==record);
    require(!recovery_state_allows_restore(record).available);
    record.state=RecoveryState::bound;record.binding_id="{01234567-89ab-cdef-0123-456789abcdef}";require(decode_recovery(encode_recovery(record))==record);
    require(!recovery_state_allows_restore(record).available);
    auto released=record;released.state=RecoveryState::released;require(recovery_state_allows_restore(released).available);
    released.state=RecoveryState::restored;require(recovery_state_allows_restore(released).available);
    released.state=RecoveryState::offline;require(!recovery_state_allows_restore(released).available);
    require(valid_recovery_transition(RecoveryState::prepared,RecoveryState::offline));
    require(valid_recovery_transition(RecoveryState::bound,RecoveryState::ejected));
    require(valid_recovery_transition(RecoveryState::ejected,RecoveryState::released));
    require(!valid_recovery_transition(RecoveryState::bound,RecoveryState::released));
    auto ejected=record;ejected.state=RecoveryState::ejected;
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
    bytes[5]=2;rejects([&]{decode_recovery(bytes);});bytes=encode_recovery(record);bytes[70]=1;rejects([&]{decode_recovery(bytes);});
    bytes=encode_recovery(record);bytes.pop_back();rejects([&]{decode_recovery(bytes);});
    auto bad=record;bad.bus_id="../../disk";rejects([&]{encode_recovery(bad);});bad=record;bad.binding_id="arbitrary-path";rejects([&]{encode_recovery(bad);});
    bad=record;bad.owner.fill(0);rejects([&]{encode_recovery(bad);});bad=record;bad.fingerprint.fill(0);rejects([&]{encode_recovery(bad);});
    bad=record;bad.state=RecoveryState::prepared;rejects([&]{encode_recovery(bad);});bad=record;bad.binding_id.clear();rejects([&]{encode_recovery(bad);});
    std::cout<<"USB recovery serialization/policy checks passed (no registry or devices)\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
