// Read-only Windows handle type census for one explicitly identified process.
// No handle names, memory dumps, debug privilege, suspension or process control.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h>
#include "cli_arguments.h"
#include "host_file.h"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {
using QueryProcess = NTSTATUS (NTAPI*)(HANDLE, PROCESSINFOCLASS, PVOID, ULONG, PULONG);
using QueryObject = NTSTATUS (NTAPI*)(HANDLE, OBJECT_INFORMATION_CLASS, PVOID, ULONG, PULONG);
struct Handle {
    HANDLE value=nullptr;
    ~Handle(){if(value && value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
};
// ProcessHandleInformation (51), Windows 8+. This is a process-local table,
// not SystemExtendedHandleInformation or a system-wide handle enumeration.
struct TableEntry {
    HANDLE value;
    ULONG_PTR handle_count,pointer_count;
    ACCESS_MASK access;
    ULONG type,attributes,reserved;
};
struct Table { ULONG_PTR count,reserved; TableEntry entries[1]; };
struct TypeCount { uint64_t count=0; std::string name="unknown"; DWORD type_error=0; };
uint64_t ticks(FILETIME time){return uint64_t(time.dwHighDateTime)<<32 | time.dwLowDateTime;}
std::string utc() {
    SYSTEMTIME time;GetSystemTime(&time);
    char text[40];std::snprintf(text,sizeof text,"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
        time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,time.wMilliseconds);
    return text;
}
bool number(const std::string& text,uint64_t& out) {
    auto parsed=std::from_chars(text.data(),text.data()+text.size(),out);
    return !text.empty() && parsed.ec==std::errc{} && parsed.ptr==text.data()+text.size();
}
std::string type_name(QueryObject query,HANDLE handle,DWORD& error) {
    error=0;
    std::vector<unsigned char> buffer(65536);
    ULONG length=0;
    const auto status=query(handle,ObjectTypeInformation,buffer.data(),ULONG(buffer.size()),&length);
    if(status<0){error=DWORD(status);return "unknown";}
    const auto* name=reinterpret_cast<const UNICODE_STRING*>(buffer.data());
    const auto start=reinterpret_cast<uintptr_t>(buffer.data()), pointer=reinterpret_cast<uintptr_t>(name->Buffer);
    if(name->Length%2 || name->Length>128 || pointer<start || pointer>start+buffer.size() || name->Length>start+buffer.size()-pointer){error=ERROR_INVALID_DATA;return "unknown";}
    std::string result;
    for(unsigned i=0;i<name->Length/2;++i) {
        wchar_t ch=name->Buffer[i];
        result+=ch>=L'0' && ch<=L'z' && ch!=L','?char(ch):'_';
    }
    return result.empty()?"unknown":result;
}
}

int main(int argc,char** argv) {
    cli::Arguments arguments(argc,argv);
    uint64_t pid=0,started=0;std::string output;
    for(int i=1;i<arguments.count;i+=2) {
        if(i+1==arguments.count)return 2;
        const std::string key=arguments.values[i],value=arguments.values[i+1];
        if(key=="--pid"){if(!number(value,pid))return 2;}
        else if(key=="--started"){if(!number(value,started))return 2;}
        else if(key=="--out")output=value;
        else return 2;
    }
    if(!pid || pid>MAXDWORD || !started || output.empty())return 2;
    const auto begin=std::chrono::steady_clock::now();
    const auto when=utc();
    DWORD error=0;uint64_t total=0;bool valid=false;
    std::map<ULONG,TypeCount> types;
    Handle process{OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_DUP_HANDLE,FALSE,DWORD(pid))};
    if(!process.value)error=GetLastError();
    else {
        FILETIME created{},exited{},kernel{},user{};
        if(!GetProcessTimes(process.value,&created,&exited,&kernel,&user))error=GetLastError();
        else if(ticks(created)!=started)error=ERROR_INVALID_PARAMETER; // refuse a recycled PID
        else {
            auto module=GetModuleHandleW(L"ntdll.dll");
            auto query=reinterpret_cast<QueryProcess>(GetProcAddress(module,"NtQueryInformationProcess"));
            auto object=reinterpret_cast<QueryObject>(GetProcAddress(module,"NtQueryObject"));
            if(!query || !object)error=ERROR_PROC_NOT_FOUND;
            else {
                std::vector<unsigned char> buffer(65536);
                NTSTATUS status=0;ULONG needed=0;
                for(unsigned attempt=0;attempt<8;++attempt) {
                    status=query(process.value,PROCESSINFOCLASS(51),buffer.data(),ULONG(buffer.size()),&needed);
                    if(status>=0)break;
                    if(status!=NTSTATUS(0xc0000004L) && status!=NTSTATUS(0xc0000023L))break;
                    const auto next=std::max<size_t>(needed,buffer.size()*2);
                    if(next>4*1024*1024){error=ERROR_BUFFER_OVERFLOW;break;}
                    buffer.resize(next);
                }
                if(!error && status<0)error=DWORD(status);
                if(!error) {
                    const auto* table=reinterpret_cast<const Table*>(buffer.data());
                    const auto available=std::min<size_t>(needed?needed:buffer.size(),buffer.size());
                    if(available<offsetof(Table,entries) || table->count>32768 ||
                        table->count>(available - offsetof(Table,entries))/sizeof(TableEntry))error=ERROR_INVALID_DATA;
                    else {
                        valid=true;total=table->count;
                        for(size_t i=0;i<table->count;++i) {
                            const auto& entry=table->entries[i];auto& count=types[entry.type];++count.count;
                            if(count.name!="unknown" || count.count>3)continue;
                            Handle duplicate;
                            if(!DuplicateHandle(process.value,entry.value,GetCurrentProcess(),&duplicate.value,0,FALSE,DUPLICATE_SAME_ACCESS))count.type_error=GetLastError();
                            else {
                                // A target handle may close/reuse its numeric value
                                // between the table snapshot and DuplicateHandle.
                                // Verify the duplicate's global type index before
                                // using its name to label the captured bucket.
                                std::vector<unsigned char> own(65536);
                                ULONG length=0;
                                const auto own_status=query(GetCurrentProcess(),PROCESSINFOCLASS(51),own.data(),ULONG(own.size()),&length);
                                const auto* own_table=reinterpret_cast<const Table*>(own.data());
                                bool same_type=false;
                                if(own_status>=0 && own_table->count<=(own.size()-offsetof(Table,entries))/sizeof(TableEntry)) {
                                    for(size_t j=0;j<own_table->count;++j)
                                        if(own_table->entries[j].value==duplicate.value && own_table->entries[j].type==entry.type){same_type=true;break;}
                                }
                                if(same_type)count.name=type_name(object,duplicate.value,count.type_error);
                                else count.type_error=ERROR_INVALID_DATA;
                            }
                        }
                    }
                }
            }
        }
    }
    const auto path=host_file::path(output);
    std::error_code ec;
    const bool header=!std::filesystem::exists(path,ec) || std::filesystem::file_size(path,ec)==0;
    std::ofstream file(path,std::ios::app);
    if(!file)return 3;
    if(header)file<<"utc,pid,valid,type_index,type_name,count,total_handles,error,type_error,capture_ms\n";
    const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
    if(types.empty())types[0]={};
    for(const auto& [index,count]:types)
        file<<when<<','<<pid<<','<<valid<<','<<index<<','<<count.name<<','<<count.count<<','<<total<<','<<error<<','<<count.type_error<<','<<std::fixed<<std::setprecision(3)<<elapsed<<'\n';
    file.flush();
    return file && valid?0:1;
}
