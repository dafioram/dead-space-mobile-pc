// Real descriptor/stdio imports and serialization-boundary callbacks, with
// synthetic guest memory/registers and simulated native returns. No guest code.
#include "save_fixture.h"
#include "save_io.h"
#include "hle/hle.h"
#include <algorithm>
#include <chrono>
#include <cerrno>
#include <fstream>
#include <iostream>
#include <map>
#include "../src/save_lifecycle.cpp"

namespace {
std::vector<u8> guest(1<<20);
int guest_error = 0;
std::map<std::string,cpu::HleFn> imports;
struct Registers {
    u32 r[16]{};
    cpu::Ctx ctx{reinterpret_cast<cpu::Cpu*>(this)};
    Registers() { r[13]=0x70000;r[14]=0x12345678; }
};
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
u32 invoke(const std::string& name,std::initializer_list<u32> arguments) {
    Registers c;size_t i=0;for(u32 a:arguments)c.r[i++]=a;
    imports.at(name)(c.ctx);return c.r[0];
}
void text(u32 p,const std::string& value){std::memcpy(guest.data()+p,value.c_str(),value.size()+1);}
save_fixture::Bytes file(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
}
namespace mem {
u8* g_base=::guest.data();
std::array<u8*,(1u<<20)>* g_page_table=nullptr;
bool is_mapped(u32 address){return address<::guest.size();}
u32 heap_alloc(u32 size,u32){static u32 p=0x80000;u32 out=p;p=(p+size+15)&~15u;require(p<::guest.size(),"synthetic allocation overflow");return out;}
void heap_free(u32){}
}
namespace cpu {
u32 Ctx::reg(int i)const{return reinterpret_cast<const Registers*>(c)->r[i];}
void Ctx::set_reg(int i,u32 value){reinterpret_cast<Registers*>(c)->r[i]=value;}
u32 register_hle(const std::string&,HleFn){throw std::runtime_error("synthetic test must not install executable instructions");}
bool hook_entry(u32,const std::string&,HleFn){throw std::runtime_error("synthetic test must not patch executable instructions");}
void skip_function(Ctx& c,u32 result){c.ret(result);c.set_reg(12,1);}
}
namespace elf {u32 find(const std::string&){return 0;}}
namespace hle {
void reg(const char* name,cpu::HleFn fn){imports[name]=fn;}
u32 resolve(const std::string& name,bool,bool){require(name=="__sF","unexpected data import");return 0x1000;}
void set_errno(int e){guest_error=e;}
}
namespace logging {bool g_verbose = false; void write(char,const char*,...) {}}

int main(){
    namespace fs=std::filesystem;
    try {
        auto root=fs::temp_directory_path()/("deadspace-save-adapter-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        require(fs::create_directory(root),"cannot create unique synthetic root");
        save_io::set_profile_root(root);hle::g_data_dir=save_io::path_utf8(root);
        hle::register_file_descriptors();hle::register_stdio();hle::register_printf();
        require(hle::host_path("/../../outside.sb")==hle::g_data_dir+"/outside.sb"&&
                hle::host_path("..\\..\\outside.sb")==hle::g_data_dir+"/outside.sb","guest path escaped profile root");
        save_lifecycle::return_stub=0x2000;
        save_lifecycle::file_input_vtable=0x45000;
        text(0x10000,"/00_CheckpointSave.sb");text(0x11000,"wb");
        auto a=save_fixture::valid(1),b=save_fixture::valid(2);
        auto fd_save=[&](const auto& bytes,save_io::Faults* faults=nullptr,bool serialized=true){
            Registers outer;save_lifecycle::on_save(outer.ctx);
            require(outer.r[14]==0x2000,"native return was not intercepted");
            auto fd=invoke("open",{0x10000,0x241});require(s32(fd)>=0,"profile descriptor open blocked");
            std::copy(bytes.begin(),bytes.end(),guest.begin()+0x20000);
            save_io::test_faults(faults);
            auto wrote=invoke("write",{fd,0x20000,u32(bytes.size())});
            if(!faults)require(wrote==bytes.size(),"valid guest descriptor write failed");
            mem::w32(0x40000+0x10,fd);
            Registers close;close.r[0]=0x40000;save_lifecycle::on_close(close.ctx);
            invoke("close",{fd});
            close.r[0]=1;save_lifecycle::on_return(close.ctx); // native Close otherwise returns true
            if(faults)require(close.r[0]==0,"native Close hid host failure");
            save_io::test_faults(nullptr);
            outer.r[0]=serialized?1:0;save_lifecycle::on_return(outer.ctx);
            require(outer.r[14]==0x12345678,"native caller link not restored");
            return outer.r[0]!=0;
        };
        require(fd_save(a)&&file(root/"00_CheckpointSave.sb")==a,"first native save did not publish");
        save_io::Faults full;full.write_limit=7;full.interrupted_writes=3;
        // Partial/EINTR recovery is success, so test the actual imports directly.
        Registers outer;save_lifecycle::on_save(outer.ctx);
        auto fd=invoke("open",{0x10000,0x241});std::copy(b.begin(),b.end(),guest.begin()+0x20000);
        save_io::test_faults(&full);require(invoke("write",{fd,0x20000,u32(b.size())})==b.size(),"guest write-all contract failed");save_io::test_faults(nullptr);
        require(invoke("close",{fd})==0&&file(root/"00_CheckpointSave.sb")==a,"save published before native result");
        outer.r[0]=1;save_lifecycle::on_return(outer.ctx);
        require(outer.r[0]==1&&file(root/"00_CheckpointSave.sb")==b,"successful native return was overridden");
        save_io::Faults disk;disk.write_error=true;
        require(!fd_save(a,&disk)&&guest_error==hle::G_ENOSPC&&file(root/"00_CheckpointSave.sb")==b,"disk-full native failure lost errno or final");
        save_io::Faults late;late.close_error=true;
        require(!fd_save(a,&late)&&file(root/"00_CheckpointSave.sb")==b,"close failure presented as native save success");
        require(!fd_save(a,nullptr,false)&&file(root/"00_CheckpointSave.sb")==b,"native serializer false published output");
        Registers invalid;save_lifecycle::on_save(invalid.ctx);fd=invoke("open",{0x10000,0x241});
        require(invoke("write",{fd,0xfffffff0,128})==0xffffffff&&guest_error==hle::G_EFAULT,"guest write bounds not checked");
        invoke("close",{fd});invalid.r[0]=1;save_lifecycle::on_return(invalid.ctx);
        require(invalid.r[0]==0&&file(root/"00_CheckpointSave.sb")==b,"bad guest buffer published a save");
        // Native Flush discards fsync's result without the boundary correction.
        Registers flush_save;save_lifecycle::on_save(flush_save.ctx);fd=invoke("open",{0x10000,0x241});
        mem::w32(0x40010,fd);Registers flush;flush.r[0]=0x40000;save_lifecycle::on_flush(flush.ctx);
        save_io::Faults sync;sync.sync_error=true;save_io::test_faults(&sync);
        require(invoke("fsync",{fd})==0xffffffff&&guest_error==hle::G_EIO,"guest fsync hid failure");save_io::test_faults(nullptr);
        flush.r[0]=1;save_lifecycle::on_return(flush.ctx);require(flush.r[0]==0,"native Flush hid failure");
        invoke("close",{fd});flush_save.r[0]=1;save_lifecycle::on_return(flush_save.ctx);require(flush_save.r[0]==0,"failed flush allowed publication");
        fd=invoke("open",{0x10000,0});require(s32(fd)>=0,"existing save read blocked");
        mem::w32(0x41000,save_lifecycle::file_input_vtable);mem::w32(0x41004,0x40000);mem::w32(0x40010,fd);
        Registers load;load.r[1]=0x41000;save_lifecycle::on_load(load.ctx);require(load.r[14]==0x2000,"profile parse boundary missing");
        require(invoke("read",{fd,0x20000,u32(b.size())})==b.size(),"guest save read failed");
        require(std::equal(b.begin(),b.end(),guest.begin()+0x20000),"read bytes changed");
        load.r[0]=1;save_lifecycle::on_return(load.ctx);require(load.r[0]==1,"valid native parse was overridden");
        require(invoke("close",{fd})==0,"profile read close failed");
        text(0x10000,"/invalid.sb");{std::ofstream out(root/"invalid.sb",std::ios::binary);out<<"bad";}
        fd=invoke("open",{0x10000,0});mem::w32(0x40010,fd);
        Registers malformed;malformed.r[1]=0x41000;save_lifecycle::on_load(malformed.ctx);
        require(malformed.r[0]==0&&malformed.r[12]==1&&malformed.r[14]==0x12345678,"invalid input reached parser or corrupted native return path");
        require(invoke("close",{fd})==0&&file(root/"invalid.sb")==save_fixture::Bytes({'b','a','d'}),"invalid selected file changed during rejection");
        text(0x10000,"/00_CheckpointSave.sb");
        Registers stdio;save_lifecycle::on_save(stdio.ctx);auto gf=invoke("fopen",{0x10000,0x11000});require(gf!=0,"valid guest fopen failed");
        std::copy(a.begin(),a.end(),guest.begin()+0x20000);
        require(invoke("fwrite",{0x20000,1,u32(a.size()),gf})==a.size(),"guest fwrite failed");
        require(invoke("fclose",{gf})==0&&file(root/"00_CheckpointSave.sb")==b,"guest fclose published before parser result");
        stdio.r[0]=1;save_lifecycle::on_return(stdio.ctx);require(stdio.r[0]==1&&file(root/"00_CheckpointSave.sb")==a,"guest stdio commit failed");
        Registers overflow;save_lifecycle::on_save(overflow.ctx);gf=invoke("fopen",{0x10000,0x11000});
        require(invoke("fwrite",{0x20000,0x80000000,4,gf})==0&&guest_error==hle::G_EOVERFLOW,"guest fwrite product overflow missed");
        require(invoke("fclose",{gf})==0xffffffff,"guest fclose hid overflow poisoning");
        overflow.r[0]=1;save_lifecycle::on_return(overflow.ctx);require(overflow.r[0]==0&&file(root/"00_CheckpointSave.sb")==a,"overflow prevented no publication");
        require(invoke("fflush",{0x77777})==0xffffffff&&guest_error==hle::G_EBADF,"invalid guest fflush became flush-all");
        Registers formatted;save_lifecycle::on_save(formatted.ctx);gf=invoke("fopen",{0x10000,0x11000});
        text(0x12000,"%s");text(0x12100,"synthetic");save_io::Faults print_error;print_error.zero_write=true;save_io::test_faults(&print_error);
        require(invoke("fprintf",{gf,0x12000,0x12100})==0xffffffff,"fprintf hid file write failure");save_io::test_faults(nullptr);
        require(invoke("ferror",{gf})!=0,"stdio error indicator lost host write failure");
        invoke("clearerr",{gf});require(invoke("ferror",{gf})==0,"clearerr did not clear the stdio indicator");
        invoke("fclose",{gf});formatted.r[0]=1;save_lifecycle::on_return(formatted.ctx);
        require(formatted.r[0]==0&&file(root/"00_CheckpointSave.sb")==a,"formatted output failure published a save");
        text(0x10000,"/unused.sb");require(invoke("fopen",{0x10000,0x11000+0x100})==0&&guest_error==hle::G_EINVAL,"empty fopen mode not rejected");
        text(0x11100,"rb");require(invoke("fopen",{0x10000,0x11100})==0&&guest_error==hle::G_ENOENT,"expected missing save errno wrong");
        require(hle::host_errno_to_guest(EACCES)==hle::G_EACCES&&hle::host_errno_to_guest(EROFS)==hle::G_EROFS,"host errno mapping wrong");
        fs::remove_all(root); // only this test's exclusively-created synthetic root
        std::cout<<"production save descriptor/stdio/native-result adapter tests passed\n";
    }catch(const std::exception& e){save_io::test_faults(nullptr);std::cerr<<e.what()<<'\n';return 1;}
}
