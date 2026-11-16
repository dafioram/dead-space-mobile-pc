// Synthetic positions/indices only. Executes production diagnostics and capture
// policy, never game code, SDL, GL, a driver or filesystem capture fixtures.
#include "geometry_capture.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {size_t check_warnings=0;}
namespace logging { void write(char level,const char*,...) {if(level=='W')++check_warnings;} }
namespace {
using namespace geometry;
void require(bool okay,const char* why){if(!okay)throw std::runtime_error(why);}
bool close(double a,double b){return std::abs(a-b)<1e-5;}
size_t probes=0;
bool readable=true;
bool mapped(uint32_t page){++probes;return readable && page<0x10000;}
template<class T,size_t N> Source source(const T(&data)[N]) {
    return {reinterpret_cast<const uint8_t*>(data),sizeof(data),0,false,nullptr};
}
float vertices[]={0,0,0, 4,0,0, 0,3,0, 4,3,0};
uint16_t indices[]={0,1,2, 1,3,2};
Draw draw() {
    Draw d;d.indexed=true;d.count=6;d.mode=triangles;d.index_type=unsigned_short;d.indices=source(indices);
    d.position.enabled=true;d.position.components=3;d.position.type=float_type;d.position.source=source(vertices);d.position.pointer=0x4000;
    d.state.viewport={0,0,1280,720};d.state.depth_test=true;d.caller=0x10001000;
    return d;
}
void formats_and_topology() {
    auto d=draw();auto s=inspect(d);
    require(s.triangle_count==2 && s.inspected==6 && close(s.longest.edge,5),"float/indexed triangle decoding wrong");
    require(s.min_index==0 && s.max_index==3 && s.local_max[0]==4 && s.local_max[1]==3,"bounds/index range wrong");
    require(flags(s)=="no-checked-error" && s.digest!=0,"valid geometry falsely marked malformed");
    auto original=s;
    d.state.native=true;s=inspect(d);require(s.digest==original.digest&&s.longest.local==original.longest.local,"renderer selection changed inspection");
    d.state.modelview[0]=2;d.state.modelview[5]=2;d.state.modelview[12]=17;
    s=inspect(d);require(close(s.longest.edge,10)&&s.local_min[0]==0&&s.eye_min[0]==17,"local/eye transforms mixed");
    require(s.digest==original.digest,"geometry digest changed because camera/modelview changed");
    int16_t shorts[]={0,0,0, 4,0,0, 0,3,0};d=draw();d.count=3;d.position.type=short_type;d.position.source=source(shorts);
    s=inspect(d);require(close(s.longest.edge,5),"signed short coordinates wrong");
    int32_t fixed_points[]={0,0,0, 4*65536,0,0, 0,3*65536,0};
    d.position.type=fixed;d.position.source=source(fixed_points);s=inspect(d);require(close(s.longest.edge,5),"16.16 fixed coordinates wrong");
    int8_t bytes[]={0,0,0, 4,0,0, 0,3,0};
    d.position.type=byte;d.position.source=source(bytes);s=inspect(d);require(close(s.longest.edge,5),"signed byte coordinates wrong");
    struct Interleaved {float x,y,z,pad;};Interleaved packed[]={{0,0,0,99},{4,0,0,99},{0,3,0,99}};
    d.position.type=float_type;d.position.stride=sizeof(Interleaved);d.position.source=source(packed);s=inspect(d);require(close(s.longest.edge,5),"interleaved stride wrong");
    float two_d[]={0,0,4,0,0,3};d.position.components=2;d.position.stride=0;d.position.source=source(two_d);s=inspect(d);require(close(s.longest.edge,5),"2D vertex default z/w wrong");
    uint8_t small[]={0,1,2};d.indices=source(small);d.index_type=unsigned_byte;s=inspect(d);require(s.triangle_count==1,"8-bit indices wrong");
    uint32_t large[]={0,1,2};d.indices=source(large);d.index_type=unsigned_int;s=inspect(d);require(s.triangle_count==1,"32-bit indices wrong");
    d=draw();d.indexed=false;d.count=3;d.first=1;s=inspect(d);require(s.min_index==1&&s.max_index==3&&s.triangle_count==1,"glDrawArrays first/count wrong");
    d.first=0;d.count=4;d.mode=triangle_strip;s=inspect(d);require(s.triangle_count==2,"triangle strip assembly wrong");
    d.mode=triangle_fan;s=inspect(d);require(s.triangle_count==2&&s.longest.indices[0]==0,"triangle fan anchor lost");
    float strip_points[]={0,0,0, 4,0,0, 0,3,0, 8,7,0};d.position.source=source(strip_points);d.mode=triangle_strip;s=inspect(d);
    require(s.longest.number==1&&s.longest.indices==std::array<uint32_t,3>{2,1,3},"odd strip winding recorded incorrectly");
    float long_points[]={0,0,0, 1000,0,0, 0,0.001f,0};d=draw();d.count=3;d.position.source=source(long_points);
    s=inspect(d);require(s.thinnest.aspect>100000&&s.longest.edge>=1000&&!s.unreadable&&!s.bad_layout,"long thin candidate was treated as invalid");
    require(long_points[3]==1000,"inspector changed source geometry");
}
void unsafe_inputs() {
    auto d=draw();d.position.stride=-1;auto s=inspect(d);require(s.bad_layout&&s.inspected==0,"negative stride dereferenced");
    d=draw();d.position.components=99;s=inspect(d);require(s.bad_layout,"component overflow accepted");
    d=draw();d.position.type=0xdead;s=inspect(d);require(s.bad_layout,"unknown scalar width guessed");
    d=draw();d.index_type=float_type;s=inspect(d);require(s.unsupported&&!s.inspected,"unknown index type guessed");
    d=draw();d.indices.bytes=1;s=inspect(d);require(s.bad_index_span&&s.unreadable,"index buffer extent ignored");
    d=draw();uint32_t bad[]={0,1,0xffffffff};d.index_type=unsigned_int;d.indices=source(bad);d.count=3;s=inspect(d);
    require(s.unreadable&&!s.longest.valid,"out-of-range vertex fetched");
    d=draw();d.position.source.bytes=4;s=inspect(d);require(s.unreadable,"short vertex buffer accepted");
    d=draw();float nan_points[]={0,0,0, 1,0,0, 0,std::numeric_limits<float>::quiet_NaN(),0};
    d.position.source=source(nan_points);d.count=3;s=inspect(d);require(s.nonfinite&&!s.longest.valid,"non-finite triangle used in metrics");
    d=draw();d.state.modelview[0]=std::numeric_limits<float>::infinity();s=inspect(d);require(s.nonfinite,"non-finite matrix missed");
    float fan_points[]={std::numeric_limits<float>::quiet_NaN(),0,0, 1,0,0, 0,1,0, 2,2,0};
    d=draw();d.indexed=false;d.count=4;d.mode=triangle_fan;d.position.source=source(fan_points);s=inspect(d);
    require(s.nonfinite&&!s.triangle_count&&!s.longest.valid,"malformed fan silently acquired a different anchor");
    d=draw();d.count=INT32_MAX;s=inspect(d,4);require(s.limited&&s.inspected<=4&&s.bad_index_span,"work/count bound ignored");
    d=draw();d.color=d.position;d.color.source.bytes=1;s=inspect(d);require(s.unreadable_attributes==2,"enabled color range not inspected");
    d=draw();d.uv=d.position;d.uv.stride=-1;s=inspect(d);require(s.bad_attribute_layouts==4,"enabled UV layout not inspected");
    d=draw();d.position.source.guest=true;d.position.source.guest_address=0x4000;d.position.source.mapped=mapped;
    readable=false;probes=0;s=inspect(d);require(s.unreadable&&probes>0,"unmapped guest geometry read attempted");
    readable=true;d.position.source.guest_address=0xfffffff8;s=inspect(d);require(s.unreadable,"4-GiB wrap allowed");
    uint8_t out[16]{};Source src=source(vertices);
    require(!read(src,UINT64_MAX-1,out,8)&&!read(src,1,out,SIZE_MAX),"source arithmetic overflow accepted");
    d=draw();d.count=0;s=inspect(d);require(!s.inspected&&!s.longest.valid,"zero count read geometry");
    d.count=-1;s=inspect(d);require(!s.inspected,"negative count read geometry");
    d=draw();d.indexed=false;d.first=-1;s=inspect(d);require(s.bad_layout,"negative first accepted");
    d=draw();d.mode=1;s=inspect(d);require(s.unsupported&&!s.inspected,"line primitives interpreted as triangles");
}
void clipping() {
    State state;state.viewport={0,0,1280,720};float line[4];
    require(screen_edge({-2,0,0,1},{2,0,0,1},state,1280,720,line)&&close(line[0],0)&&close(line[2],1280),"left/right homogeneous clip wrong");
    require(!screen_edge({0,0,0,-1},{1,1,0,-1},state,1280,720,line),"behind-camera edge divided by w");
    require(screen_edge({0,0,-2,1},{0,1,0,1},state,1280,720,line),"near-plane crossing rejected");
    require(std::all_of(line,line+4,[](float n){return std::isfinite(n);}),"clipped overlay has NaN/inf");
    require(!screen_edge({0,0,0,0},{0,1,0,0},state,1280,720,line),"zero-w edge accepted");
    state.viewport[2]=0;require(!screen_edge({0,0,0,1},{1,1,0,1},state,1280,720,line),"zero viewport accepted");
}
void capture_policy() {
    Capture capture;auto d=draw();
    auto original=std::vector<float>(std::begin(vertices),std::end(vertices));
    d.position.source.guest=true;d.position.source.guest_address=0x4000;d.position.source.mapped=mapped;
    probes=0;capture.begin_frame();capture.observe(d,1280,720);capture.end_frame();require(!capture.retained_frames()&&!probes,"disabled capture read or retained geometry");d=draw();
    require(capture.command("/geom_on").find("ON")!=std::string::npos,"cannot arm capture");
    for(int frame=0;frame<180;frame++){capture.begin_frame();capture.observe(d,1280,720);capture.end_frame();}
    require(capture.retained_frames()==history_frames,"history not bounded");
    require(capture.mark().find("recorded")!=std::string::npos,"marker failed");
    require(capture.mark().find("pending")!=std::string::npos,"overlapping markers accepted");
    for(size_t frame=0;frame<post_frames;frame++){capture.begin_frame();d.state.native=frame%2;capture.observe(d,1280,720);capture.end_frame();}
    require(capture.choices()==1&&capture.queued_lines()<2048,"candidate dedup/output bound failed");
    auto log=capture.drain(1);require(log.size()==1,"incremental logging limit ignored");
    auto rest=capture.drain(4096);std::string report;for(const auto& text:rest)report+=text+'\n';
    require(report.find("REPORT END")!=std::string::npos&&report.find("owner=unknown")!=std::string::npos,"capture lacked report/ownership limitations");
    require(capture.command("/geom_focus_1").find("highlighting")!=std::string::npos,"candidate focus unavailable");
    capture.begin_frame();d.state.modelview[0]=0.1f;d.state.modelview[5]=0.1f;capture.observe(d,1280,720);capture.end_frame();
    require(capture.focus_seen()&&!capture.lines().empty()&&capture.lines().size()<=6,"focus overlay missing/unbounded");
    require(std::equal(original.begin(),original.end(),std::begin(vertices)),"capture changed guest geometry");
    capture.command("/geom_focus_0");require(!capture.focused(),"cannot clear highlight");
    require(capture.command("/geom_focus_9999999999999999999999999").find("integer")!=std::string::npos,"focus integer overflow accepted");
    require(capture.command("/geom_focus_1_bad").find("integer")!=std::string::npos,"focus trailing junk accepted");
    capture.scene_changed();require(!capture.retained_frames()&&!capture.choices()&&!capture.focused()&&capture.enabled(),"scene boundary retained stale choices or disarmed");
    capture.begin_frame();capture.observe(d,1280,720);capture.end_frame();capture.mark();
    // Reporting retains copied values, not Source pointers. Turn all guest
    // mappings inaccessible before finalizing the historical report.
    readable=false;capture.finish();require(!capture.drain(4096).empty(),"incomplete capture not flushed on shutdown");readable=true;
    capture.command("/geom_off");require(!capture.enabled()&&!capture.retained_frames(),"off did not release observer history");
    require(capture.command("/geom_nope").find("usage")!=std::string::npos,"unknown diagnostic command silently accepted");
    Capture budget;budget.command("/geom_on");budget.begin_frame();
    std::vector<float> many(max_draw_indices*3,0.25f);
    d=draw();d.indexed=false;d.count=int(max_draw_indices);d.position.source={reinterpret_cast<const uint8_t*>(many.data()),many.size()*sizeof(float)};
    for(int i=0;i<10;i++)budget.observe(d,1280,720);
    require(budget.current().scanned==max_frame_indices&&budget.current().limited>0,"per-frame budget unbounded or not exercised");budget.end_frame();
    Capture severe;severe.command("/geom_on");severe.begin_frame();check_warnings=0;
    d=draw();d.position.source={};
    for(int i=0;i<30;i++){d.caller=uint32_t(i);severe.observe(d,1280,720);}
    require(check_warnings==8,"severe check warnings not bounded per scene");severe.end_frame();
    // Repeated completed reports without a consumer must remain bounded.
    Capture output;output.command("/geom_on");
    for(int i=0;i<3000;i++)output.command("/geom_off");
    require(output.queued_lines()==2049,"output cap/overflow sentinel missing");
    Capture effect;effect.command("/geom_on");effect.begin_frame();d=draw();d.state.depth_test=false;d.state.projection[11]=-1;d.state.framebuffer=99;
    effect.observe(d,1280,720);require(effect.current().candidates.size()==1,"depth-disabled perspective effect filtered out");effect.end_frame();
    effect.mark();effect.finish();require(effect.ready(),"report completion not exposed");
    effect.command("/geom_focus_1");effect.begin_frame();d.state.framebuffer=99;effect.observe(d,1280,720);effect.end_frame();
    require(effect.focus_seen()&&effect.lines().empty(),"offscreen target got a misleading screen-space outline");
    Capture empty;empty.command("/geom_on");empty.mark();empty.finish();require(empty.ready()&&!empty.choices(),"empty report never became ready");
}
void fuzz() {
    uint32_t rng=123456;auto next=[&](){rng=rng*1664525+1013904223;return rng;};
    uint8_t data[512];
    for(int n=0;n<30000;n++) {
        for(auto& b:data)b=uint8_t(next()>>24);
        Draw d=draw();d.position.source=source(data);d.indices=source(data);
        d.position.type=std::array<uint32_t,8>{byte,unsigned_byte,short_type,unsigned_short,unsigned_int,float_type,fixed,0xdead}[next()%8];
        d.position.components=int(next()%8)-1;d.position.stride=int(next()%40)-2;
        d.index_type=std::array<uint32_t,4>{unsigned_byte,unsigned_short,unsigned_int,0xdead}[next()%4];
        d.count=int(next()%400);d.mode=std::array<uint32_t,4>{triangles,triangle_strip,triangle_fan,1}[next()%4];
        inspect(d,128);
    }
}
}
int main() {
    try {formats_and_topology();unsafe_inputs();clipping();capture_policy();fuzz();
        std::cout<<"geometry decoding, safe inspection, capture lifecycle, bounded output, overlays and 30000 synthetic mutations passed\n";
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
