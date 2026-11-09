#include <dm/camera_pipe.hpp>
#include <future>
#include <iostream>
#include <stdexcept>
using namespace dm::camera;
using namespace std::chrono_literals;
void check(bool value){if(!value)throw std::runtime_error("Camera pipe check failed");}
int main(){try{
    FramePublisher publisher({4,4,30});publisher.start();
    std::promise<bool> delivered;auto result=delivered.get_future();
    FrameSubscriber subscriber(publisher.name(),{4,4,30},[&](auto pixels,std::int64_t timestamp){
        delivered.set_value(pixels.size()==24&&pixels.front()==23&&pixels.back()==23&&timestamp==77);
    });
    std::vector<std::uint8_t> pixels(24,23);publisher.submit(pixels,77);subscriber.start();
    if(result.wait_for(2s)!=std::future_status::ready)throw std::runtime_error("Delivery timeout: publisher="+publisher.last_error()+" subscriber="+subscriber.last_error());check(result.get());
    // Both ends are blocked waiting for the next frame/request. Stop must cancel
    // pipe I/O and wake the publisher without a frame, UI, socket or device.
    subscriber.stop();check(!subscriber.failed());publisher.stop();publisher.stop();
    bool rejected=false;try{FrameSubscriber invalid(L"\\\\remote\\pipe\\camera",{4,4,30},[](auto,auto){});}catch(const std::invalid_argument&){rejected=true;}check(rejected);
    FramePublisher mismatch({4,4,30});mismatch.start();mismatch.submit(pixels,99);
    FrameSubscriber wrong(mismatch.name(),{8,4,30},[](auto,auto){throw std::runtime_error("Invalid geometry delivered");});wrong.start();
    const auto deadline=std::chrono::steady_clock::now()+2s;
    while(!wrong.failed()&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(5ms);
    check(wrong.failed());wrong.stop();mismatch.stop();
    std::cout<<"Local camera pipe delivery, geometry validation and cancellation passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
