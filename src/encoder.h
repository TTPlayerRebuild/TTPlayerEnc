#pragma once
#include "common.h"
#include <mutex>

namespace ttp::enc {
enum class Kind { Mp3, Wma, Pro };
using Tags = std::vector<std::pair<std::string, std::wstring>>;
struct Options {
    int mp3_mode{0}; // original Quality/Mode: CBR, ABR, preset
    int mp3_value{192};
    bool joint{true};
    int wma_mode{2}; // original WMA/VBR: CBR, two-pass VBR, quality VBR; extension 3: lossless
    int wma_value{75};
    DWORD pro_options{7};
};
Options load_options();
void save_options(const Options&);
bool chinese_ui();
std::wstring module_directory();
std::wstring settings_file(const wchar_t* name);
HRESULT configure(Kind, HWND);
HRESULT make_encoder(Kind, void**);
Bytes id3_tags(const Tags&);

class WmaSession {
    struct Impl;
    std::unique_ptr<Impl> impl_;
  public:
    WmaSession();
    ~WmaSession();
    void start(const std::wstring&, const WAVEFORMATEX&, const Options&, const Tags&);
    void write(const short*, DWORD frames);
    void finish();
};
class ProSession {
    HGLOBAL output_{};
    DWORD chunk_{};
    bool acquired_{};
  public:
    ProSession() = default;
    ~ProSession();
    void start(const std::wstring&, const WAVEFORMATEX&, DWORD options);
    void write(const short*, DWORD bytes);
    void finish();
};
bool pro_available();
HRESULT configure_pro(HWND);
}
