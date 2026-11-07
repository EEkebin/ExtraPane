#include <extrapane/usb_tunnel.hpp>
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace extrapane::usb::tunnel {
namespace {
void require(bool value,const char* text){if(!value)throw std::invalid_argument(text);}
void put(std::uint8_t* out,std::uint64_t value,unsigned size){while(size){out[--size]=static_cast<std::uint8_t>(value);value>>=8;}}
std::uint64_t get(const std::uint8_t* in,unsigned size){std::uint64_t value=0;while(size--)value=(value<<8)|*in++;return value;}
void valid(const Frame& f){require(f.credit<=capacity&&f.payload.size()<=maximum_payload&&(!f.stop||(f.payload.empty()&&f.control==Control::none))&&static_cast<unsigned>(f.control)<=8,"Invalid USB tunnel frame limits");}
}
void validate_control(Control c,bool importer){require(c==Control::none||(importer?c==Control::return_request:(c>=Control::return_busy&&c<=Control::return_failed)),"Unexpected USB return control direction");}
Header encode(const Frame& f){valid(f);Header h{};h[0]='E';h[1]='P';h[2]='U';h[3]='T';h[4]=3;h[5]=f.stop?1:0;h[6]=static_cast<std::uint8_t>(f.control);put(h.data()+8,f.sequence,8);put(h.data()+16,f.payload.size(),4);put(h.data()+20,f.credit,4);return h;}
Frame decode(const Header& h){require(h[0]=='E'&&h[1]=='P'&&h[2]=='U'&&h[3]=='T'&&h[4]==3&&h[5]<=1&&h[6]<=8&&!h[7],"Unsupported USB tunnel frame");Frame f;f.control=static_cast<Control>(h[6]);f.sequence=get(h.data()+8,8);f.stop=h[5]!=0;f.credit=static_cast<std::uint32_t>(get(h.data()+20,4));const auto size=get(h.data()+16,4);require(size<=maximum_payload&&f.credit<=capacity&&(!f.stop||(!size&&f.control==Control::none)),"Oversized USB tunnel frame");f.payload.resize(static_cast<std::size_t>(size));return f;}
Pump::Pump(std::string bus):bus_(std::move(bus)),gate_open_(bus_.empty()){
    require(bus_.size()<32&&std::all_of(bus_.begin(),bus_.end(),[](unsigned char c){return c>=33&&c<=126;}),"Invalid selected USB bus identity");queue_.reserve(capacity);
}
std::size_t Pump::read_limit()const{return stopped_?0:std::min<std::size_t>(maximum_payload,peer_credit_);}
Frame Pump::next(std::span<const std::uint8_t> bytes,bool stop,Control control){
    require(!stopped_&&sent_!=std::numeric_limits<std::uint64_t>::max(),"USB tunnel ended or sequence exhausted");require(bytes.size()<=read_limit()&&(!stop||bytes.empty()),"USB tunnel send exceeds peer credit");
    Frame frame;frame.sequence=sent_;frame.stop=stop;frame.control=control;frame.credit=static_cast<std::uint32_t>(capacity-(queue_.size()-offset_));frame.payload.assign(bytes.begin(),bytes.end());
    valid(frame);++sent_;advertised_=frame.credit;if(stop)stopped_=true;return frame;
}
void Pump::accept(Frame f){
    valid(f);require(!stopped_&&received_!=std::numeric_limits<std::uint64_t>::max()&&f.sequence==received_,"USB tunnel sequence mismatch");require(f.payload.size()<=advertised_&&f.payload.size()<=capacity-(queue_.size()-offset_),"USB tunnel receive exceeds credit");
    ++received_;peer_credit_=f.credit;if(f.stop){stopped_=true;queue_.clear();offset_=0;return;}
    if(offset_){queue_.erase(queue_.begin(),queue_.begin()+offset_);offset_=0;}queue_.insert(queue_.end(),f.payload.begin(),f.payload.end());
    if(!gate_open_&&queue_.size()>=40){
        require(queue_[0]==1&&queue_[1]==0x11&&queue_[2]==0x80&&queue_[3]==3&&queue_[4]==0&&queue_[5]==0&&queue_[6]==0&&queue_[7]==0,"Only the selected USB/IP import is permitted; device lists are not forwarded");
        for(std::size_t i=0;i<32;++i)require(queue_[8+i]==(i<bus_.size()?static_cast<std::uint8_t>(bus_[i]):0),"USB/IP import does not match the selected device");gate_open_=true;
    }
}
std::span<const std::uint8_t> Pump::pending()const{return gate_open_&&!stopped_?std::span<const std::uint8_t>(queue_).subspan(offset_):std::span<const std::uint8_t>{};}
void Pump::consume(std::size_t count){require(count<=pending().size(),"USB tunnel consumed unavailable bytes");offset_+=count;if(offset_==queue_.size()){queue_.clear();offset_=0;}}
}
