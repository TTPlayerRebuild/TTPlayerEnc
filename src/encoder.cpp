#include "encoder.h"
#include <lame.h>
#include <shlwapi.h>
#include <cmath>
#include <ks.h>
#include <ksmedia.h>

namespace ttp::enc {
namespace {
// 602537B7 resolves the host's standard tag writer (ttpctrl.dll, then EXE).
using CreateContent=HRESULT (WINAPI*)(IStream*,DWORD,IUnknown**,ULONGLONG*);
HMODULE host_module() {
    HMODULE host=GetModuleHandleW(L"ttpctrl.dll");
    if(!host) host=GetModuleHandleW(nullptr);
    return host;
}
CreateContent host_content() {
    return reinterpret_cast<CreateContent>(GetProcAddress(host_module(),"CreateStdContent"));
}
void open_host_file(const wchar_t* path,DWORD mode,IStream** result) {
    using CreateStream=HRESULT (WINAPI*)(LPCWSTR,DWORD,IStream**);
    auto create=reinterpret_cast<CreateStream>(GetProcAddress(host_module(),"CreateStreamOnFile"));
    if(create) check(create(path,mode,result));
    else check(SHCreateStreamOnFileEx(path,mode,0,!!(mode&STGM_CREATE),nullptr,result));
}
bool write_host_tags(IStream* stream,const Tags& tags) {
    auto create=host_content();
    if(!create) return false;
    ComPtr<IUnknown> object;
    seek(stream,0);
    HRESULT hr=create(stream,0,object.put(),nullptr);
    // Standalone hosts may expose a placeholder. Real write failures must not
    // silently change the host-selected tag format or leave a partial fallback.
    if(hr==E_NOTIMPL || hr==E_NOINTERFACE) return false;
    check(hr); require(bool(object),E_UNEXPECTED);
    ComPtr<Metadata> content;
    check(object->QueryInterface(iid_metadata,reinterpret_cast<void**>(content.put())));
    for(const auto& tag:tags) {
        const char* name=tag.first.c_str();
        if(!_stricmp(name,"Year")) name="Date";
        else if(!_stricmp(name,"Track")) name="Tracknumber";
        else if(!_stricmp(name,"Disc")) name="Discnumber";
        check(content->Set(name,tag.second.c_str()));
    }
    content.reset(); object.reset();
    commit_stream(stream);
    return true;
}
void prepend_stream_tags(IStream* stream,const Bytes& tags) {
    if(tags.empty()) return;
    const auto size=stream_size(stream);
    require(size<=INT64_MAX-tags.size(),STG_E_MEDIUMFULL);
    ULARGE_INTEGER expanded{}; expanded.QuadPart=size+tags.size(); check(stream->SetSize(expanded));
    Bytes block(65536);
    for(auto end=size;end;) {
        DWORD count=DWORD(std::min<std::uint64_t>(end,block.size())); end-=count;
        seek(stream,end); read_exact(stream,block.data(),count);
        seek(stream,end+tags.size()); write_exact(stream,block.data(),count);
    }
    seek(stream,0); write_exact(stream,tags.data(),DWORD(tags.size()));
    seek(stream,expanded.QuadPart); commit_stream(stream);
}
void utf16(Bytes& out,const std::wstring& text) {
    out.push_back(0xff); out.push_back(0xfe);
    for (wchar_t c:text) { out.push_back(BYTE(c)); out.push_back(BYTE(c>>8)); }
}
void frame(Bytes& tag,const char* id,const Bytes& data) {
    tag.insert(tag.end(),id,id+4); put32(tag,DWORD(data.size())); tag.push_back(0); tag.push_back(0);
    tag.insert(tag.end(),data.begin(),data.end());
}
void prepend_tags(const std::wstring& path,const Bytes& tags) {
    if (tags.empty()) return;
    // Stream the audio into a sibling file, then replace atomically; memory is bounded.
    auto slash=path.find_last_of(L"\\/");
    const auto dir=slash==std::wstring::npos?L".":path.substr(0,slash);
    wchar_t temporary[MAX_PATH]{};
    wincheck(GetTempFileNameW(dir.c_str(),L"enc",0,temporary));
    try {
        ComPtr<IStream> source,destination;
        check(SHCreateStreamOnFileEx(path.c_str(),STGM_READ|STGM_SHARE_DENY_WRITE,0,FALSE,nullptr,source.put()));
        check(SHCreateStreamOnFileEx(temporary,STGM_CREATE|STGM_READWRITE|STGM_SHARE_EXCLUSIVE,0,TRUE,nullptr,destination.put()));
        write_exact(destination.p,tags.data(),DWORD(tags.size()));
        Bytes buffer(65536); ULONG got{};
        for (;;) {
            check(source->Read(buffer.data(),DWORD(buffer.size()),&got));
            if (!got) break;
            write_exact(destination.p,buffer.data(),got);
        }
        check(destination->Commit(STGC_DEFAULT)); source.reset(); destination.reset();
        wincheck(ReplaceFileW(path.c_str(),temporary,nullptr,0,nullptr,nullptr));
    } catch (...) { DeleteFileW(temporary); throw; }
}
WAVEFORMATEX validate(const WAVEFORMATEX* input) {
    require(input!=nullptr,E_POINTER); auto f=*input;
    if (f.wFormatTag==WAVE_FORMAT_EXTENSIBLE) {
        require(f.cbSize>=22,E_INVALIDARG);
        const auto* e=reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(input);
        if (same(e->SubFormat,KSDATAFORMAT_SUBTYPE_PCM)) f.wFormatTag=WAVE_FORMAT_PCM;
        else if (same(e->SubFormat,KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) f.wFormatTag=WAVE_FORMAT_IEEE_FLOAT;
        else throw Failure{E_NOTIMPL};
        require(e->Samples.wValidBitsPerSample==0 || e->Samples.wValidBitsPerSample==f.wBitsPerSample,E_NOTIMPL);
    }
    require(f.nChannels>=1 && f.nChannels<=2 && f.nSamplesPerSec>=8000 && f.nSamplesPerSec<=192000,E_NOTIMPL);
    require((f.wFormatTag==WAVE_FORMAT_PCM && (f.wBitsPerSample==8 || f.wBitsPerSample==16 || f.wBitsPerSample==24 || f.wBitsPerSample==32)) ||
            (f.wFormatTag==WAVE_FORMAT_IEEE_FLOAT && (f.wBitsPerSample==32 || f.wBitsPerSample==64)),E_NOTIMPL);
    require(f.nBlockAlign==f.nChannels*(f.wBitsPerSample/8) && f.nAvgBytesPerSec==f.nSamplesPerSec*f.nBlockAlign,E_INVALIDARG);
    f.cbSize=0; return f;
}
short sample16(const BYTE* p, const WAVEFORMATEX& f) {
    if (f.wFormatTag==WAVE_FORMAT_IEEE_FLOAT) {
        double x{};
        if (f.wBitsPerSample==64) std::memcpy(&x,p,8);
        else { float v{}; std::memcpy(&v,p,4); x=v; }
        if (!std::isfinite(x)) x=0;
        x=std::clamp(x,-1.0,32767.0/32768.0);
        return short(std::lround(x*32768.0));
    }
    if (f.wBitsPerSample==8) return short((int(*p)-128)*256);
    if (f.wBitsPerSample==16) { short v{}; std::memcpy(&v,p,2); return v; }
    if (f.wBitsPerSample==24) return short(WORD(p[1])|(WORD(p[2])<<8));
    return short(WORD(p[2])|(WORD(p[3])<<8));
}
class Instance final : public StreamEncoder, public Metadata {
    LONG refs_{1};
    Kind kind_;
    enum class State { Fresh, Open, Active, Finished, Failed } state_{State::Fresh};
    WAVEFORMATEX format_{};
    std::wstring path_;
    ComPtr<IStream> stream_;
    Tags tags_;
    lame_t lame_{};
    std::unique_ptr<WmaSession> wma_;
    std::unique_ptr<ProSession> pro_;
    std::uint64_t mp3_start_{};
    Options options_;
  public:
    explicit Instance(Kind kind) : kind_(kind) {}
    ~Instance() {
        if (state_==State::Active) Finish();
        if (lame_) lame_close(lame_);
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
        if (!out) return E_POINTER;
        *out=nullptr;
        if (same(iid,IID_IUnknown) || same(iid,iid_encoder) || (kind_==Kind::Mp3 && same(iid,iid_stream_encoder)))
            *out=static_cast<StreamEncoder*>(this);
        else if (same(iid,iid_metadata)) *out=static_cast<Metadata*>(this);
        else return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs_); }
    ULONG STDMETHODCALLTYPE Release() override { auto n=InterlockedDecrement(&refs_); if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE Open(const wchar_t* path,WAVEFORMATEX* input) override {
        return protect([&]() -> HRESULT {
            require(state_==State::Fresh,E_UNEXPECTED); require(path && *path,E_INVALIDARG);
            auto f=validate(input); options_=load_options();
            std::vector<wchar_t> absolute(32768);
            DWORD length=GetFullPathNameW(path,DWORD(absolute.size()),absolute.data(),nullptr);
            require(length && length<absolute.size(),HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE));
            std::wstring target_path(absolute.data(),length);
            ComPtr<IStream> target;
            if (kind_==Kind::Mp3)
                open_host_file(target_path.c_str(),STGM_CREATE|STGM_READWRITE|STGM_SHARE_DENY_WRITE,target.put());
            path_=std::move(target_path); format_=f; stream_=std::move(target); state_=State::Open; return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE OpenStream(IStream* stream,WAVEFORMATEX* input) override {
        return protect([&]() -> HRESULT {
            require(kind_==Kind::Mp3,E_NOTIMPL); require(state_==State::Fresh,E_UNEXPECTED); require(stream!=nullptr,E_POINTER);
            auto f=validate(input); options_=load_options();
            stream_=ComPtr<IStream>(stream,true); format_=f; state_=State::Open; return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE Start() override {
        // 60251765: starting an already initialized MP3 encoder is a no-op.
        if (kind_==Kind::Mp3 && state_==State::Active) return S_OK;
        HRESULT hr=protect([&]() -> HRESULT {
            require(state_==State::Open,E_UNEXPECTED);
            if (kind_==Kind::Mp3) {
                lame_=lame_init(); require(lame_!=nullptr,E_OUTOFMEMORY);
                require(lame_set_in_samplerate(lame_,int(format_.nSamplesPerSec))>=0);
                require(lame_set_out_samplerate(lame_,int(format_.nSamplesPerSec))>=0);
                require(lame_set_num_channels(lame_,format_.nChannels)>=0);
                lame_set_mode(lame_,format_.nChannels==1?MONO:options_.joint?JOINT_STEREO:STEREO);
                lame_set_quality(lame_,2);
                lame_set_copyright(lame_,1);
                lame_set_original(lame_,1);
                lame_set_error_protection(lame_,1);
                lame_set_extension(lame_,1);
                if (options_.mp3_mode==0) { lame_set_VBR(lame_,vbr_off); lame_set_brate(lame_,options_.mp3_value); }
                else if (options_.mp3_mode==1) { lame_set_VBR(lame_,vbr_abr); require(lame_set_preset(lame_,options_.mp3_value)>=0); }
                else if (options_.mp3_value>=1100) { lame_set_VBR(lame_,vbr_default); lame_set_VBR_q(lame_,options_.mp3_value-1100); }
                else {
                    const int presets[]{R3MIX,STANDARD,EXTREME,INSANE,STANDARD_FAST,EXTREME_FAST,MEDIUM,MEDIUM_FAST};
                    require(lame_set_preset(lame_,presets[options_.mp3_value-1000])>=0);
                }
                lame_set_write_id3tag_automatic(lame_,0);
                lame_set_bWriteVbrTag(lame_,options_.mp3_mode!=0);
                require(lame_init_params(lame_)>=0,E_INVALIDARG);
                seek(stream_.p,0); ULARGE_INTEGER zero{}; check(stream_->SetSize(zero));
                mp3_start_=tell(stream_.p);
            } else if (kind_==Kind::Wma) {
                wma_=std::make_unique<WmaSession>(); wma_->start(path_,format_,options_,tags_);
            } else {
                pro_=std::make_unique<ProSession>(); pro_->start(path_,format_,options_.pro_options);
            }
            state_=State::Active; return S_OK;
        });
        if (FAILED(hr) && state_==State::Open) { state_=State::Failed; pro_.reset(); wma_.reset(); }
        return hr;
    }
    HRESULT STDMETHODCALLTYPE Finish() override {
        if (state_==State::Finished) return S_FALSE;
        if (state_!=State::Active && state_!=State::Failed) return E_UNEXPECTED;
        bool failed=state_==State::Failed;
        state_=State::Finished;
        HRESULT hr=protect([&]() -> HRESULT {
            if (kind_==Kind::Mp3 && lame_) {
                Bytes out(16384);
                int n=lame_encode_flush(lame_,out.data(),int(out.size())); require(n>=0,E_FAIL);
                if (!failed && n) write_exact(stream_.p,out.data(),DWORD(n));
                if (!failed) {
                    size_t size=lame_get_lametag_frame(lame_,nullptr,0);
                    if (size) {
                        require(size<=65536); out.resize(size);
                        require(lame_get_lametag_frame(lame_,out.data(),out.size())==size);
                        auto end=tell(stream_.p); seek(stream_.p,mp3_start_); write_exact(stream_.p,out.data(),DWORD(size)); seek(stream_.p,end);
                    }
                    commit_stream(stream_.p);
                    if(!tags_.empty() && !write_host_tags(stream_.p,tags_))
                        prepend_stream_tags(stream_.p,id3_tags(tags_));
                }
            } else if (wma_) wma_->finish();
            else if (pro_) {
                pro_->finish();
                if(!failed && !tags_.empty()) {
                    bool handled=false;
                    if(host_content()) {
                        ComPtr<IStream> file;
                        // 6025252F reopens via the host too: the original tag
                        // writer depends on its file stream's Stat semantics.
                        open_host_file(path_.c_str(),STGM_READWRITE|STGM_SHARE_DENY_NONE,file.put());
                        handled=write_host_tags(file.p,tags_);
                    }
                    if(!handled) prepend_tags(path_,id3_tags(tags_));
                }
            }
            return failed?E_FAIL:S_OK;
        });
        pro_.reset(); wma_.reset();
        if (lame_) { lame_close(lame_); lame_=nullptr; }
        return hr;
    }
    HRESULT STDMETHODCALLTYPE Write(Buffer* buffer,DWORD* written) override {
        if (written) *written=0;
        HRESULT hr=protect([&]() -> HRESULT {
            require(state_==State::Active,E_UNEXPECTED); require(buffer!=nullptr,E_POINTER);
            BYTE* source{}; DWORD bytes{}; check(buffer->Data(&source,&bytes));
            require(!bytes || source,E_POINTER); require(bytes%format_.nBlockAlign==0,E_INVALIDARG);
            DWORD sample_bytes=format_.wBitsPerSample/8;
            std::vector<short> pcm; Bytes encoded(20000);
            while (bytes) {
                DWORD frames=std::min<DWORD>(bytes/format_.nBlockAlign,4096);
                DWORD count=frames*format_.nChannels;
                pcm.resize(count);
                for (DWORD i=0;i<count;++i) pcm[i]=sample16(source+i*sample_bytes,format_);
                if (kind_==Kind::Mp3) {
                    int n=format_.nChannels==2?lame_encode_buffer_interleaved(lame_,pcm.data(),int(frames),encoded.data(),int(encoded.size())):
                        lame_encode_buffer(lame_,pcm.data(),pcm.data(),int(frames),encoded.data(),int(encoded.size()));
                    require(n>=0,E_FAIL);
                    if (n) write_exact(stream_.p,encoded.data(),DWORD(n));
                    if (written) *written+=DWORD(n);
                } else if (wma_) wma_->write(pcm.data(),frames);
                else pro_->write(pcm.data(),count*2);
                DWORD consumed=frames*format_.nBlockAlign;
                source+=consumed; bytes-=consumed;
            }
            return S_OK;
        });
        if (FAILED(hr) && state_==State::Active) state_=State::Failed;
        return hr;
    }
    HRESULT STDMETHODCALLTYPE Extension(wchar_t** out) override { return text(kind_==Kind::Wma?L"wma":L"mp3",out); }
    HRESULT STDMETHODCALLTYPE Count(DWORD* out) override { if (!out) return E_POINTER; *out=DWORD(tags_.size()); return S_OK; }
    HRESULT STDMETHODCALLTYPE At(DWORD index,wchar_t** key,wchar_t** value) override {
        if (key) *key=nullptr; if (value) *value=nullptr;
        return protect([&]() -> HRESULT {
            require(key && value,E_POINTER); require(index<tags_.size(),E_INVALIDARG);
            check(text(wide(tags_[index].first),key));
            HRESULT hr=text(tags_[index].second,value);
            if (FAILED(hr)) { CoTaskMemFree(*key); *key=nullptr; }
            return hr;
        });
    }
    HRESULT STDMETHODCALLTYPE Get(const char* key,wchar_t** value) override {
        if (value) *value=nullptr;
        if (!key || !value) return E_POINTER;
        for (const auto& tag:tags_) if (!_stricmp(key,tag.first.c_str())) return text(tag.second,value);
        return E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE Set(const char* key,const wchar_t* value) override {
        return protect([&]() -> HRESULT {
            require(key && *key,E_POINTER);
            require(state_==State::Fresh || state_==State::Open,E_UNEXPECTED);
            require(std::strlen(key)<=1024 && (!value || std::wcslen(value)<=1024*1024),E_INVALIDARG);
            auto found=std::find_if(tags_.begin(),tags_.end(),[&](const auto& tag){return !_stricmp(key,tag.first.c_str());});
            if (!value || !*value) { if (found!=tags_.end()) tags_.erase(found); }
            else if (found!=tags_.end()) found->second=value;
            else { require(tags_.size()<4096,E_INVALIDARG); tags_.emplace_back(key,value); }
            return S_OK;
        });
    }
};
}
Bytes id3_tags(const Tags& tags) {
    Bytes frames;
    for (const auto& tag:tags) {
        const char* id=nullptr;
        for (const auto& pair : {std::pair{"Title","TIT2"},{"Artist","TPE1"},{"Album","TALB"},{"AlbumArtist","TPE2"},
                                 {"Year","TYER"},{"Date","TYER"},{"Genre","TCON"},{"Track","TRCK"},{"TrackNumber","TRCK"},
                                 {"Disc","TPOS"},{"DiscNumber","TPOS"},{"Composer","TCOM"},{"Copyright","TCOP"},
                                 {"ISRC","TSRC"},{"Subtitle","TIT3"}})
            if (!_stricmp(tag.first.c_str(),pair.first)) { id=pair.second; break; }
        Bytes payload{1};
        if (id) utf16(payload,tag.second);
        else if (!_stricmp(tag.first.c_str(),"Comment")) {
            id="COMM"; payload.insert(payload.end(),{'e','n','g',0xff,0xfe,0,0}); utf16(payload,tag.second);
        } else if (!_stricmp(tag.first.c_str(),"Lyrics")) {
            id="USLT"; payload.insert(payload.end(),{'e','n','g',0xff,0xfe,0,0}); utf16(payload,tag.second);
        } else if (!_stricmp(tag.first.c_str(),"Copyright_URL")) {
            id="WCOP"; const auto url=utf8(tag.second); payload.assign(url.begin(),url.end());
        } else {
            id="TXXX"; utf16(payload,wide(tag.first)); payload.push_back(0); payload.push_back(0); utf16(payload,tag.second);
        }
        frame(frames,id,payload); require(frames.size()<=16*1024*1024,E_INVALIDARG);
    }
    if (frames.empty()) return {};
    DWORD size=DWORD(frames.size());
    Bytes result{'I','D','3',3,0,0,BYTE((size>>21)&127),BYTE((size>>14)&127),BYTE((size>>7)&127),BYTE(size&127)};
    result.insert(result.end(),frames.begin(),frames.end()); return result;
}
HRESULT make_encoder(Kind kind,void** out) {
    if (!out) return E_POINTER; *out=nullptr;
    return protect([&]() -> HRESULT { *out=static_cast<StreamEncoder*>(new Instance(kind)); return S_OK; });
}
}
