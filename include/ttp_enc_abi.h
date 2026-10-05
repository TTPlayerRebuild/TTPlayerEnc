#pragma once
#include <windows.h>
#include <mmreg.h>
#include <objidl.h>

namespace ttp::enc {
inline constexpr GUID iid_addin{0xeec6c534,0xfeba,0x421e,{0xaa,0x5d,0x10,0xac,0x66,0xf9,0x87,0x84}};
inline constexpr GUID cat_encoder{0x5954a909,0x9e02,0x4650,{0x99,0x5e,0xf7,0x53,0x5e,0xe2,0xbd,0x8b}};
inline constexpr GUID iid_encoder{0x04b9a359,0x7ff7,0x4ee9,{0x9c,0xfb,0xae,0xbb,0x3a,0x33,0x1d,0xc7}};
inline constexpr GUID iid_stream_encoder{0xffb27df8,0x77ef,0x4ba7,{0x89,0x45,0x7e,0x14,0x4c,0x38,0x1c,0xbe}};
inline constexpr GUID iid_metadata{0x7ad84e00,0x5fef,0x4481,{0xb5,0x32,0xfb,0xbd,0x67,0x7e,0x67,0xc2}};
// Exact x86 slot order. All returned strings use CoTaskMemAlloc.
struct Buffer : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE SetLength(DWORD) = 0;
    virtual HRESULT STDMETHODCALLTYPE Capacity(DWORD*) = 0;
    virtual HRESULT STDMETHODCALLTYPE Data(BYTE**, DWORD*) = 0;
};
struct Encoder : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Open(const wchar_t*, WAVEFORMATEX*) = 0;
    virtual HRESULT STDMETHODCALLTYPE Start() = 0;
    virtual HRESULT STDMETHODCALLTYPE Finish() = 0;
    virtual HRESULT STDMETHODCALLTYPE Write(Buffer*, DWORD*) = 0;
    virtual HRESULT STDMETHODCALLTYPE Extension(wchar_t**) = 0;
};
struct StreamEncoder : Encoder {
    virtual HRESULT STDMETHODCALLTYPE OpenStream(IStream*, WAVEFORMATEX*) = 0;
};
struct Metadata : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Count(DWORD*) = 0;
    virtual HRESULT STDMETHODCALLTYPE At(DWORD, wchar_t**, wchar_t**) = 0;
    virtual HRESULT STDMETHODCALLTYPE Get(const char*, wchar_t**) = 0;
    virtual HRESULT STDMETHODCALLTYPE Set(const char*, const wchar_t*) = 0;
};
struct EncoderCreator : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Create(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE Name(wchar_t**) = 0;
    virtual HRESULT STDMETHODCALLTYPE Extension(wchar_t**) = 0;
    virtual HRESULT STDMETHODCALLTYPE Configurable() = 0;
    virtual HRESULT STDMETHODCALLTYPE Configure(HWND) = 0;
};
struct AddIn : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Enum(DWORD, GUID*, void**) = 0;
};
}
