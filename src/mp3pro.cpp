#include "encoder.h"
#include <shlwapi.h>
#include <sstream>

namespace ttp::enc {
namespace {
constexpr const char* output_token = "TTPLAYER_MP3PRO_OUTPUT.mp3";
constexpr DWORD session_options = 2147480000;
using Open = HGLOBAL (WINAPI*)(LPCSTR,DWORD,WORD,WORD,DWORD,DWORD*,DWORD);
using Write = DWORD (WINAPI*)(HGLOBAL,const void*,DWORD);
using Close = void (WINAPI*)(HGLOBAL);
using Configure = DWORD (WINAPI*)(HWND,HMODULE,DWORD,WORD,WORD,DWORD);
struct Filter {
    struct Import { ULONG_PTR* slot; ULONG_PTR original, replacement; };
    std::vector<Import> imports;
    std::mutex mutex;
    HMODULE dll{};
    Open open{}; Write write{}; Close close{}; Configure configure{};
    bool busy{};
    std::wstring ini, output;
    HANDLE file{INVALID_HANDLE_VALUE};
    DWORD io_error{};
    void restore_imports() noexcept {
        for (const auto& item:imports) {
            DWORD previous{}, ignored{};
            if (*item.slot==item.replacement && VirtualProtect(item.slot,sizeof(ULONG_PTR),PAGE_READWRITE,&previous)) {
                *item.slot=item.original;
                VirtualProtect(item.slot,sizeof(ULONG_PTR),previous,&ignored);
            }
        }
        imports.clear();
    }
    ~Filter() { if (dll) { restore_imports(); FreeLibrary(dll); } }
};
// Avoid a TLS-guarded local static in a dynamically loaded XP DLL.
Filter filter_state;
Filter& filter() { return filter_state; }
std::wstring ansi(const char* s) {
    if (!s) return {};
    int n = MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
    require(n > 0, E_INVALIDARG);
    std::wstring w(n, L'\0'); MultiByteToWideChar(CP_ACP,0,s,-1,w.data(),n); w.pop_back(); return w;
}
bool profile(const char* file) { return file && !_stricmp(PathFindFileNameA(file), "coolmp3.ini"); }
DWORD WINAPI profile_string(LPCSTR section,LPCSTR key,LPCSTR def,LPSTR out,DWORD cap,LPCSTR file) {
    if (!profile(file)) return GetPrivateProfileStringA(section,key,def,out,cap,file);
    if (!out || !cap) return 0;
    DWORD result{};
    HRESULT hr = protect([&]() -> HRESULT {
        require(cap <= 1024 * 1024, E_INVALIDARG);
        auto s=ansi(section), k=ansi(key), d=ansi(def);
        std::vector<wchar_t> wide_buffer(cap + 2);
        DWORD n = GetPrivateProfileStringW(section?s.c_str():nullptr,key?k.c_str():nullptr,
            d.c_str(),wide_buffer.data(),cap,filter().ini.c_str());
        const bool multi = !section || !key;
        int bytes = WideCharToMultiByte(CP_ACP,0,wide_buffer.data(),int(n + (multi?2:1)),nullptr,0,nullptr,nullptr);
        std::vector<char> buffer(bytes);
        WideCharToMultiByte(CP_ACP,0,wide_buffer.data(),int(n + (multi?2:1)),buffer.data(),bytes,nullptr,nullptr);
        DWORD terminators = multi && cap > 1 ? 2 : 1;
        result = std::min<DWORD>(DWORD(bytes) - (multi?2:1),cap-terminators);
        std::memcpy(out,buffer.data(),result); std::memset(out+result,0,terminators);
        return S_OK;
    });
    if (FAILED(hr)) { out[0]=0; SetLastError(ERROR_NOT_ENOUGH_MEMORY); return 0; }
    return result;
}
UINT WINAPI profile_int(LPCSTR section,LPCSTR key,INT def,LPCSTR file) {
    if (!profile(file)) return GetPrivateProfileIntA(section,key,def,file);
    UINT value=def;
    protect([&]() -> HRESULT { auto s=ansi(section),k=ansi(key);
        value=GetPrivateProfileIntW(s.c_str(),k.c_str(),def,filter().ini.c_str()); return S_OK; });
    return value;
}
BOOL WINAPI profile_write(LPCSTR section,LPCSTR key,LPCSTR value,LPCSTR file) {
    if (!profile(file)) return WritePrivateProfileStringA(section,key,value,file);
    BOOL ok=FALSE;
    protect([&]() -> HRESULT { auto s=ansi(section),k=ansi(key),v=ansi(value);
        ok=WritePrivateProfileStringW(section?s.c_str():nullptr,key?k.c_str():nullptr,
            value?v.c_str():nullptr,filter().ini.c_str()); return S_OK; });
    return ok;
}
HANDLE WINAPI create_file(LPCSTR path,DWORD access,DWORD share,LPSECURITY_ATTRIBUTES sa,
                         DWORD creation,DWORD flags,HANDLE source) {
    auto& f=filter();
    if (path && !std::strcmp(path,output_token) && f.busy && !f.output.empty()) {
        f.file=CreateFileW(f.output.c_str(),access,share,sa,creation,flags,source);
        if (f.file==INVALID_HANDLE_VALUE) f.io_error=GetLastError();
        return f.file;
    }
    return CreateFileA(path,access,share,sa,creation,flags,source);
}
BOOL WINAPI write_file(HANDLE file,LPCVOID data,DWORD bytes,LPDWORD written,LPOVERLAPPED overlapped) {
    const BOOL ok=WriteFile(file,data,bytes,written,overlapped);
    auto& f=filter();
    if (file==f.file && (!ok || (written && *written!=bytes)))
        f.io_error=ok?ERROR_WRITE_FAULT:GetLastError();
    return ok;
}
void patch_imports(Filter& f, HMODULE dll) {
    auto* base=reinterpret_cast<BYTE*>(dll);
    auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt=reinterpret_cast<IMAGE_NT_HEADERS*>(base+dos->e_lfanew);
    auto rva=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    require(rva != 0);
    struct Patch { FARPROC old_address; FARPROC replacement; bool found{}; };
    HMODULE kernel=GetModuleHandleW(L"kernel32.dll");
    Patch patches[]{
        {GetProcAddress(kernel,"GetPrivateProfileStringA"),reinterpret_cast<FARPROC>(profile_string)},
        {GetProcAddress(kernel,"GetPrivateProfileIntA"),reinterpret_cast<FARPROC>(profile_int)},
        {GetProcAddress(kernel,"WritePrivateProfileStringA"),reinterpret_cast<FARPROC>(profile_write)},
        {GetProcAddress(kernel,"CreateFileA"),reinterpret_cast<FARPROC>(create_file)},
        {GetProcAddress(kernel,"WriteFile"),reinterpret_cast<FARPROC>(write_file)}};
    auto replace=[&](ULONG_PTR* slot) {
        for (auto& patch:patches) {
            if (*slot!=ULONG_PTR(patch.old_address)) continue;
            f.imports.push_back({slot,*slot,ULONG_PTR(patch.replacement)});
            DWORD previous{},ignored{};
            require(!!VirtualProtect(slot,sizeof(ULONG_PTR),PAGE_READWRITE,&previous));
            *slot=ULONG_PTR(patch.replacement);
            VirtualProtect(slot,sizeof(ULONG_PTR),previous,&ignored);
            patch.found=true;
        }
    };
    auto* imports=reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base+rva);
    for (;imports->Name;++imports) {
        auto* thunk=reinterpret_cast<IMAGE_THUNK_DATA*>(base+imports->FirstThunk);
        for (;thunk->u1.Function;++thunk) replace(&thunk->u1.Function);
    }
    // UPX retains its loader import directory, while restoring the filter's
    // original IAT elsewhere. Recover only slots used by x86 indirect import
    // call/jump instructions; never patch arbitrary data or codec instructions.
    auto* section=IMAGE_FIRST_SECTION(nt);
    const auto image_end=ULONG_PTR(base)+nt->OptionalHeader.SizeOfImage;
    for (WORD index=0;index<nt->FileHeader.NumberOfSections;++index,++section) {
        if (!(section->Characteristics&IMAGE_SCN_MEM_EXECUTE)) continue;
        auto begin=ULONG_PTR(base)+section->VirtualAddress;
        auto end=std::min(image_end,begin+section->Misc.VirtualSize);
        while (begin<end) {
            MEMORY_BASIC_INFORMATION page{};
            require(!!VirtualQuery(reinterpret_cast<void*>(begin),&page,sizeof(page)));
            auto page_end=std::min(end,ULONG_PTR(page.BaseAddress)+page.RegionSize);
            if (page.State==MEM_COMMIT && !(page.Protect&(PAGE_GUARD|PAGE_NOACCESS))) {
                for (auto address=begin;address+6<=page_end;++address) {
                    auto* instruction=reinterpret_cast<BYTE*>(address);
                    if (instruction[0]!=0xff || (instruction[1]!=0x15 && instruction[1]!=0x25)) continue;
                    ULONG_PTR slot{}; std::memcpy(&slot,instruction+2,sizeof(slot));
                    if (slot%sizeof(ULONG_PTR) || slot<ULONG_PTR(base) || slot+sizeof(ULONG_PTR)>image_end) continue;
                    MEMORY_BASIC_INFORMATION target{};
                    if (!VirtualQuery(reinterpret_cast<void*>(slot),&target,sizeof(target)) || target.State!=MEM_COMMIT ||
                        target.Protect&(PAGE_GUARD|PAGE_NOACCESS)) continue;
                    replace(reinterpret_cast<ULONG_PTR*>(slot));
                }
            }
            require(page_end>begin); begin=page_end;
        }
    }
    for (const auto& patch:patches) require(patch.found,HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND));
}
void load(Filter& f) {
    if (f.dll) return;
    f.ini=settings_file(L"coolmp3.ini");
    const auto path=module_directory()+L"\\fhtpro.flt";
    HMODULE dll=LoadLibraryW(path.c_str());
    require(dll!=nullptr,HRESULT_FROM_WIN32(GetLastError()));
    try {
        f.open=reinterpret_cast<Open>(GetProcAddress(dll,"OpenFilterOutput"));
        f.write=reinterpret_cast<Write>(GetProcAddress(dll,"WriteFilterOutput"));
        f.close=reinterpret_cast<Close>(GetProcAddress(dll,"CloseFilterOutput"));
        f.configure=reinterpret_cast<Configure>(GetProcAddress(dll,"FilterGetOptions"));
        require(f.open && f.write && f.close,HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND));
        patch_imports(f,dll);
        f.dll=dll;
    } catch (...) { f.restore_imports(); FreeLibrary(dll); throw; }
}
std::wstring configuration(Filter& f,DWORD id,DWORD rate,WORD channels) {
    auto key=L"Key"+std::to_wstring(id);
    wchar_t value[2048]{};
    GetPrivateProfileStringW(L"Configurations",key.c_str(),L"",value,DWORD(std::size(value)),f.ini.c_str());
    std::vector<std::wstring> values;
    std::wistringstream input(value); std::wstring part;
    while (std::getline(input,part,L',')) values.push_back(part);
    if (values.size()!=22) {
        values.clear();
        std::wistringstream defaults(L"44100,2,22050,0,100,96000,2,1,1,0,1,0,0,0,0,0,44100,2,1,0,0,0");
        while (std::getline(defaults,part,L',')) values.push_back(part);
    }
    // A configuration token is tied to its source sample type. Retain the
    // chosen compression parameters when the next track has a different type.
    if (channels==1 && values[1]==L"2" && values[18]==L"1" && values[3]==L"0") {
        // Keep the per-channel target when adapting a stereo mp3PRO preset.
        // 96 kbps mono makes this legacy core silently choose ordinary MP3.
        const long bitrate=wcstol(values[5].c_str(),nullptr,10);
        if (bitrate>0) values[5]=std::to_wstring(bitrate/2);
    }
    values[0]=std::to_wstring(rate); values[1]=std::to_wstring(channels);
    values[16]=std::to_wstring(rate);
    if (channels==1) values[17]=L"1";
    std::wstring result;
    for (const auto& v:values) { if (!result.empty()) result+=L','; result+=v; }
    return result;
}
}
bool pro_available() {
    const auto path=module_directory()+L"\\fhtpro.flt";
    DWORD a=GetFileAttributesW(path.c_str());
    return a!=INVALID_FILE_ATTRIBUTES && !(a&FILE_ATTRIBUTE_DIRECTORY);
}
HRESULT configure_pro(HWND parent) {
    return protect([&]() -> HRESULT {
        auto& f=filter(); std::lock_guard<std::mutex> lock(f.mutex);
        if (f.busy) return HRESULT_FROM_WIN32(ERROR_BUSY);
        load(f); require(f.configure!=nullptr,E_NOTIMPL);
        auto options=load_options();
        auto value=configuration(f,options.pro_options,44100,2);
        auto key=L"Key"+std::to_wstring(options.pro_options);
        require(!!WritePrivateProfileStringW(L"Configurations",key.c_str(),value.c_str(),f.ini.c_str()));
        DWORD selected=f.configure(parent,f.dll,44100,2,16,options.pro_options);
        if (!selected) return S_FALSE;
        options.pro_options=selected; save_options(options); return S_OK;
    });
}
ProSession::~ProSession() { protect([&]() -> HRESULT { finish(); return S_OK; }); }
void ProSession::start(const std::wstring& path,const WAVEFORMATEX& format,DWORD options) {
    auto& f=filter(); std::lock_guard<std::mutex> lock(f.mutex);
    require(!acquired_ && !f.busy,HRESULT_FROM_WIN32(ERROR_BUSY));
    load(f);
    auto value=configuration(f,options,format.nSamplesPerSec,format.nChannels);
    auto key=L"Key"+std::to_wstring(session_options);
    require(!!WritePrivateProfileStringW(L"Configurations",key.c_str(),value.c_str(),f.ini.c_str()));
    f.output=path; f.file=INVALID_HANDLE_VALUE; f.io_error=0; f.busy=true; acquired_=true;
    chunk_=0x1200;
    output_=f.open(output_token,format.nSamplesPerSec,16,format.nChannels,0,&chunk_,session_options);
    require(output_ && output_!=HGLOBAL(1), f.io_error?HRESULT_FROM_WIN32(f.io_error):E_FAIL);
    require(chunk_>=DWORD(format.nChannels)*2 && chunk_<=1024*1024 && chunk_%(format.nChannels*2)==0);
}
void ProSession::write(const short* pcm,DWORD bytes) {
    auto& f=filter(); require(output_!=nullptr,E_UNEXPECTED);
    auto* data=reinterpret_cast<const BYTE*>(pcm);
    while (bytes) {
        DWORD n=std::min(bytes,chunk_);
        DWORD consumed=f.write(output_,data,n);
        require(!f.io_error,HRESULT_FROM_WIN32(f.io_error));
        require(consumed==n,STG_E_WRITEFAULT);
        data+=n; bytes-=n;
    }
}
void ProSession::finish() {
    if (!acquired_) return;
    auto& f=filter(); std::lock_guard<std::mutex> lock(f.mutex);
    if (output_ && output_!=HGLOBAL(1)) f.close(output_);
    output_=nullptr; acquired_=false;
    DWORD error=f.io_error;
    f.file=INVALID_HANDLE_VALUE; f.output.clear(); f.busy=false;
    require(!error,HRESULT_FROM_WIN32(error));
}
}
