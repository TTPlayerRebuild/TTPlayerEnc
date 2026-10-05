#include "encoder.h"
#include <lame.h>

namespace ttp::enc {
HMODULE module{};
class Factory final : public EncoderCreator {
    LONG refs_{1};
    Kind kind_;
  public:
    explicit Factory(Kind kind) : kind_(kind) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (!same(iid, IID_IUnknown) && !same(iid, cat_encoder)) return E_NOINTERFACE;
        *out = static_cast<EncoderCreator*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs_); }
    ULONG STDMETHODCALLTYPE Release() override {
        auto n = InterlockedDecrement(&refs_); if (!n) delete this; return n;
    }
    HRESULT STDMETHODCALLTYPE Create(void** out) override { return make_encoder(kind_, out); }
    HRESULT STDMETHODCALLTYPE Name(wchar_t** out) override {
        return protect([&] {
            if (kind_ == Kind::Mp3) return text(L"MP3 编码器 (LAME " + wide(get_lame_version()) + L")", out);
            return text(kind_ == Kind::Wma ? L"WMA 编码器 (Windows Media Audio)" :
                        L"mp3PRO 编码器 (Cool Edit Pro 2)", out);
        });
    }
    HRESULT STDMETHODCALLTYPE Extension(wchar_t** out) override {
        return text(kind_ == Kind::Wma ? L"wma" : L"mp3", out);
    }
    HRESULT STDMETHODCALLTYPE Configurable() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Configure(HWND parent) override { return configure(kind_, parent); }
};
class SoundAddIn final : public AddIn {
    LONG refs_{1};
  public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (!same(iid, IID_IUnknown) && !same(iid, iid_addin)) return E_NOINTERFACE;
        *out = static_cast<AddIn*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs_); }
    ULONG STDMETHODCALLTYPE Release() override {
        auto n = InterlockedDecrement(&refs_); if (!n) delete this; return n;
    }
    HRESULT STDMETHODCALLTYPE Enum(DWORD index, GUID* category, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (!category) return E_POINTER;
        return protect([&]() -> HRESULT {
            if (index > 2 || (index == 2 && !pro_available())) return E_INVALIDARG;
            *out = static_cast<EncoderCreator*>(new Factory(static_cast<Kind>(index)));
            *category = cat_encoder; return S_OK;
        });
    }
};
}
extern "C" HRESULT WINAPI ttpGetSoundAddIn(void** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    return ttp::enc::protect([&]() -> HRESULT {
        *out = static_cast<ttp::enc::AddIn*>(new ttp::enc::SoundAddIn); return S_OK;
    });
}
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        ttp::enc::module = instance; DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
