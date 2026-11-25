#include "save_fixture.h"
#include "save_io.h"
#include "save_validation.h"
#include "temporary_profile.h"
#include "settings.h"
#include <chrono>
#include <algorithm>
#include <cerrno>
#include <cstdarg>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <mutex>
#include <thread>
#include <fcntl.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace {
namespace fs = std::filesystem;
using save_fixture::Bytes;
std::mutex logs_mutex;
std::vector<std::string> logs;
save_io::Stage termination_stage;
void terminate_at_stage(save_io::Stage stage) { if (stage == termination_stage) std::_Exit(0); }
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
Bytes read_file(const fs::path& path) {
    std::ifstream file(path,std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>(file),{});
}
void raw_write(const fs::path& path, const Bytes& bytes) {
    std::ofstream f(path,std::ios::binary|std::ios::trunc);
    f.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));
    require(bool(f),"synthetic fixture write failed");
}
struct Inject {
    save_io::Faults faults;
    explicit Inject(save_io::Faults f) : faults(f) { save_io::test_faults(&faults); }
    ~Inject() { save_io::test_faults(nullptr); }
};
bool save(const fs::path& path, const Bytes& bytes, bool serializer = true) {
    save_io::begin_save();
    int fd = save_io::open(path,O_WRONLY|O_CREAT|O_TRUNC);
    bool okay = fd >= 0;
    if (okay) {
        okay = save_io::write(fd,bytes.data(),bytes.size()) == int64_t(bytes.size());
        okay = save_io::close(fd) == 0 && okay;
    }
    return save_io::finish_save(serializer && okay);
}
fs::path backup(const fs::path& p) { auto b=p; b += ".bak"; return b; }
void no_temporary(const fs::path& root) {
    for (const auto& e : fs::recursive_directory_iterator(root))
        require(!e.path().filename().string().starts_with(".ds-save-"),"owned temporary leaked after completed operation");
}
void validation() {
    using save_validation::Status;
    auto valid=save_fixture::valid();
    require(save_validation::inspect(valid).status==Status::valid,"synthetic valid file rejected");
    for (size_t n=0;n<valid.size();n++) {
        Bytes short_file(valid.begin(),valid.begin()+n);
        require(save_validation::inspect(short_file).status!=Status::valid,"truncated input accepted as valid");
    }
    for (const char* tag : {"STRU","FIEL","ENUM","OHDR","DATA","STRS","SYMB"}) {
        auto bad=valid;size_t p=save_fixture::section(bad,tag);
        bad[p+8]^=1;
        require(save_validation::inspect(bad).status==Status::invalid,"section checksum failure missed");
    }
    auto bad=valid;save_fixture::put32(bad,4,99);
    require(save_validation::inspect(bad).status==Status::unknown,"unfamiliar version guessed or banned");
    auto corrupt=[&](const char* tag,size_t offset,uint32_t value,bool half=false) {
        auto b=valid;size_t p=save_fixture::section(b,tag)+12;
        if(half)save_fixture::put16(b,p+offset,uint16_t(value));else save_fixture::put32(b,p+offset,value);
        save_fixture::rehash(b,tag);
        require(save_validation::inspect(b).status==Status::invalid,"malformed schema/reference accepted");
    };
    corrupt("STRS",0,0xffffffffu);
    corrupt("STRS",4,0xffffffffu);
    corrupt("STRU",0,999,true);
    corrupt("STRU",2,999,true);
    corrupt("FIEL",0,999,true);
    corrupt("OHDR",4,0xfffffff8u);
    corrupt("DATA",2,0xffff,true);
    corrupt("DATA",24,999,true);
    corrupt("DATA",34,999);
    corrupt("DATA",38,999,true);
    corrupt("DATA",48,0xffffffffu);
    corrupt("DATA",52,999);
    auto duplicate=valid;save_fixture::chunk(duplicate,"DATA",{});
    require(save_validation::inspect(duplicate).status==Status::invalid,"duplicate section accepted");
    auto missing=valid;size_t p=save_fixture::section(missing,"STRU");missing[p]='X';
    require(save_validation::inspect(missing).status==Status::invalid,"missing required section accepted");
    auto unknown=valid;p=save_fixture::section(unknown,"FIEL");save_fixture::put16(unknown,p+14,99);save_fixture::rehash(unknown,"FIEL");
    require(save_validation::inspect(unknown).status==Status::unknown,"unknown field type given a guessed width");
    // Deterministic malformed-input smoke/fuzz corpus, entirely generated.
    uint32_t random=123456;
    for (unsigned i=0;i<20000;i++) {
        random=random*1664525+1013904223;
        Bytes bytes(i%512);
        for(auto& b:bytes){random=random*1664525+1013904223;b=uint8_t(random>>24);}
        if(i%2==0 && bytes.size()>=8){std::copy_n("SBIN",4,bytes.begin());save_fixture::put32(bytes,4,2);}
        save_validation::inspect(bytes);
    }
    for (unsigned i=0;i<30000;i++) {
        Bytes bytes=valid;
        const char* tags[]={"STRU","FIEL","OHDR","DATA","STRS","SYMB"};
        const char* tag=tags[i%6];size_t p=save_fixture::section(bytes,tag);
        uint32_t size=uint32_t(bytes[p+4])|uint32_t(bytes[p+5])<<8|uint32_t(bytes[p+6])<<16|uint32_t(bytes[p+7])<<24;
        for(unsigned j=0;j<1+i%5;j++){
            random=random*1664525+1013904223;
            bytes[p+12+random%size]^=uint8_t((random>>24)|1);
        }
        save_fixture::rehash(bytes,tag); // exercise deep bounds/reference logic, not only checksums
        save_validation::inspect(bytes);
    }
}
void successful_saving(const fs::path& root) {
    auto path=root/"00_CheckpointSave.sb";
    auto a=save_fixture::valid(1),b=save_fixture::valid(2);
    require(save(path,a),"first ordinary save failed");
    require(read_file(path)==a && !fs::exists(backup(path)),"first save bytes/backup wrong");
    save_io::begin_save();
    int fd=save_io::open(path,O_RDWR|O_CREAT|O_TRUNC);
    require(fd>=0,"staged open failed");
    require(read_file(path)==a,"open truncated final before commit");
    require(save_io::write(fd,b.data(),b.size())==int64_t(b.size()),"write failed");
    require(save_io::seek(fd,0,SEEK_SET)==0,"staged seek failed");
    Bytes seen(b.size());require(save_io::read(fd,seen.data(),seen.size())==int64_t(seen.size())&&seen==b,"staged readback/position failed");
    require(save_io::sync(fd)==0 && save_io::close(fd)==0,"sync/close failed");
    require(read_file(path)==a,"close published before serializer result");
    require(save_io::finish_save(true),"successful serializer was blocked");
    require(read_file(path)==b && read_file(backup(path))==a,"atomic replacement/backup bytes wrong");
    { Inject f({.write_limit=3,.interrupted_writes=4}); require(save(path,a),"partial/interrupted writes not completed"); }
    for(uint32_t i=0;i<30;i++)require(save(path,save_fixture::valid(i)),"repeated legitimate save blocked");
    no_temporary(root);
    auto international=root/fs::path(u8"sauvegarde-\u00e9-\u51b7");
    fs::create_directories(international);
    require(save(international/"01_LevelSave.sb",b),"Unicode profile save failed");
    auto unknown=a;save_fixture::put32(unknown,4,99);
    require(save(root/"future.sb",unknown),"unfamiliar version prevented all future-format saves");
}
void failure_paths(const fs::path& root) {
    auto a=save_fixture::valid(100),b=save_fixture::valid(200);
    auto path=root/"02_LevelSave.sb";
    require(save(path,a),"failure setup save failed");
    auto test=[&](save_io::Faults faults) {
        { Inject injected(faults);require(!save(path,b),"injected failure reported save success"); }
        require(read_file(path)==a,"failed save damaged existing final");
        no_temporary(root);
        require(save(root/"recovery-check.sb",b),"successful saving stayed disabled after a failure");
    };
    test({.zero_write=true});
    test({.write_error=true});
    test({.bytes_until_error=19});
    test({.sync_error=true});
    test({.close_error=true});
    test({.replace_error=true});
    test({.backup_error=true});
    test({.read_error=true});
    require(!save(path,b,false)&&read_file(path)==a,"serializer failure published an otherwise complete file");
    auto bad=b;bad.resize(20);
    require(!save(path,bad)&&read_file(path)==a,"truncated staged save published");
    save_io::begin_save();
    int fd=save_io::open(path,O_WRONLY|O_CREAT|O_TRUNC);
    require(fd>=0 && save_io::write(fd,b.data(),b.size())==int64_t(b.size()),"late close setup failed");
    require(!save_io::finish_save(true),"save success claimed with an unclosed stream");
    require(save_io::close(fd)<0 && read_file(path)==a,"late close published after scope rejection");
    no_temporary(root);
    auto first=root/"first-failed.sb";
    { Inject f({.zero_write=true});require(!save(first,a),"failed first save reported success"); }
    require(!fs::exists(first),"failed first save left a corrupt final");
    { Inject f({.directory_sync_error=true}); require(!save(first,b),"failed directory durability reported success"); }
    require(read_file(first)==b,"post-rename durability failure pretended to roll back");
    auto bak=backup(path);raw_write(bak,a);raw_write(path,Bytes{'b','a','d'});
    require(save(path,b)&&read_file(path)==b&&read_file(bak)==a,"corrupt prior final replaced the known-good backup");
    fs::permissions(path,fs::perms::owner_write|fs::perms::group_write|fs::perms::others_write,fs::perm_options::remove);
    require(!save(path,a)&&read_file(path)==b,"read-only final was overwritten");
    fs::permissions(path,fs::perms::owner_write,fs::perm_options::add);
    int invalid=save_io::open(root/"missing.sb",O_RDONLY);
    require(invalid<0&&errno==ENOENT,"missing file misclassified");
    require(save_io::sync(-1)<0&&save_io::close(-1)<0,"invalid fd sync/close reported success");
    auto directory=root/"directory.sb";fs::create_directory(directory);
    require(!save(directory,a)&&fs::is_directory(directory),"save replaced a directory");
    auto target=root/"link-target.sb";require(save(target,a),"link setup failed");
    std::error_code ec;auto link=root/"link.sb";fs::create_symlink(target,link,ec);
    if(!ec){require(!save(link,b)&&read_file(target)==a,"transaction followed a save symlink");fs::remove(link);}
    auto backup_directory=backup(path);fs::remove(backup_directory);fs::create_directory(backup_directory);
    require(!save(path,a)&&read_file(path)==b,"failed backup target damaged final");fs::remove(backup_directory);
}
void stdio_and_modes(const fs::path& root) {
    auto path=root/"settings.sb";auto a=save_fixture::valid(1),b=save_fixture::valid(2);
    require(save(path,a),"stdio setup failed");
    save_io::begin_save();
    auto* f=save_io::fopen(path,"w+b");require(f!=nullptr,"stdio open failed");
    { Inject injected({.write_limit=5,.interrupted_writes=2});require(save_io::fwrite(b.data(),1,b.size(),f)==b.size(),"stdio full write failed"); }
    require(save_io::fflush(f)==0&&save_io::fclose(f)==0&&read_file(path)==a,"stdio close published before serializer result");
    require(save_io::finish_save(true)&&read_file(path)==b,"stdio commit failed");
    save_io::begin_save();f=save_io::fopen(path,"wb");require(f!=nullptr,"failed-stdio setup failed");
    { Inject injected({.bytes_until_error=17});require(save_io::fwrite(a.data(),1,a.size(),f)<a.size(),"stdio partial failure hidden"); }
    require(save_io::fclose(f)==EOF&&!save_io::finish_save(true)&&read_file(path)==b,"stdio failure published partial data");
    save_io::begin_save();f=save_io::fopen(path,"wb");require(f!=nullptr,"overflow stdio setup failed");
    require(save_io::fwrite(a.data(),SIZE_MAX,2,f)==0,"stdio overflow accepted");
    require(save_io::fclose(f)==EOF&&!save_io::finish_save(true)&&read_file(path)==b,"stdio overflow did not poison transaction");
    save_io::begin_save();f=save_io::fopen(path,"wb");require(f!=nullptr,"flush failure setup failed");
    require(save_io::fwrite(a.data(),1,a.size(),f)==a.size(),"flush failure data write failed");
    { Inject injected({.flush_error=true});require(save_io::fflush(f)==EOF,"late flush failure hidden"); }
    require(save_io::fclose(f)==EOF&&!save_io::finish_save(true)&&read_file(path)==b,"flush failure published a save");
    auto plain=root/"ordinary.bin";
    int fd=save_io::open(plain,O_WRONLY|O_CREAT|O_TRUNC);
    { Inject injected({.write_limit=3});require(save_io::write(fd,a.data(),a.size())==3,"generic POSIX short write semantics changed"); }
    require(save_io::close(fd)==0&&read_file(plain).size()==3,"generic file silently given save validation");
    int pipe[2];
#ifdef _WIN32
    require(_pipe(pipe,1024,O_BINARY)==0,"synthetic pipe creation failed");
#else
    require(::pipe(pipe)==0,"synthetic pipe creation failed");
#endif
    { Inject injected({.write_limit=3});require(save_io::write(pipe[1],a.data(),a.size())==3,"pipe short-write semantics changed"); }
    uint8_t small[3];require(save_io::read(pipe[0],small,3)==3,"synthetic pipe read failed");
    require(save_io::close(pipe[0])==0&&save_io::close(pipe[1])==0,"synthetic pipe close failed");
    fd=save_io::open(path,O_WRONLY|O_CREAT|O_EXCL|O_TRUNC);
    require(fd<0&&errno==EEXIST&&read_file(path)==b,"exclusive save open replaced existing file");
    require(save_io::fopen(path,"")==nullptr&&errno==EINVAL,"empty mode accepted");
}
void concurrency_and_protection(const fs::path& root) {
    auto path=root/"03_CheckpointSave.sb";auto a=save_fixture::valid(1);
    require(save(path,a),"concurrency existing-final setup failed");
    int fd=save_io::open(path,O_WRONLY|O_CREAT|O_TRUNC);require(fd>=0,"writer setup failed");
    int result=0;
    std::thread competing([&]{result=save_io::open(path,O_WRONLY|O_CREAT|O_TRUNC);});competing.join();
    require(result<0,"concurrent same-path writer allowed");
    require(save_io::open(path,O_WRONLY|O_APPEND)<0,"append writer bypassed an active save transaction");
#ifdef _WIN32
    auto alias=path.parent_path()/"03_CHECKPOINTSAVE.SB";
    require(save_io::open(alias,O_WRONLY|O_TRUNC)<0,"case alias bypassed active Windows save writer");
    require(read_file(path)==a,"case alias truncated existing final");
#endif
    require(save_io::write(fd,a.data(),a.size())==int64_t(a.size())&&save_io::close(fd)==0,"rejected competitor poisoned legitimate writer");
    save_io::ProfileLock first,second;
    require(first.acquire(root)&&!second.acquire(root),"profile writer lock not exclusive");
    first.release();require(second.acquire(root),"profile lock was not released");second.release();
    auto original=root/"original";fs::create_directories(original);raw_write(original/"00_LevelSave.sb",a);
    raw_write(original/"port_settings.ini",Bytes{'x','=','1','\n'});
    TemporaryProfile temporary;
    temporary.prepare(original);
    save_io::set_profile_root(temporary.data_path());
    require(save(temporary.data_path()/"00_LevelSave.sb",save_fixture::valid(9)),"--no-save temporary progression blocked");
    settings::load(save_io::path_utf8(temporary.data_path()/"port_settings.ini"));
    settings::set_int("x",9);
    require(settings::save(),"--no-save temporary settings blocked");
    require(read_file(original/"00_LevelSave.sb")==a&&read_file(original/"port_settings.ini")==Bytes({'x','=','1','\n'}),"temporary profile changed originals");
    require(!fs::exists(original/".deadspace-profile.lock")&&!fs::exists(backup(original/"00_LevelSave.sb")),"temporary session left sidecars in originals");
    require(!save(original/"escape.sb",a),"transaction published outside configured disposable profile");
    require(!temporary.discard(),"temporary profile cleanup failed");save_io::set_profile_root(root);
    auto text=root/"port_settings.ini";raw_write(text,Bytes{'o','l','d'});
    { Inject injected({.replace_error=true});require(!save_io::atomic_text(text,"new"),"failed settings commit reported success"); }
    require(read_file(text)==Bytes({'o','l','d'}),"failed preferences write truncated original");
    require(save_io::atomic_text(text,"new")&&read_file(text)==Bytes({'n','e','w'}),"normal preference save blocked after failure");
    no_temporary(root);
}
}
namespace logging {
bool g_verbose = false;
void write(char level,const char* format,...) {
    char text[8192];va_list args;va_start(args,format);std::vsnprintf(text,sizeof text,format,args);va_end(args);
    std::lock_guard lock(logs_mutex);logs.push_back(std::string(1,level)+" "+text);
}
}
int main(int argc,char** argv) {
    try {
        if(argc>2&&std::string(argv[1])=="--inspect") {
            auto bytes=read_file(save_io::utf8_path(argv[2]));auto result=save_validation::inspect(bytes);
            std::cout<<save_validation::name(result.status)<<" bytes="<<bytes.size()<<" reason="<<result.reason<<'\n';
            return result.status==save_validation::Status::invalid?1:0;
        }
        if(argc>2&&std::string(argv[1])=="--lock") {
            save_io::ProfileLock lock;if(!lock.acquire(save_io::utf8_path(argv[2])))return 2;
            std::cout<<"LOCKED\n"<<std::flush;std::string line;std::getline(std::cin,line);return 0;
        }
        if(argc>2&&std::string(argv[1])=="--abandon") {
            auto root=save_io::utf8_path(argv[2]);auto a=save_fixture::valid(1),b=save_fixture::valid(2);
            require(save(root/"00_LevelSave.sb",a),"abandon setup failed");
            int fd=save_io::open(root/"00_LevelSave.sb",O_WRONLY|O_CREAT|O_TRUNC);
            require(fd>=0&&save_io::write(fd,b.data(),b.size())==int64_t(b.size()),"abandon write failed");
            std::_Exit(0);  // synthetic transaction termination, never game execution
        }
        if(argc>3&&std::string(argv[1])=="--terminate-at") {
            std::string step=argv[2];auto root=save_io::utf8_path(argv[3]);
            auto a=save_fixture::valid(1),b=save_fixture::valid(2);
            require(save(root/"00_LevelSave.sb",a),"termination setup failed");
            termination_stage=step=="sync"?save_io::Stage::data_synced:step=="close"?save_io::Stage::stream_closed:
                              step=="backup"?save_io::Stage::backup_ready:save_io::Stage::final_replaced;
            save_io::Faults f;f.after_stage=terminate_at_stage;save_io::test_faults(&f);
            save(root/"00_LevelSave.sb",b);
            throw std::runtime_error("requested transaction termination stage was not reached");
        }
        auto root=fs::temp_directory_path()/("deadspace-save-io-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        require(fs::create_directory(root),"cannot create unique synthetic test root");
        save_io::set_profile_root(root);
        validation();successful_saving(root);failure_paths(root);stdio_and_modes(root);concurrency_and_protection(root);
        require(std::any_of(logs.begin(),logs.end(),[](const std::string& s){return s.find("COMMITTED")!=std::string::npos;}),"successful saves have no commit diagnostic");
        require(std::any_of(logs.begin(),logs.end(),[](const std::string& s){return s.find("FAILED")!=std::string::npos&&s.find("path=")!=std::string::npos;}),"failures lack operation/path diagnostic");
        fs::remove_all(root); // only this test's exclusively-created synthetic root
        std::cout<<"save validation, transaction, I/O failure and profile protection tests passed\n";
    } catch(const std::exception& e) { save_io::test_faults(nullptr);std::cerr<<e.what()<<'\n';return 1; }
}
