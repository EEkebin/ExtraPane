#include <extrapane/speaker_jitter.hpp>
#include <iostream>
#include <stdexcept>
#include <source_location>
using namespace extrapane::speaker_wire;
void check(bool v,const std::source_location at=std::source_location::current()){if(!v)throw std::runtime_error("Speaker jitter check failed at line "+std::to_string(at.line()));}
Frame frame(std::int16_t left,std::int16_t right){Frame f;for(std::size_t i=0;i<sample_count;++i)for(unsigned c=0;c<2;++c){const auto value=static_cast<std::uint16_t>(c?right:left);const auto at=(i*2+c)*2;f.pcm[at]=value&255;f.pcm[at+1]=value>>8;}return f;}
int main(){try{
    JitterBuffer buffer;const auto source=frame(1000,-2000);std::array<std::int16_t,96> output{};
    for(unsigned i=0;i<3;++i){buffer.accept(source);buffer.render(output);for(auto sample:output)check(sample==0);}
    buffer.accept(source);
    for(unsigned tick=0;tick<10000;++tick){
        if(tick&&tick%30==0)for(unsigned i=0;i<3;++i)buffer.accept(source);
        buffer.render(output);
        if(tick>=2)for(unsigned i=0;i<output.size();i+=2){check(output[i]==1000);check(output[i+1]==-2000);}
    }
    check(buffer.statistics().underruns==0);check(buffer.statistics().dropped_frames==0);
    std::array<std::int16_t,JitterBuffer::capacity_frames*2+192> drain{};
    buffer.render(drain);check(buffer.statistics().underruns==1);check(buffer.statistics().target_frames==2880);
    for(unsigned repeat=0;repeat<3;++repeat){for(unsigned i=0;i<8;++i)buffer.accept(source);buffer.render(drain);}
    check(buffer.statistics().target_frames==JitterBuffer::max_target_frames);
    buffer.reset();for(unsigned i=0;i<12;++i)buffer.accept(source);buffer.accept(frame(3000,-4000));
    check(buffer.statistics().queued_frames==JitterBuffer::initial_frames);
    std::array<std::int16_t,JitterBuffer::initial_frames*2> recent{};buffer.render(recent);
    check(recent[recent.size()-2]==3000&&recent.back()==-4000);
    auto silence=source;silence.muted=true;buffer.accept(silence);buffer.render(output);for(auto sample:output)check(sample==0);
    buffer.accept(source);buffer.render(output);for(auto sample:output)check(sample==0);
    auto gap=frame(123,-456);gap.discontinuity=true;buffer.accept(gap);check(buffer.statistics().queued_frames==480);buffer.render(output);for(auto sample:output)check(sample==0);
    for(unsigned i=0;i<3;++i)buffer.accept(frame(123,-456));buffer.render(recent);check(recent.back()==-456);
    buffer.reset();buffer.render(output);for(auto sample:output)check(sample==0);
    JitterBuffer independent;for(unsigned i=0;i<4;++i)independent.accept(source);buffer.reset();independent.render(recent);check(recent.back()==-2000);
    // Mute during startup fade cannot leak the rest of its buffered samples.
    JitterBuffer fading;for(unsigned i=0;i<4;++i)fading.accept(frame(-32768,32767));
    fading.render(output);check(output[0]<0&&output[1]>0);
    silence.muted=true;fading.accept(silence);fading.render(output);for(auto sample:output)check(sample==0);
    std::array<std::int16_t,3> malformed{1,2,3};fading.render(malformed);check(malformed==std::array<std::int16_t,3>{});
    std::cout<<"Stereo jitter timing, mute, discontinuity, overflow and isolation checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
