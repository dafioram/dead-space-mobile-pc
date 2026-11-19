#include "geometry_capture.h"
#include "geometry_source.h"
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
void require(bool okay,const char* why){if(!okay)throw std::runtime_error(why);}
bool near(float a,float b){return std::fabs(a-b)<.001f;}
template<class T> void append(std::vector<unsigned char>& bytes,T value){const auto at=bytes.size();bytes.resize(at+sizeof value);std::memcpy(bytes.data()+at,&value,sizeof value);}
geometry::Submission input(const geometry::Source& v,const geometry::Source& i) {
    geometry::Submission in;in.indexed=true;in.count=3;
    in.vertices={v.reader(),0x2000,0,0x1406,3,0,true};in.indices=i.reader();
    in.state.modelview[12]=10;in.state.modelview[14]=-5;
    in.state.projection[11]=-1;in.state.projection[15]=0;
    in.state.color={1,.1f,.05f,1};return in;
}
void numeric_and_indices() {
    for(uint32_t type:{0x1400u,0x1401u,0x1402u,0x1403u,0x1404u,0x1405u,0x1406u,0x140cu}) {
        std::vector<unsigned char> raw;
        for(int value:{0,0,0,100,0,0,0,1,0})switch(type){
            case 0x1400:case 0x1401:append(raw,uint8_t(value));break;
            case 0x1402:case 0x1403:append(raw,uint16_t(value));break;
            case 0x1404:case 0x1405:append(raw,uint32_t(value));break;
            case 0x140c:append(raw,int32_t(value*65536));break;
            default:append(raw,float(value));break;
        }
        for(uint32_t index_type:{0x1401u,0x1403u,0x1405u}) {
            std::vector<unsigned char> index;
            for(unsigned n:{0u,1u,2u}){if(index_type==0x1401)append(index,uint8_t(n));else if(index_type==0x1403)append(index,uint16_t(n));else append(index,n);}
            const auto before=raw,index_before=index;
            geometry::Source vertices{raw.data(),raw.size()},indices{index.data(),index.size()};
            for(bool native:{false,true}) {
                auto in=input(vertices,indices);in.vertices.type=type;in.index_type=index_type;in.native=native;
                geometry::Budget budget;const auto r=geometry::analyze(in,budget);
                require(r.native==native && r.vertices_checked==3 && r.indices_checked==3 && r.primitives_checked==1,"both renderer inputs, all index widths");
                require(r.flags==geometry::long_edge && near(r.local_hi[0],100) && near(r.eye_lo[0],10) && near(r.eye_hi[0],110),"vertex numeric types and modelview before rendering");
                require(near(r.ndc_hi[0],22) && near(r.clip_w_lo,5) && near(r.color_mean[0],1),"projection and current color evidence");
                require(raw==before && index==index_before,"read-only capture preserves exact input bytes");
            }
        }
    }
}
void modes_and_formats() {
    const float positions[]={999,999,999,0,0,0,100,0,0,0,.001f,0};
    geometry::Source v{reinterpret_cast<const unsigned char*>(positions),sizeof positions},none;
    auto in=input(v,none);in.indexed=false;in.first=1;
    for(uint32_t mode=0;mode<=6;++mode){in.mode=mode;geometry::Budget b;const auto r=geometry::analyze(in,b);
        require(r.vertices_checked && r.primitives_checked && !(r.flags&(geometry::unsupported|geometry::unreadable)),"points/lines/loop/strip/triangles/fan/strip supported");
        require(r.lo==1 && r.hi==(mode==1?2u:3u) && r.local_hi[0]==100,"DrawArrays first offset honored; incomplete trailing line ignored");
        if(mode>=4)require(r.flags&geometry::thin_triangle,"stretched thin primitive is a suspect, not culled");}
    unsigned char packed[3*20+1]{};const float p[][4]={{0,0,0,1},{100,0,0,1},{0,1,0,1}};
    for(unsigned i=0;i<3;++i)std::memcpy(packed+1+i*20,p[i],16);
    const uint8_t colors[]={200,20,10,255,200,20,10,255,200,20,10,255};
    geometry::Source strided{packed+1,sizeof packed-1},color{colors,sizeof colors};
    in=input(strided,none);in.indexed=false;in.vertices.components=4;in.vertices.stride=20;
    in.colors={color.reader(),42,7,0x1401,4,0,true};
    geometry::Budget b;auto r=geometry::analyze(in,b);
    require(r.vertices_checked==3 && r.color_array && near(r.color_mean[0],200.f/255) && r.colors_checked==3,"unaligned strided XYZW and normalized ubyte colors");
    in.vertices.components=2;in.vertices.stride=20;b={};r=geometry::analyze(in,b);
    require(r.vertices_checked==3 && near(r.eye_lo[2],-5),"two-coordinate vertices use default Z/W");
    in.state.modelview[14]=0;b={};r=geometry::analyze(in,b);
    require(r.flags&geometry::zero_w,"near-zero clip W explicitly reported");
    in.state.modelview[14]=5;b={};r=geometry::analyze(in,b);
    require(r.behind_eye==3,"behind-eye clip coordinates exposed");
}
void bounds_and_budgets() {
    float p[]={0,0,0,1,0,0,0,1,0};uint32_t index[]={0,1,0xffffffff};
    geometry::Source v{reinterpret_cast<const unsigned char*>(p),sizeof p},i{reinterpret_cast<const unsigned char*>(index),sizeof index};
    auto in=input(v,i);in.index_type=0x1405;
    geometry::Budget b;auto r=geometry::analyze(in,b);
    require(r.flags&geometry::unreadable,"huge indices never allocate or dereference outside source");
    in.indexed=false;in.first=UINT32_MAX;b={};r=geometry::analyze(in,b);
    require(r.flags&geometry::unreadable,"DrawArrays overflow diagnosed safely");
    in.first=0;in.vertices.stride=-1;b={};r=geometry::analyze(in,b);
    require(r.flags&geometry::unsupported,"invalid array stride is not interpreted");
    in.vertices.stride=0;in.mode=999;b={};r=geometry::analyze(in,b);require(r.flags&geometry::unsupported,"unknown primitive explicitly unsupported");
    in.mode=4;p[3]=std::numeric_limits<float>::infinity();b={};r=geometry::analyze(in,b);require(r.flags&geometry::nonfinite,"nonfinite inputs retained as evidence");p[3]=1;
    b={0,0};r=geometry::analyze(in,b);require(r.flags==geometry::partial && !r.vertices_checked,"exhausted budgets never read");
    in.count=UINT32_MAX;b={};r=geometry::analyze(in,b);require(r.vertices_checked<=96 && (r.flags&geometry::partial),"untrusted counts cannot cause unbounded scan/allocation");
    in.count=3;b={8192,2048};for(int n=0;n<9000;++n)geometry::analyze(in,b);
    require(b.vertices<=8192 && !b.draws,"per-frame budget saturates without underflow");
    // Uniform sampling includes the LAST primitive, not just an early prefix.
    std::vector<uint16_t> late(600,0);late[599]=0xffff;
    geometry::Source ix{reinterpret_cast<const unsigned char*>(late.data()),late.size()*2};
    in=input(v,ix);in.count=600;b={};r=geometry::analyze(in,b);
    require((r.flags&geometry::unreadable) && r.hi==0xffff && r.indices_checked==96,"late bad index included in bounded sample");
    b={};b.per_draw_vertices=3;b.sample_phase=199;r=geometry::analyze(in,b);
    require(r.vertices_checked==2 && (r.flags&geometry::unreadable),"one-primitive adaptive budget rotates to late defect");
}
bool mapped(uint64_t address,size_t size){return address>=0x2000 && address<=0x2020 && size<=0x2020-address;}
void source_guards() {
    unsigned char data[64]{},out[16]{};
    const geometry::Source source{data,1ull<<32,0x2000,mapped};
    require(source.reader().get(16,out,16) && !source.reader().get(24,out,16),"guest mapped-range predicate protects crossing reads");
    require(!source.reader().get(UINT64_MAX,out,16),"offset arithmetic guarded");
    const geometry::Source wrapped{data,64,UINT64_MAX-2,nullptr};require(!wrapped.reader().get(4,out,1),"base overflow guarded");
    const geometry::Source shadow{data,4};require(!shadow.reader().get(3,out,2),"VBO shadow end checked");
}
}
int main(){try{numeric_and_indices();modes_and_formats();bounds_and_budgets();source_guards();
    std::cout<<"Shared geometry numeric/index/primitive/state/reader/budget tests passed; no SDL/GL/assets/game executed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
