// Actual allocator/libc imports/JNI table callbacks with synthetic registers.
// No ARM instructions, Dynarmic, SDL, game assets or real profile are executed.
#include "hle/hle.h"
#include "jni/jni.h"
#include "jni/array_bounds.h"
#include <algorithm>
#include <iostream>
#include <map>
#include <stdexcept>
#ifdef TEST_REAL_JAVA_ADAPTER
#include "config.h"
#include "game_patches.h"
#include "host_file.h"
#include <chrono>
#include <fstream>
Config g_config;
std::atomic<bool> g_quit{false};
namespace patches {std::shared_ptr<const std::vector<u8>> asset(const std::string&){return {};}}
#endif

namespace {
void require(bool yes,const char* why) { if(!yes)throw std::runtime_error(why); }
std::map<std::string,cpu::HleFn> imports;
std::map<u32,cpu::HleFn> callbacks;
int guest_error=0;
u32 stack=0;
struct Registers { u32 r[16]{};cpu::Ctx ctx{reinterpret_cast<cpu::Cpu*>(this)}; };
u32 invoke(cpu::HleFn fn,std::initializer_list<u32> args) {
    Registers c;c.r[13]=stack;size_t i=0;
    for(u32 v:args) { if(i<4)c.r[i]=v;else mem::w32(stack+u32(i-4)*4,v);++i; }
    fn(c.ctx);return c.r[0];
}
u32 api(int index,std::initializer_list<u32> args={}) {
    return invoke(callbacks.at(mem::r32(mem::r32(jni::env())+u32(index)*4)),args);
}
void exception(const char* cls) {
    require(api(228,{jni::env()})==1,"Java exception pending");
    auto e=jni::get(api(15,{jni::env()}));require(e && e->cls==cls,"correct Java exception class");
    api(17,{jni::env()});
}
#ifdef TEST_REAL_JAVA_ADAPTER
u32 java_call(int index,const jni::ObjPtr& object,const char* name,const char* sig,std::initializer_list<jni::JValue> args) {
    auto cls=jni::new_local(jni::class_object(object->cls));auto ref=jni::new_local(object);
    u32 n=mem::heap_strdup(name),s=mem::heap_strdup(sig);
    u32 method=api(33,{jni::env(),cls,n,s});mem::heap_free(n);mem::heap_free(s);
    u32 values=mem::heap_alloc(u32(std::max(size_t(1),args.size())*8));u32 offset=0;
    for(auto value:args){std::memcpy(mem::host(values+offset),&value.raw,8);offset+=8;}
    u32 result=api(index,{jni::env(),ref,method,values});mem::heap_free(values);return result;
}
void java_adapter_cases() {
    namespace fs=std::filesystem;
    auto root=fs::temp_directory_path()/("deadspace-java-array-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root/fs::path(u8"\u8d44\u4ea7"));auto file=root/fs::path(u8"\u8d44\u4ea7/input.bin");
    {std::ofstream out(file,std::ios::binary);out<<"abcdefgh";}
    hle::g_assets_dir=host_file::utf8(root);
    auto manager=jni::make("android/content/res/AssetManager");auto name=jni::new_local(jni::make_string("\xe8\xb5\x84\xe4\xba\xa7/input.bin"));
    auto stream=jni::get(java_call(36,manager,"open","(Ljava/lang/String;)Ljava/io/InputStream;",{jni::JValue::L(name)}));
    require(stream && api(228,{jni::env()})==0,"real Java asset adapter opens synthetic Unicode asset");
    auto array=jni::make_array('B',8);auto ref=jni::new_local(array);std::fill_n(mem::host(array->data),8,u8(0x44));
    auto read=[&](int start,int count){return java_call(51,stream,"read","([BII)I",{jni::JValue::L(ref),jni::JValue::I(start),jni::JValue::I(count)});};
    require(read(-1,3)==UINT32_MAX,"asset read rejects negative offset");exception("java/lang/IndexOutOfBoundsException");
    require(read(7,2)==UINT32_MAX,"asset read rejects mapped adjacent array extent");exception("java/lang/IndexOutOfBoundsException");
    require(mem::r8(array->data)==0x44 && read(2,3)==3,"invalid reads did not write or consume stream");
    require(mem::r8(array->data+2)=='a' && mem::r8(array->data+4)=='c',"valid read copies exact bytes at offset");
    auto track=jni::make("android/media/AudioTrack");
    auto audio=[&](int start,int count){return s32(java_call(51,track,"write","([BII)I",{jni::JValue::L(ref),jni::JValue::I(start),jni::JValue::I(count)}));};
    require(audio(-1,4)==-2 && audio(7,2)==-2 && audio(0,-1)==-2,"real audio adapter returns BAD_VALUE before queue/pacing on invalid extents");
    require(audio(8,0)==0,"valid empty audio write");
    java_call(63,stream,"close","()V",{}); // CallVoidMethodA
    fs::remove_all(root);
}
#endif
void allocation_cases() {
    u32 rounded=0,bytes=0;
    require(!checked_size::product(0x40000001u,4,UINT32_MAX,bytes),"multiply rejected before narrowing");
    require(!checked_size::round_up(UINT32_MAX,16,UINT32_MAX,rounded),"round-up cannot wrap to zero");
    require(!checked_size::round_up(1,0,UINT32_MAX,rounded),"zero alignment refused");
    require(!checked_size::round_up(1,3,UINT32_MAX,rounded),"non-power-of-two alignment refused");
    require(mem::heap_request(0,rounded)&&rounded==16,"zero malloc minimum block preserved");
    require(!mem::heap_request(UINT32_MAX,rounded)&&!mem::heap_request(uint64_t(1)<<32,rounded),"guest heap limit checked wide");
    require(!mem::map_request(0,rounded)&&!mem::map_request(UINT32_MAX,rounded),"invalid page allocation rejected");
    require(mem::map_request(4097,rounded)&&rounded==8192,"valid page rounding preserved");
    u32 before=mem::heap_alloc(16);
    require(invoke(imports.at("calloc"),{0x40000001u,4})==0 && guest_error==hle::G_ENOMEM,"actual calloc adapter rejects oversized product");
    require(invoke(imports.at("malloc"),{UINT32_MAX})==0 && guest_error==hle::G_ENOMEM,"actual malloc rejects rounding overflow");
    u32 after=mem::heap_alloc(16);require(after==before+16,"rejected requests did not mutate allocator");
    u32 p=invoke(imports.at("calloc"),{7,3});require(p && mem::heap_size(p)>=21,"normal calloc allocation");
    require(std::all_of(mem::host(p),mem::host(p)+21,[](u8 v){return v==0;}),"normal calloc zeroed");
    std::fill_n(mem::host(p),21,u8(0x6a));
    require(invoke(imports.at("realloc"),{p,UINT32_MAX})==0,"oversized realloc refused");
    require(mem::heap_size(p)>=21 && mem::r8(p)==0x6a && mem::r8(p+20)==0x6a,"failed realloc preserves original block/content");
    u32 q=invoke(imports.at("realloc"),{p,8192});require(q && mem::r8(q+20)==0x6a,"normal realloc copies original bytes");
    require(invoke(imports.at("realloc"),{q,0})==0 && mem::heap_size(q)==0,"zero realloc releases block as before");
    require(invoke(imports.at("calloc"),{0,UINT32_MAX})!=0,"zero product retains valid zero-size behavior");
    require(mem::map_alloc(UINT32_MAX)==0 && mem::map_alloc(0)==0,"actual map allocator rejects invalid sizes before commit");
    u32 mapped=mem::map_alloc(1);require(mapped==mem::MAP_BASE && mem::is_mapped(mapped),"map free-list unchanged by rejection");
    require(!mem::map_range(mapped+1,4096) && !mem::map_range(mem::MAP_END-4096,8192),"unmap alignment/end range validation");
    require(invoke(imports.at("munmap"),{mapped+1,4096})==UINT32_MAX && guest_error==hle::G_EINVAL,"invalid munmap reports EINVAL");
    require(mem::is_mapped(mapped),"invalid munmap left live pages untouched");mem::map_free(mapped,1);
    require(!mem::is_mapped(mapped),"valid unmap still decommits");
    require(invoke(imports.at("mmap"),{0,0,0,0x20,UINT32_MAX,0})==UINT32_MAX && guest_error==hle::G_EINVAL,"zero mmap refused");
}
void jni_cases() {
    jni::init();api(19,{jni::env(),64});
    require(api(179,{jni::env(),UINT32_MAX})==0,"negative primitive array refused");exception("java/lang/NegativeArraySizeException");
    require(api(179,{jni::env(),0x40000001})==0,"overflow primitive extent refused");exception("java/lang/OutOfMemoryError");
    require(api(172,{jni::env(),UINT32_MAX,0,0})==0,"negative object array refused without null dereference");exception("java/lang/NegativeArraySizeException");
    u32 ref=api(179,{jni::env(),8});auto array=jni::get(ref);require(array && array->len==8,"valid int array");
    u32 buffer=mem::heap_alloc(64);std::fill_n(mem::host(array->data),32,u8(0x5a));std::fill_n(mem::host(buffer),64,u8(0x7b));
    // JNI IntArrayRegion slots are base 199/207 plus primitive index 4.
    api(211,{jni::env(),ref,0xfffffffe,4,buffer});exception("java/lang/ArrayIndexOutOfBoundsException");
    require(mem::r8(array->data)==0x5a && mem::r8(array->data+31)==0x5a,"wrapped region performed no write");
    api(203,{jni::env(),ref,0,UINT32_MAX,buffer});exception("java/lang/ArrayIndexOutOfBoundsException");
    require(mem::r8(buffer)==0x7b,"invalid get did not touch external buffer");
    api(211,{jni::env(),ref,7,2,buffer});exception("java/lang/ArrayIndexOutOfBoundsException");
    api(203,{jni::env(),ref,0,1,0});exception("java/lang/IllegalArgumentException");
    api(211,{jni::env(),ref,8,0,0});require(api(228,{jni::env()})==0,"zero copy at end permits null external buffer");
    mem::w32(buffer,0x12345678);api(211,{jni::env(),ref,3,1,buffer});
    require(mem::r32(array->data+12)==0x12345678,"valid set copies correct element");
    mem::w32(buffer+4,0);api(203,{jni::env(),ref,3,1,buffer+4});require(mem::r32(buffer+4)==0x12345678,"valid get copies correct element");
    u32 address=0,bytes=0;
    require(!jni::array_span(array->data,8,-1,1,4,address,bytes),"asset/audio common guard rejects negative offset");
    require(!jni::array_span(array->data,8,7,2,4,address,bytes),"mapped adjacent memory is not part of array");
    require(!jni::array_span(0xfffffff0,8,0,8,4,address,bytes),"guest address end cannot wrap");
    require(jni::array_span(array->data,8,2,3,4,address,bytes)&&address==array->data+8&&bytes==12,"valid byte span exact");
    require(api(229,{jni::env(),buffer,0,1})==0,"direct buffer 64-bit capacity cannot truncate");exception("java/lang/IllegalArgumentException");
    u32 direct=api(229,{jni::env(),buffer,16,0});require(jni::get(direct)->len==16,"valid direct buffer preserved");
    auto string=jni::new_local(jni::make_string("abcd"));
    api(220,{jni::env(),string,UINT32_MAX,2,buffer});exception("java/lang/StringIndexOutOfBoundsException");
#ifdef TEST_REAL_JAVA_ADAPTER
    java_adapter_cases();
#endif
    api(20,{jni::env(),0});
    mem::heap_free(buffer);
}
}
namespace cpu {
u32 Ctx::reg(int i)const{return reinterpret_cast<const Registers*>(c)->r[i];}
void Ctx::set_reg(int i,u32 v){reinterpret_cast<Registers*>(c)->r[i]=v;}
u32 Ctx::ext(int)const{return 0;}void Ctx::set_ext(int,u32){}
u32 Ctx::fpscr()const{return 0;}void Ctx::set_fpscr(u32){}
ThreadInfo& thread(){static ThreadInfo t{};t.tid=1;return t;}
u32 register_hle(const std::string& name,HleFn fn){u32 id=mem::STUB_BASE+u32(callbacks.size())*8;callbacks[id]=fn;imports[name]=fn;return id;}
u32 call_v(u32,const u32*,int,u32*){throw std::runtime_error("synthetic tests must never execute a guest/JIT");}
u32 call(u32,std::initializer_list<u32>){throw std::runtime_error("synthetic tests must never execute a guest/JIT");}
u32 current_pc(){return 0;}std::string symbolize(u32){return "synthetic";}std::string hle_name_at(u32){return "synthetic";}
}
namespace elf {u32 find(const std::string&){return 0;}}
#ifndef TEST_REAL_JAVA_ADAPTER
namespace jni {void register_java_classes(){}}
#endif
namespace hle {
void reg(const char* name,cpu::HleFn fn){cpu::register_hle(name,fn);}void set_errno(int e){guest_error=e;}
int host_errno_to_guest(int e){return e;}
std::string host_path(const std::string& path){return path;}
std::string g_assets_dir,g_data_dir;
}
namespace logging {bool g_verbose=false;void write(char,const char*,...) {}}
int main(){try{
    require(mem::init(),"synthetic guest address reservation");stack=mem::heap_alloc(256);
    hle::register_libc();allocation_cases();jni_cases();
    std::cout<<"Allocation and JNI adapter boundary tests passed; no game/guest instructions/JIT executed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
