#pragma once
#include <memory>
#include <string>
namespace dm::mac::peer {
struct DisplayOptions {unsigned port=0,width=1920,height=1080,refresh=60,monitor=0;bool existing=false,input=false,exact=false;std::string credential;};
class PeerDisplayWindow {
 struct State;std::shared_ptr<State> state_;
public:
 explicit PeerDisplayWindow(DisplayOptions);
 ~PeerDisplayWindow();
 void stop();bool done()const;
};
}
