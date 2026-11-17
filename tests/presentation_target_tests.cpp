#include "hle/presentation_target.h"

#include <array>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>

namespace p = hle::presentation;
namespace {
void require(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
bool same(p::Bindings a, p::Bindings b) {
    return a.draw == b.draw && a.read == b.read && a.renderbuffer == b.renderbuffer;
}
bool same(p::Target a, p::Target b) {
    return a.fbo==b.fbo && a.color==b.color && a.depth==b.depth && a.normal==b.normal && a.veil==b.veil &&
           a.depth_format==b.depth_format && a.samples==b.samples;
}

// No GL, SDL, game/JIT, assets or driver execution. The synthetic driver's
// completeness rule models the reported stale-attachment sample mismatch.
struct Driver {
    struct Buffer { int samples; uint32_t format; };
    struct Framebuffer { std::array<uint32_t,3> colors{}; uint32_t depth=0, texture=0; };
    std::map<uint32_t,Buffer> buffers;
    std::map<uint32_t,Framebuffer> framebuffers;
    p::Bindings bound{900,901,902};  // unrelated caller bindings
    uint32_t next=1;
    int fail_allocation=-1, allocations=0;
    bool round_color=false, mismatch_depth=false, mismatch_normal=false;
    bool unsupported_core=false, unsupported_normal=false, unsupported_veil=false;
    std::vector<int> checked_samples;
    std::set<uint32_t> deleted;

    p::Bindings bindings() { return bound; }
    void restore(p::Bindings value) { bound=value; }
    uint32_t framebuffer() {
        if (++allocations == fail_allocation) return 0;
        uint32_t id=next++;framebuffers[id]={};return id;
    }
    void delete_framebuffer(uint32_t id) {
        require(framebuffers.erase(id)==1,"framebuffer deleted once");deleted.insert(id);
        if(bound.draw==id)bound.draw=0;if(bound.read==id)bound.read=0;
    }
    void bind_framebuffer(uint32_t id) { bound.draw=bound.read=id; }
    uint32_t renderbuffer(int count,uint32_t format,int width,int height) {
        require(width==1280 && height==720,"candidate retains dimensions");
        if (++allocations == fail_allocation) return 0;
        if(count && round_color && format==p::rgba8 && count<4)count=4;
        if(mismatch_depth && format==p::depth32f_stencil8)count=count?count+2:2;
        if(mismatch_normal && format==p::rgb10_a2)count=count?count+2:2;
        uint32_t id=next++;buffers[id]={count,format};bound.renderbuffer=id;return id;
    }
    void delete_renderbuffer(uint32_t id) {
        require(buffers.erase(id)==1,"renderbuffer deleted once");deleted.insert(id);
        if(bound.renderbuffer==id)bound.renderbuffer=0;
    }
    int samples(uint32_t id) { bound.renderbuffer=id;return buffers.at(id).samples; }
    void attach_color(int slot,uint32_t id) { framebuffers.at(bound.draw).colors.at(slot)=id; }
    void attach_depth(uint32_t id) { framebuffers.at(bound.draw).depth=id; }
    void attach_texture(uint32_t id) { framebuffers.at(bound.draw).texture=id; }
    uint32_t status() {
        auto& f=framebuffers.at(bound.draw);
        if(unsupported_core)return 0x8CDD;
        if(!f.depth || (!f.texture && !f.colors[0]))return 0x8CD6;
        int count=f.colors[0]?buffers.at(f.colors[0]).samples:0;
        checked_samples.push_back(count);
        if(buffers.at(f.depth).samples!=count)return p::incomplete_multisample;
        for(auto id:f.colors)if(id && buffers.at(id).samples!=count)return p::incomplete_multisample;
        if(unsupported_normal && f.colors[1])return 0x8CDD;
        if(unsupported_veil && f.colors[2])return 0x8CDD;
        return p::complete;
    }
    uint32_t status(uint32_t fbo) {
        auto old=bound;bind_framebuffer(fbo);uint32_t value=status();bound=old;return value;
    }
};

p::Result change(Driver& gl,p::Target& live,int count,uint32_t format=p::depth32f_stencil8) {
    return p::change(gl,live,700,1280,720,count,format);
}

void transitions() {
    Driver gl;p::Target live;
    auto bindings=gl.bindings();
    for(int repetition=0;repetition<12;++repetition) {
        for(int count:{4,0,2,8,4,8,2,0,4}) {
            auto old=live;
            require(change(gl,live,count).accepted,"all supported transitions accepted");
            require(live.samples==count && live.normal && live.veil,"exact sample count and effects retained");
            require(live.depth_format==p::depth32f_stencil8,"no bogus 24-bit downgrade");
            require(gl.status(live.fbo)==p::complete,"every published framebuffer complete");
            require(same(gl.bindings(),bindings),"separate caller bindings preserved");
            require(gl.framebuffers.size()==1 && gl.buffers.size()==size_t(count?4:3),"no per-transition object leak");
            if(old.fbo)require(gl.deleted.count(old.fbo)==1,"old framebuffer retired only on success");
        }
    }
    p::release(gl,live);
    require(gl.framebuffers.empty() && gl.buffers.empty(),"explicit resource cleanup");
}

void stale_attachment_reproduction() {
    Driver gl;
    uint32_t fbo=gl.framebuffer();gl.bind_framebuffer(fbo);
    auto color=gl.renderbuffer(2,p::rgba8,1280,720);gl.attach_color(0,color);
    auto depth=gl.renderbuffer(2,p::depth32f_stencil8,1280,720);gl.attach_depth(depth);
    auto normal=gl.renderbuffer(2,p::rgb10_a2,1280,720);gl.attach_color(1,normal);
    auto old_veil=gl.renderbuffer(4,p::rgba8,1280,720);gl.attach_color(2,old_veil);
    require(gl.status()==p::incomplete_multisample,"old 4x veil poisons a new 2x completeness check");
    gl.attach_color(1,0);
    require(gl.status()==p::incomplete_multisample,"dropping normals does not remove the OLD veil mismatch");
    gl.attach_color(2,0);
    require(gl.status()==p::complete,"removing stale veil exposes supported core");
}

void rejection_preserves_live_target() {
    for(int failure=1;failure<=5;++failure) {
        Driver gl;p::Target live;require(change(gl,live,4).accepted,"initial target");
        auto old=live;auto bindings=gl.bindings();size_t objects=gl.buffers.size();
        gl.fail_allocation=gl.allocations+failure;
        auto result=change(gl,live,8);
        require(!result.accepted,"candidate allocation failure refused");
        require(same(old,live) && same(gl.bindings(),bindings),"old target/metadata/bindings not mutated");
        require(gl.status(live.fbo)==p::complete,"working image survives allocation failure");
        require(gl.buffers.size()==objects && gl.framebuffers.size()==1,"failed partial target cleaned up");
    }
    for(int failure=0;failure<4;++failure) {
        Driver gl;p::Target live;require(change(gl,live,4).accepted,"initial target");auto old=live;
        if(failure==0)gl.unsupported_core=true;
        if(failure==1)gl.unsupported_normal=true;
        if(failure==2)gl.unsupported_veil=true;
        if(failure==3)gl.mismatch_depth=true;
        require(!change(gl,live,8).accepted,"unsupported proposal refused instead of dropping existing quality");
        require(same(old,live),"unsupported candidate never published");
        gl.unsupported_core=gl.unsupported_normal=gl.unsupported_veil=gl.mismatch_depth=false;
        require(gl.status(live.fbo)==p::complete,"previous valid attachments untouched");
        require(gl.framebuffers.size()==1 && gl.buffers.size()==4,"unsupported proposal cleaned up");
    }
}

void initial_fallbacks() {
    Driver gl;p::Target live;
    gl.mismatch_depth=true;
    auto mismatch=change(gl,live,4);
    require(!mismatch.accepted && mismatch.status==p::incomplete_multisample && !live.fbo,"initial sample mismatch rejected");
    require(gl.framebuffers.empty() && gl.buffers.empty(),"failed initial setup owns no resources");
    gl.mismatch_depth=false;
    gl.unsupported_core=true;
    require(!change(gl,live,0).accepted && !live.fbo,"failed MSAA-off cannot be claimed as success");
    gl.unsupported_core=false;gl.unsupported_normal=true;
    require(change(gl,live,0,p::depth24_stencil8).accepted,"initial optional unsupported normal fallback retained");
    require(!live.normal && !live.veil && live.depth_format==p::depth24_stencil8,"fallback metadata truthful");
    require(gl.status(live.fbo)==p::complete && gl.buffers.size()==1,"unsupported auxiliary objects detached/freed");
    p::release(gl,live);
    gl.unsupported_normal=false;gl.unsupported_veil=true;
    require(change(gl,live,2).accepted && live.normal && !live.veil,"initial fog-only capability fallback");
    require(gl.status(live.fbo)==p::complete && gl.buffers.size()==3,"normal feature remains available");
}

void actual_samples_and_bindings() {
    Driver gl;p::Target live;gl.round_color=true;
    require(change(gl,live,2).accepted && live.samples==4,"driver-rounded color count becomes actual configuration");
    require(gl.samples(live.depth)==4 && gl.samples(live.normal)==4 && gl.samples(live.veil)==4,"auxiliaries follow ACTUAL color sample count");
    gl.restore({live.fbo,901,live.depth});
    require(change(gl,live,8).accepted,"transition with active live framebuffer/renderbuffer");
    require(same(gl.bindings(),{live.fbo,901,live.depth}),"old live draw/rb bindings remap; unrelated read remains separate");
    gl.restore({900,live.fbo,live.color});
    require(change(gl,live,0).accepted,"transition to texture color");
    require(same(gl.bindings(),{900,live.fbo,0}),"read remapped; retired MSAA color binding becomes zero");
    auto old=live;gl.mismatch_normal=true;
    require(!change(gl,live,2).accepted && same(old,live),"incompatible auxiliary sample count refused");
    require(same(gl.bindings(),{900,live.fbo,0}),"failed proposal preserves split bindings");
}
}
int main() {
    try {
        stale_attachment_reproduction();transitions();rejection_preserves_live_target();
        initial_fallbacks();actual_samples_and_bindings();
        std::cout<<"Presentation framebuffer transition/rollback tests passed; synthetic driver only\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
