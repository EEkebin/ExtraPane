#include "../platform/linux/output_slot.hpp"
#include <array>
#include <iostream>
#include <vector>
using dm::linux_client::reserveOutputSlot;
void need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
struct Node {bool foreign=false,busy=false;int failure=0,opened=0,closed=0,configured=0;};
struct Lease {
    Node& node;int index;
    Lease(Node& value,int id):node(value),index(id){node.busy=true;++node.opened;}
    ~Lease(){node.busy=false;++node.closed;}
};
int main(){try{
    std::array<Node,5> nodes{};nodes[0].foreign=true;nodes[1].busy=true;
    nodes[2].failure=1;nodes[3].failure=2;
    std::vector<int> candidates{0,1,2,3,4};
    auto acquire=[&](int id)->std::unique_ptr<Lease>{
        auto& n=nodes[id];if(n.foreign||n.busy)return {};
        return std::make_unique<Lease>(n,id);
    };
    auto configure=[](Lease& lease){
        ++lease.node.configured;
        if(lease.node.failure==1)throw std::runtime_error("resolution mismatch");
        if(lease.node.failure==2)throw std::runtime_error("frame rate mismatch");
    };
    auto selected=reserveOutputSlot<Lease>(candidates,acquire,configure);
    need(selected->index==4,"Must try later compatible slot after geometry/FPS failure");
    need(nodes[0].configured==0&&nodes[1].configured==0,"Foreign or leased slot was configured");
    need(nodes[2].closed==1&&nodes[3].closed==1&&!nodes[2].busy&&!nodes[3].busy,"Rejected slot lease leaked");
    bool rejected=false;try{reserveOutputSlot<Lease>(candidates,acquire,configure);}catch(const std::runtime_error& e){rejected=std::string(e.what()).find("frame rate mismatch")!=std::string::npos;}
    need(rejected&&nodes[4].opened==1,"Concurrent session reused leased node or lost failure reason");
    selected.reset();need(nodes[4].closed==1&&!nodes[4].busy,"Disconnect retained output lease");
    selected=reserveOutputSlot<Lease>(candidates,acquire,configure);need(selected->index==4,"Released slot cannot be reused");selected.reset();
    nodes[2].failure=0;selected=reserveOutputSlot<Lease>(candidates,acquire,configure);need(selected->index==2,"First compatible slot was skipped");selected.reset();
    for(auto& n:nodes)n.foreign=true;
    rejected=false;try{reserveOutputSlot<Lease>(candidates,acquire,configure);}catch(const std::runtime_error& e){rejected=std::string(e.what()).find("No writable, unused")!=std::string::npos;}
    need(rejected,"No eligible slots did not report unavailable");
    std::cout<<"Camera slot failover, exclusion, reservation and release checks passed (fake handles).\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
