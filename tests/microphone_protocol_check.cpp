#include <extrapane/microphone_protocol.hpp>
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace extrapane;
using namespace extrapane::microphone_wire;
void require(bool value){if(!value)throw std::runtime_error("Microphone protocol check failed");}
template<class F>void rejects(F action){try{action();}catch(const std::exception&){return;}throw std::runtime_error("Invalid microphone protocol accepted");}
int main(){try{
    const media::Binding binding{9,17};auto offer=encode_offer(binding);require(decode_offer(offer)==binding);
    offer[6]=2;rejects([&]{decode_offer(offer);});offer=encode_offer(binding);offer[14]=1;rejects([&]{decode_offer(offer);});
    rejects([]{encode_offer({0,1});});rejects([]{decode_offer({});});
    Frame frame;frame.binding=binding;frame.position=480;frame.muted=true;frame.pcm.fill(55);
    auto h=encode_header(frame);auto decoded=decode_header(h);require(decoded.muted&&decoded.position==480&&decoded.binding==binding);
    h[7]=4;rejects([&]{decode_header(h);});
    std::vector<std::uint8_t> bytes;std::size_t position=0;
    auto read=[&](std::span<std::uint8_t> out){if(position+out.size()>bytes.size())throw std::runtime_error("EOF");std::copy_n(bytes.data()+position,out.size(),out.data());position+=out.size();};
    auto write=[&](std::span<const std::uint8_t> in){bytes.insert(bytes.end(),in.begin(),in.end());};
    Channel sender(binding,read,write);sender.send(frame);require(bytes.size()==1000);
    Channel receiver(binding,read,write);auto received=receiver.receive();for(auto b:received.pcm)require(b==0);
    rejects([&]{sender.send(frame);});frame.sequence=1;rejects([&]{sender.send(frame);});
    bytes.clear();position=0;frame.sequence=0;frame.binding.generation++;Channel wrong(binding,read,write);rejects([&]{wrong.send(frame);});
    bytes.assign(39,0);position=0;Channel shortRead(binding,read,write);rejects([&]{shortRead.receive();});
    std::cout<<"Microphone wire checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
