#include "host_file.h"
#include "asset_file.h"
#include "elf_loader.h"
#include "cpu.h"
#include "log.h"
#include "texpack.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool yes,const char* why){if(!yes)throw std::runtime_error(why);}
std::string read(const std::filesystem::path& path){std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
}
namespace mem {
u8* g_base=nullptr;
void commit(u32,u32){throw std::runtime_error("invalid synthetic ELF must not map guest memory");}
}
namespace cpu {
std::string hle_name_at(u32){return "";}
u32 call(u32,std::initializer_list<u32>){throw std::runtime_error("path tests must never execute guest/JIT");}
u32 call_v(u32,const u32*,int,u32*){throw std::runtime_error("path tests must never execute guest/JIT");}
}
int main(){namespace fs=std::filesystem;fs::path root;
try{
    root=fs::temp_directory_path()/("deadspace-host-path-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    auto folder=root/fs::path(u8"\u6d4b\u8bd5 \u041f\u0443\u0442\u044c");fs::create_directories(folder);
    auto file=folder/fs::path(u8"\u8d44\u4ea7.bin");auto name=host_file::utf8(file);
    require(host_file::path(name)==file,"native/UTF-8 path round trip");
    auto f=host_file::open(name,"wb");require(f,"Unicode host write");std::fputs("synthetic",f);std::fclose(f);
    f=assets::open_file(name);require(f,"actual asset reader opens Unicode file");char bytes[10]{};
    require(std::fread(bytes,1,9,f)==9&&std::string(bytes)=="synthetic","asset bytes unchanged");std::fclose(f);
    require(!assets::open_file(host_file::utf8(folder)),"asset reader still refuses directories");
    auto log=folder/fs::path(u8"\u65e5\u5fd7.log");logging::open_file(host_file::utf8(log).c_str());LOGI("synthetic Unicode log");
    // An invalid, synthetic header proves opening reached parsing, not an ANSI
    // fopen failure. This deliberately does not exercise/change ELF bounds.
    auto library=folder/fs::path(u8"\u5e93.so");{std::ofstream out(library,std::ios::binary);char header[52]{};out.write(header,sizeof header);}
    elf::Module module;require(!elf::load(host_file::utf8(library),0x10000000,module,[](const std::string&,bool,bool){return u32(0);}),"synthetic non-ELF refused");
    logging::close_file();auto logged=read(log);
    require(logged.find("synthetic Unicode log")!=std::string::npos&&logged.find("not a 32-bit ARM ELF")!=std::string::npos,"real log and ELF file-open paths handle Unicode");
    require(logged.find("cannot open")==std::string::npos,"Unicode library was opened successfully");
    std::vector<u8> rgba(8*8*4);for(size_t i=0;i<rgba.size();++i)rgba[i]=u8(i);
    char hash[16];std::snprintf(hash,sizeof hash,"%08x",texpack::hash(rgba.data(),8,8));
    auto pack=folder/"mods"/"textures"/fs::path(u8"\u66ff\u6362");fs::create_directories(pack);
    auto png=pack/(std::string(hash)+".png");require(texpack::save_png(host_file::utf8(png),rgba.data(),8,8),"Unicode PNG write");
    texpack::Image image;require(texpack::load_png(host_file::utf8(png),image)&&image.px==rgba,"Unicode PNG read preserves pixels");
    texpack::init(host_file::utf8(folder)+"/",true,true);require(texpack::pack_files()==1,"Unicode pack enumeration");
    auto replacement=texpack::on_upload(1,rgba.data(),8,8);require(replacement&&replacement->px==rgba,"replacement discovered/opened using full Unicode path");
    require(fs::exists(folder/"mods"/"textures"/"_dump"/(std::string(hash)+".png")),"Unicode texture dump path");
    auto redirected=folder/fs::path(u8"\u91cd\u5b9a\u5411.log");f=host_file::open(name,"rb");require(f,"synthetic reopen source");
    require(host_file::reopen(host_file::utf8(redirected),"wb",f),"Unicode freopen equivalent");std::fputs("redirect",f);std::fclose(f);
    require(read(redirected)=="redirect","Unicode reopen contents");
    fs::remove_all(root);std::cout<<"Synthetic Unicode asset/library/log/PNG/mod/reopen tests passed; no game/profile loaded\n";return 0;
}catch(const std::exception& e){logging::close_file();std::cerr<<e.what()<<'\n';if(!root.empty()){std::error_code ec;fs::remove_all(root,ec);}return 1;}}
