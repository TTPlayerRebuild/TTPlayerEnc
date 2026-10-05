#include "encoder.h"
#include <shlobj.h>
#include <shlwapi.h>

namespace ttp::enc {
namespace {
std::mutex options_mutex;
std::mutex directory_mutex;
std::wstring settings_directory;
std::wstring writable_directory() {
    auto dir = module_directory();
    wchar_t probe[MAX_PATH]{};
    if (GetTempFileNameW(dir.c_str(), L"enc", 0, probe)) {
        DeleteFileW(probe); return dir;
    }
    wchar_t appdata[MAX_PATH]{};
    check(SHGetFolderPathW(nullptr, CSIDL_APPDATA | CSIDL_FLAG_CREATE, nullptr, SHGFP_TYPE_CURRENT, appdata));
    dir = std::wstring(appdata) + L"\\TTPlayer\\ttp_enc";
    int result = SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    require(result == ERROR_SUCCESS || result == ERROR_ALREADY_EXISTS || result == ERROR_FILE_EXISTS,
            HRESULT_FROM_WIN32(result));
    return dir;
}
int number(const wchar_t* section, const wchar_t* key, int def, const std::wstring& path) {
    return static_cast<int>(GetPrivateProfileIntW(section, key, def, path.c_str()));
}
void put(const wchar_t* section, const wchar_t* key, int value, const std::wstring& path) {
    const auto s = std::to_wstring(value);
    wincheck(WritePrivateProfileStringW(section, key, s.c_str(), path.c_str()));
}
struct DialogState { Kind kind; Options options; bool mp3_quality{}; };
// Keep the original table order and each mode's independent selection.
constexpr int mp3_cbr[]{320,256,224,192,160,128,112,96,80,64,40,32};
constexpr int mp3_abr[]{320,256,224,192,160,144,128,112,96,80,64,56,48,40,32};
constexpr int wma_cbr[]{320,256,224,192,160,128,96,80,64,48,32,24,16};
constexpr int wma_vbr[]{192,160,128,96,80,64,48};
constexpr int wma_quality[]{98,90,75,50,25,10};
void add_choice(HWND combo,int value,const std::wstring& label,int selected) {
    auto index=SendMessageW(combo,CB_ADDSTRING,0,LPARAM(label.c_str()));
    require(index>=0,E_OUTOFMEMORY);
    SendMessageW(combo,CB_SETITEMDATA,index,value);
    if(value==selected) SendMessageW(combo,CB_SETCURSEL,index,0);
}
template<size_t N> void numbers(HWND window,int id,const int (&values)[N],int selected,int fallback) {
    auto combo=GetDlgItem(window,id);
    for(int value:values) add_choice(combo,value,std::to_wstring(value),selected);
    if(SendMessageW(combo,CB_GETCURSEL,0,0)==CB_ERR) SendMessageW(combo,CB_SETCURSEL,fallback,0);
}
int choice(HWND window,int id) {
    auto combo=GetDlgItem(window,id);
    auto selected=SendMessageW(combo,CB_GETCURSEL,0,0);
    require(selected!=CB_ERR,E_INVALIDARG);
    return int(SendMessageW(combo,CB_GETITEMDATA,selected,0));
}
void update_mode(HWND window,const DialogState& state) {
    const int mode=state.kind==Kind::Mp3?
        (state.options.mp3_mode==2 && state.mp3_quality?3:state.options.mp3_mode):state.options.wma_mode;
    for(int i=0;i<4;++i) {
        CheckDlgButton(window,1001+i,i==mode?BST_CHECKED:BST_UNCHECKED);
        if(i<3 || state.kind==Kind::Mp3) EnableWindow(GetDlgItem(window,1005+i),i==mode);
    }
}
Options selected_options(HWND window,const DialogState& state) {
    auto o=state.options;
    if(state.kind==Kind::Mp3) {
        o.mp3_value=choice(window,o.mp3_mode==2 && state.mp3_quality?1008:1005+o.mp3_mode);
        o.joint=IsDlgButtonChecked(window,1010)==BST_CHECKED;
    } else o.wma_value=o.wma_mode==3?0:choice(window,1005+o.wma_mode);
    return o;
}
INT_PTR show_dialog(int id,HWND parent,DLGPROC proc,LPARAM state) {
    WORD language=chinese_ui()?MAKELANGID(LANG_CHINESE,SUBLANG_CHINESE_SIMPLIFIED):MAKELANGID(LANG_ENGLISH,SUBLANG_ENGLISH_US);
    auto resource=FindResourceExW(module,RT_DIALOG,MAKEINTRESOURCEW(id),language);
    require(resource!=nullptr,HRESULT_FROM_WIN32(ERROR_RESOURCE_NOT_FOUND));
    auto loaded=LoadResource(module,resource); require(loaded!=nullptr);
    auto result=DialogBoxIndirectParamW(module,static_cast<const DLGTEMPLATE*>(LockResource(loaded)),parent,proc,state);
    if(result==-1) throw Failure{HRESULT_FROM_WIN32(GetLastError())};
    return result;
}
void center(HWND window) {
    RECT bounds{},rect{};
    HWND parent=GetParent(window);
    if(parent) GetWindowRect(parent,&bounds);
    else SystemParametersInfoW(SPI_GETWORKAREA,0,&bounds,0);
    GetWindowRect(window,&rect);
    SetWindowPos(window,nullptr,bounds.left+(bounds.right-bounds.left-(rect.right-rect.left))/2,
        bounds.top+(bounds.bottom-bounds.top-(rect.bottom-rect.top))/2,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
}
INT_PTR CALLBACK options_proc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    auto* state=reinterpret_cast<DialogState*>(GetWindowLongPtrW(window,DWLP_USER));
    if(message==WM_INITDIALOG) {
        state=reinterpret_cast<DialogState*>(lp); SetWindowLongPtrW(window,DWLP_USER,lp);
        HRESULT hr=protect([&]() -> HRESULT {
            if(state->kind==Kind::Mp3) {
                numbers(window,1005,mp3_cbr,state->options.mp3_value,6);
                numbers(window,1006,mp3_abr,state->options.mp3_value,7);
                const wchar_t* names[]{L"R3mix",L"Standard",L"Extreme",L"Insane",L"Fast Standard",L"Fast Extreme",L"Medium",L"FastMedium"};
                for(int i=0;i<8;++i) add_choice(GetDlgItem(window,1007),1000+i,names[i],state->options.mp3_value);
                for(int i=0;i<=9;++i) add_choice(GetDlgItem(window,1008),1100+i,L"V"+std::to_wstring(i),state->options.mp3_value);
                if(SendDlgItemMessageW(window,1007,CB_GETCURSEL,0,0)==CB_ERR) SendDlgItemMessageW(window,1007,CB_SETCURSEL,4,0);
                if(SendDlgItemMessageW(window,1008,CB_GETCURSEL,0,0)==CB_ERR) SendDlgItemMessageW(window,1008,CB_SETCURSEL,4,0);
                state->mp3_quality=state->options.mp3_mode==2 && state->options.mp3_value>=1100;
                CheckDlgButton(window,1010,state->options.joint?BST_CHECKED:BST_UNCHECKED);
            } else {
                numbers(window,1005,wma_cbr,state->options.wma_value,6);
                numbers(window,1006,wma_vbr,state->options.wma_value,4);
                numbers(window,1007,wma_quality,state->options.wma_value,2);
            }
            update_mode(window,*state); center(window); return S_OK;
        });
        if(FAILED(hr)) EndDialog(window,IDCANCEL);
        return TRUE;
    }
    if(!state || message!=WM_COMMAND) return FALSE;
    int id=LOWORD(wp);
    if(id>=1001 && id<=1004 && HIWORD(wp)==BN_CLICKED) {
        if(state->kind==Kind::Mp3) {
            // Both VBR groups use the existing mode/preset encoding on disk.
            state->options.mp3_mode=id==1004?2:id-1001;
            if(id>=1003) state->mp3_quality=id==1004;
        }
        else state->options.wma_mode=id-1001;
        update_mode(window,*state); return TRUE;
    }
    if(id==IDCANCEL) { EndDialog(window,IDCANCEL); return TRUE; }
    if(id==IDOK) {
        auto hr=protect([&]() -> HRESULT {
            auto saved=load_options(); auto selected=selected_options(window,*state);
            if(state->kind==Kind::Mp3) { saved.mp3_mode=selected.mp3_mode; saved.mp3_value=selected.mp3_value; saved.joint=selected.joint; }
            else { saved.wma_mode=selected.wma_mode; saved.wma_value=selected.wma_value; }
            save_options(saved); return S_OK;
        });
        if(FAILED(hr)) MessageBoxW(window,chinese_ui()?L"无法保存编码配置，请检查配置目录是否可写。":L"Cannot save encoder settings. Check that the settings folder is writable.",chinese_ui()?L"编码选项":L"Encoder options",MB_ICONERROR);
        else EndDialog(window,IDOK);
        return TRUE;
    }
    return FALSE;
}
}
bool chinese_ui() { return PRIMARYLANGID(GetUserDefaultUILanguage())==LANG_CHINESE; }
std::wstring module_directory() {
    std::vector<wchar_t> path(32768);
    DWORD n = GetModuleFileNameW(module, path.data(), DWORD(path.size()));
    require(n && n < path.size(), E_FAIL);
    std::wstring result(path.data(), n);
    auto slash = result.find_last_of(L"\\/"); require(slash != std::wstring::npos);
    return result.substr(0, slash);
}
std::wstring settings_file(const wchar_t* name) {
    // XP does not reliably initialize MSVC's guarded local statics in a DLL
    // loaded after process startup. Keep lazy initialization explicitly locked.
    std::lock_guard<std::mutex> guard(directory_mutex);
    if (settings_directory.empty()) settings_directory = writable_directory();
    return settings_directory + L"\\" + name;
}
Options load_options() {
    std::lock_guard<std::mutex> guard(options_mutex);
    Options o; auto path = settings_file(L"ttp_enc.ini");
    o.mp3_mode = number(L"Quality", L"Mode", 0, path);
    if (o.mp3_mode < 0 || o.mp3_mode > 2) o.mp3_mode = 0;
    o.mp3_value = number(L"Quality", L"Preset", o.mp3_mode == 2 ? 1004 : 192, path);
    if (o.mp3_mode == 2) {
        if (!((o.mp3_value >= 1000 && o.mp3_value <= 1007) || (o.mp3_value >= 1100 && o.mp3_value <= 1109))) o.mp3_value = 1004;
    } else if (o.mp3_value < 8 || o.mp3_value > 320) o.mp3_value = 192;
    o.joint = number(L"Stereo", L"Joint", 1, path) != 0;
    o.wma_mode = number(L"WMA", L"VBR", 2, path);
    if (o.wma_mode < 0 || o.wma_mode > 3) o.wma_mode = 2;
    o.wma_value = number(L"WMA", L"Preset", o.wma_mode == 2 ? 75 : 96, path);
    if (o.wma_mode == 2) o.wma_value = std::clamp(o.wma_value, 0, 100);
    else if(o.wma_mode==3) o.wma_value=0;
    else o.wma_value = std::clamp(o.wma_value, 8, 640);
    o.pro_options = DWORD(number(L"mp3PRO", L"Options", 7, path));
    return o;
}
void save_options(const Options& o) {
    std::lock_guard<std::mutex> guard(options_mutex);
    auto path = settings_file(L"ttp_enc.ini");
    put(L"Quality", L"Mode", o.mp3_mode, path); put(L"Quality", L"Preset", o.mp3_value, path);
    put(L"Stereo", L"Joint", o.joint, path); put(L"WMA", L"VBR", o.wma_mode, path);
    put(L"WMA", L"Preset", o.wma_value, path); put(L"mp3PRO", L"Options", int(o.pro_options), path);
}
HRESULT configure(Kind kind, HWND parent) {
    if (kind == Kind::Pro) return configure_pro(parent);
    return protect([&]() -> HRESULT {
        DialogState state{kind, load_options()};
        auto result = show_dialog(kind==Kind::Mp3?1000:102, parent, options_proc, LPARAM(&state));
        if (result == -1) return HRESULT_FROM_WIN32(GetLastError());
        return result == IDOK ? S_OK : S_FALSE;
    });
}
}
