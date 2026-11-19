#include "geometry_capture.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace geometry {
namespace {
unsigned width(uint32_t type) {
    switch(type){case 0x1400:case 0x1401:return 1;case 0x1402:case 0x1403:return 2;
    case 0x1404:case 0x1405:case 0x1406:case 0x140c:return 4;default:return 0;}
}
template<class T> T load(const unsigned char* p){T v;std::memcpy(&v,p,sizeof v);return v;}
float number(const unsigned char* p,uint32_t type,bool color) {
    switch(type){
    case 0x1400:return color?std::max(-1.f,float(load<int8_t>(p))/127.f):float(load<int8_t>(p));
    case 0x1401:return float(*p)/(color?255.f:1.f);
    case 0x1402:return color?std::max(-1.f,float(load<int16_t>(p))/32767.f):float(load<int16_t>(p));
    case 0x1403:return float(load<uint16_t>(p))/(color?65535.f:1.f);
    case 0x1404:return float(load<int32_t>(p))/(color?2147483647.f:1.f);
    case 0x1405:return float(load<uint32_t>(p))/(color?4294967295.f:1.f);
    case 0x140c:return float(load<int32_t>(p))/65536.f;
    default:return load<float>(p);
    }
}
bool attribute(const Array& a,uint32_t index,bool color,std::array<float,4>& out,uint32_t& flags) {
    const auto bytes=width(a.type);
    if(!bytes || !a.enabled || a.components<(color?3:2) || a.components>4 || a.stride<0){flags|=unsupported;return false;}
    const uint64_t stride=a.stride?uint64_t(a.stride):uint64_t(a.components)*bytes;
    unsigned char value[16]{};
    if(!a.storage.get(uint64_t(index)*stride,value,size_t(a.components)*bytes)){flags|=unreadable;return false;}
    out={0,0,0,1};
    for(int k=0;k<a.components;++k){out[size_t(k)]=number(value+unsigned(k)*bytes,a.type,color);if(!std::isfinite(out[size_t(k)]))flags|=nonfinite;}
    return true;
}
std::array<float,4> transform(const std::array<float,16>& m,const std::array<float,4>& p) {
    std::array<float,4> out{};
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)out[r]+=m[c*4+r]*p[c];
    return out;
}
bool cartesian(const std::array<float,4>& p,std::array<float,3>& out,uint32_t& flags) {
    for(float v:p)if(!std::isfinite(v)){flags|=nonfinite;return false;}
    if(std::fabs(p[3])<1e-8f){flags|=zero_w;return false;}
    for(unsigned k=0;k<3;++k){out[k]=p[k]/p[3];if(!std::isfinite(out[k])){flags|=nonfinite;return false;}}
    return true;
}
void bounds(const std::array<float,3>& p,std::array<float,3>& lo,std::array<float,3>& hi) {
    for(unsigned k=0;k<3;++k){lo[k]=std::min(lo[k],p[k]);hi[k]=std::max(hi[k],p[k]);}
}
float distance(const std::array<float,3>& a,const std::array<float,3>& b) {
    return std::hypot(a[0]-b[0],a[1]-b[1],a[2]-b[2]);
}
}  // namespace
Record analyze(const Submission& in,Budget& budget) {
    Record r;
    r.native=in.native;r.indexed=in.indexed;r.source=in.vertices.pointer;r.vertex_buffer=in.vertices.buffer;
    r.texture=in.texture;r.return_address=in.return_address;r.mode=in.mode;r.count=in.count;r.first=in.first;
    r.index_type=in.index_type;r.index_pointer=in.index_pointer;r.element_buffer=in.element_buffer;
    r.vertex_type=in.vertices.type;r.components=uint32_t(in.vertices.components);r.stride=uint32_t(in.vertices.stride);
    r.color_array=in.colors.enabled;r.color_type=in.colors.type;r.color_components=uint32_t(in.colors.components);
    r.color_pointer=in.colors.pointer;r.color_buffer=in.colors.buffer;r.state=in.state;
    r.perspective=std::fabs(in.state.projection[11])>1e-8f;
    r.local_lo.fill(std::numeric_limits<float>::infinity());r.eye_lo=r.ndc_lo=r.local_lo;
    r.local_hi.fill(-std::numeric_limits<float>::infinity());r.eye_hi=r.ndc_hi=r.local_hi;
    r.clip_w_lo=std::numeric_limits<float>::infinity();r.clip_w_hi=-r.clip_w_lo;
    if(!budget.draws){r.flags|=partial;return r;}
    --budget.draws;
    for(float f:in.state.modelview)if(!std::isfinite(f))r.flags|=nonfinite;
    for(float f:in.state.projection)if(!std::isfinite(f))r.flags|=nonfinite;
    unsigned arity=0;uint32_t primitives=0;
    switch(in.mode){
    case 0:arity=1;primitives=in.count;break; // POINTS
    case 1:arity=2;primitives=in.count/2;break; // LINES
    case 2:arity=2;primitives=in.count>1?in.count:0;break; // LINE_LOOP
    case 3:arity=2;primitives=in.count>1?in.count-1:0;break; // LINE_STRIP
    case 4:arity=3;primitives=in.count/3;break;
    case 5:case 6:arity=3;primitives=in.count>2?in.count-2:0;break; // strip/fan
    default:r.flags|=unsupported;return r;
    }
    if(!primitives)return r;
    const unsigned index_bytes=in.index_type==0x1401?1:in.index_type==0x1403?2:in.index_type==0x1405?4:0;
    if(in.indexed && !index_bytes){r.flags|=unsupported;return r;}
    const uint32_t samples=std::min({primitives,32u,budget.vertices/arity,budget.per_draw_vertices/arity});
    if(samples<primitives)r.flags|=partial;
    for(uint32_t sample=0;sample<samples;++sample){
        uint32_t primitive=samples==1?0:uint32_t(uint64_t(sample)*(primitives-1)/(samples-1));
        // Keep an end-of-stream probe when there are multiple samples, while
        // varying the other probes across frames instead of certifying a prefix.
        if(samples<primitives && budget.sample_phase && (samples==1 || sample+1<samples))
            primitive=uint32_t((uint64_t(primitive)+budget.sample_phase)%primitives);
        uint32_t slots[3]{};
        if(in.mode==0)slots[0]=primitive;
        else if(in.mode==1){slots[0]=primitive*2;slots[1]=slots[0]+1;}
        else if(in.mode==2){slots[0]=primitive;slots[1]=(primitive+1)%in.count;}
        else if(in.mode==3){slots[0]=primitive;slots[1]=primitive+1;}
        else if(in.mode==4){slots[0]=primitive*3;slots[1]=slots[0]+1;slots[2]=slots[0]+2;}
        else if(in.mode==5){slots[0]=primitive;slots[1]=primitive+1;slots[2]=primitive+2;}
        else {slots[0]=0;slots[1]=primitive+1;slots[2]=primitive+2;}
        std::array<std::array<float,3>,3> local{},eye{};bool valid=true;
        for(unsigned k=0;k<arity;++k){
            --budget.vertices;
            uint32_t index=0;
            if(in.indexed){unsigned char raw[4]{};
                if(!in.indices.get(uint64_t(slots[k])*index_bytes,raw,index_bytes)){r.flags|=unreadable;valid=false;continue;}
                index=index_bytes==1?raw[0]:index_bytes==2?load<uint16_t>(raw):load<uint32_t>(raw);++r.indices_checked;
            }else {
                const uint64_t wide=uint64_t(in.first)+slots[k];
                if(wide>UINT32_MAX){r.flags|=unreadable;valid=false;continue;}index=uint32_t(wide);
            }
            r.lo=std::min(r.lo,index);r.hi=std::max(r.hi,index);
            std::array<float,4> p;
            if(!attribute(in.vertices,index,false,p,r.flags)){valid=false;continue;}
            ++r.vertices_checked;
            const auto eye4=transform(in.state.modelview,p),clip=transform(in.state.projection,eye4);
            const bool local_ok=cartesian(p,local[k],r.flags),eye_ok=cartesian(eye4,eye[k],r.flags);
            valid=valid && local_ok && eye_ok;
            if(local_ok)bounds(local[k],r.local_lo,r.local_hi);
            if(eye_ok)bounds(eye[k],r.eye_lo,r.eye_hi);
            if(std::isfinite(clip[3])){r.clip_w_lo=std::min(r.clip_w_lo,clip[3]);r.clip_w_hi=std::max(r.clip_w_hi,clip[3]);if(clip[3]<=0)++r.behind_eye;}
            std::array<float,3> ndc{};if(cartesian(clip,ndc,r.flags))bounds(ndc,r.ndc_lo,r.ndc_hi);
            auto color=in.state.color;
            if(!in.colors.enabled || attribute(in.colors,index,true,color,r.flags)){
                bool finite=true;for(float f:color)finite=finite && std::isfinite(f);
                if(finite){++r.colors_checked;for(unsigned c=0;c<4;++c)r.color_mean[c]+=color[c];}else r.flags|=nonfinite;
            }
        }
        if(!valid)continue;
        ++r.primitives_checked;
        if(arity>1){
            const float a=distance(eye[0],eye[1]);float longest=a;
            r.max_local_edge=std::max(r.max_local_edge,distance(local[0],local[1]));
            if(arity==3){
                longest=std::max({a,distance(eye[1],eye[2]),distance(eye[2],eye[0])});
                r.max_local_edge=std::max({r.max_local_edge,distance(local[1],local[2]),distance(local[2],local[0])});
                const std::array<float,3> u{eye[1][0]-eye[0][0],eye[1][1]-eye[0][1],eye[1][2]-eye[0][2]},v{eye[2][0]-eye[0][0],eye[2][1]-eye[0][1],eye[2][2]-eye[0][2]};
                const float twice_area=std::hypot(u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]);
                const float aspect=longest*longest/std::max(twice_area,1e-12f);
                if(std::isfinite(aspect))r.max_aspect=std::max(r.max_aspect,aspect);else r.flags|=nonfinite;
                if(longest>2 && aspect>1000)r.flags|=thin_triangle;
            }
            if(!std::isfinite(longest))r.flags|=nonfinite;else r.max_edge=std::max(r.max_edge,longest);
        }
    }
    if(r.max_edge>20)r.flags|=long_edge;
    if(r.colors_checked)for(float& f:r.color_mean)f/=float(r.colors_checked);
    return r;
}
}  // namespace geometry
