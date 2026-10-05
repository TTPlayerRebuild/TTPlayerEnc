#include "encoder.h"
#include <initguid.h>
#include <wmsdk.h>

namespace ttp::enc {
struct WmaSession::Impl {
    HMODULE library{};
    ComPtr<IWMWriter> writer;
    ComPtr<IWMWriterPreprocess> preprocess;
    HANDLE spool{INVALID_HANDLE_VALUE};
    WAVEFORMATEX format{};
    std::uint64_t frames{};
    bool active{}, com_initialized{};
    ~Impl() {
        if (active && writer) writer->EndWriting();
        if (spool!=INVALID_HANDLE_VALUE) CloseHandle(spool);
        preprocess.reset();
        writer.reset();
        if (library) FreeLibrary(library);
        if (com_initialized) CoUninitialize();
    }
};
WmaSession::WmaSession() : impl_(std::make_unique<Impl>()) {}
WmaSession::~WmaSession() = default;
void WmaSession::start(const std::wstring& path,const WAVEFORMATEX& format,const Options& options,const Tags& tags) {
    auto& s=*impl_; require(!s.writer,E_UNEXPECTED);
    HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    require(SUCCEEDED(com) || com==RPC_E_CHANGED_MODE,com);
    s.com_initialized=SUCCEEDED(com);
    wchar_t system[MAX_PATH]{};
    require(!!GetSystemDirectoryW(system,MAX_PATH));
    auto library_path=std::wstring(system)+L"\\wmvcore.dll";
    s.library=LoadLibraryW(library_path.c_str());
    require(s.library!=nullptr,HRESULT_FROM_WIN32(GetLastError()));
    using Manager=HRESULT (WINAPI*)(IWMProfileManager**);
    using Writer=HRESULT (WINAPI*)(IUnknown*,IWMWriter**);
    auto create_manager=reinterpret_cast<Manager>(GetProcAddress(s.library,"WMCreateProfileManager"));
    auto create_writer=reinterpret_cast<Writer>(GetProcAddress(s.library,"WMCreateWriter"));
    require(create_manager && create_writer,HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND));
    ComPtr<IWMProfileManager> manager; check(create_manager(manager.put()));
    ComPtr<IWMCodecInfo3> codecs;
    check(manager->QueryInterface(__uuidof(IWMCodecInfo3),reinterpret_cast<void**>(codecs.put())));
    DWORD codec_count{}; check(codecs->GetCodecInfoCount(WMMEDIATYPE_Audio,&codec_count));
    ComPtr<IWMStreamConfig> selected;
    for (DWORD codec=0;codec<codec_count && !selected;++codec) {
        BOOL vbr=options.wma_mode!=0;
        HRESULT set=codecs->SetCodecEnumerationSetting(WMMEDIATYPE_Audio,codec,g_wszVBREnabled,
            WMT_TYPE_BOOL,reinterpret_cast<BYTE*>(&vbr),sizeof(vbr));
        if (FAILED(set)) continue;
        DWORD passes=options.wma_mode==1?2:1;
        set=codecs->SetCodecEnumerationSetting(WMMEDIATYPE_Audio,codec,g_wszNumPasses,
            WMT_TYPE_DWORD,reinterpret_cast<BYTE*>(&passes),sizeof(passes));
        if (FAILED(set)) continue;
        DWORD count{};
        if (FAILED(codecs->GetCodecFormatCount(WMMEDIATYPE_Audio,codec,&count))) continue;
        for (DWORD index=0;index<count;++index) {
            ComPtr<IWMStreamConfig> stream;
            if (FAILED(codecs->GetCodecFormat(WMMEDIATYPE_Audio,codec,index,stream.put()))) continue;
            ComPtr<IWMMediaProps> props;
            if (FAILED(stream->QueryInterface(__uuidof(IWMMediaProps),reinterpret_cast<void**>(props.put())))) continue;
            DWORD size{}; if (FAILED(props->GetMediaType(nullptr,&size)) || size<sizeof(WM_MEDIA_TYPE) || size>1024*1024) continue;
            Bytes storage(size); auto* media=reinterpret_cast<WM_MEDIA_TYPE*>(storage.data());
            if (FAILED(props->GetMediaType(media,&size)) || !same(media->formattype,WMFORMAT_WaveFormatEx) || media->cbFormat<sizeof(WAVEFORMATEX)) continue;
            const auto* wave=reinterpret_cast<WAVEFORMATEX*>(media->pbFormat);
            const WORD expected=options.wma_mode==3?WAVE_FORMAT_WMAUDIO_LOSSLESS:WAVE_FORMAT_WMAUDIO2;
            if (wave->wFormatTag!=expected || wave->nChannels!=format.nChannels || wave->wBitsPerSample!=16) continue;
            // 60254722: match the requested format, never silently resample or
            // replace an unsupported bitrate with a much lower one.
            if (wave->nSamplesPerSec!=format.nSamplesPerSec) continue;
            if (options.wma_mode==2) {
                if ((wave->nAvgBytesPerSec&0x7fffff00)!=0x7fffff00 ||
                    int(wave->nAvgBytesPerSec&0xff)!=options.wma_value) continue;
            } else if (options.wma_mode<=1) {
                if ((std::uint64_t(wave->nAvgBytesPerSec)*8+56)/1000!=std::uint64_t(options.wma_value)) continue;
            }
            selected=std::move(stream); break;
        }
    }
    require(bool(selected),E_INVALIDARG);
    check(selected->SetStreamNumber(1));
    wchar_t audio_name[]=L"Audio";
    check(selected->SetStreamName(audio_name)); check(selected->SetConnectionName(audio_name));
    ComPtr<IWMProfile> profile; check(manager->CreateEmptyProfile(WMT_VER_7_0,profile.put()));
    check(profile->AddStream(selected.p));
    check(create_writer(nullptr,s.writer.put())); check(s.writer->SetProfile(profile.p));
    s.format=format; s.format.wFormatTag=WAVE_FORMAT_PCM; s.format.wBitsPerSample=16; s.format.cbSize=0;
    s.format.nBlockAlign=s.format.nChannels*2; s.format.nAvgBytesPerSec=s.format.nSamplesPerSec*s.format.nBlockAlign;
    ComPtr<IWMInputMediaProps> input; check(s.writer->GetInputProps(0,input.put()));
    WM_MEDIA_TYPE media{};
    media.majortype=WMMEDIATYPE_Audio; media.subtype=WMMEDIASUBTYPE_PCM;
    media.bFixedSizeSamples=TRUE; media.bTemporalCompression=FALSE; media.lSampleSize=s.format.nBlockAlign;
    media.formattype=WMFORMAT_WaveFormatEx; media.cbFormat=sizeof(s.format); media.pbFormat=reinterpret_cast<BYTE*>(&s.format);
    check(input->SetMediaType(&media)); check(s.writer->SetInputProps(0,input.p));
    ComPtr<IWMHeaderInfo> header;
    check(s.writer->QueryInterface(__uuidof(IWMHeaderInfo),reinterpret_cast<void**>(header.put())));
    for (const auto& tag:tags) {
        std::wstring key=wide(tag.first);
        if (!_stricmp(tag.first.c_str(),"Artist")) key=L"Author";
        else if (!_stricmp(tag.first.c_str(),"Album")) key=L"WM/AlbumTitle";
        else if (!_stricmp(tag.first.c_str(),"AlbumArtist")) key=L"WM/AlbumArtist";
        else if (!_stricmp(tag.first.c_str(),"Genre")) key=L"WM/Genre";
        else if (!_stricmp(tag.first.c_str(),"Year") || !_stricmp(tag.first.c_str(),"Date")) key=L"WM/Year";
        else if (!_stricmp(tag.first.c_str(),"Track") || !_stricmp(tag.first.c_str(),"TrackNumber")) key=L"WM/TrackNumber";
        else if (!_stricmp(tag.first.c_str(),"Comment")) key=L"Description";
        else if (!_stricmp(tag.first.c_str(),"Composer")) key=L"WM/Composer";
        else if (!_stricmp(tag.first.c_str(),"Lyrics")) key=L"WM/Lyrics";
        else if (!_stricmp(tag.first.c_str(),"ISRC")) key=L"WM/ISRC";
        else if (!_stricmp(tag.first.c_str(),"Subtitle")) key=L"WM/SubTitle";
        else if (!_stricmp(tag.first.c_str(),"Copyright_URL")) key=L"CopyrightURL";
        require(tag.second.size()<32767,E_INVALIDARG);
        check(header->SetAttribute(0,key.c_str(),WMT_TYPE_STRING,
            reinterpret_cast<const BYTE*>(tag.second.c_str()),WORD((tag.second.size()+1)*2)));
    }
    check(s.writer->SetOutputFilename(path.c_str()));
    if (options.wma_mode==1) {
        check(s.writer->QueryInterface(__uuidof(IWMWriterPreprocess),reinterpret_cast<void**>(s.preprocess.put())));
        wchar_t dir[MAX_PATH]{}, file[MAX_PATH]{};
        DWORD length=GetTempPathW(MAX_PATH,dir);
        require(length && length<MAX_PATH,HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND));
        wincheck(GetTempFileNameW(dir,L"enc",0,file));
        s.spool=CreateFileW(file,GENERIC_READ|GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,
                           FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_DELETE_ON_CLOSE|FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
        if (s.spool==INVALID_HANDLE_VALUE) {
            DWORD error=GetLastError(); DeleteFileW(file); throw Failure{HRESULT_FROM_WIN32(error)};
        }
    }
    check(s.writer->BeginWriting()); s.active=true;
    if (s.preprocess) {
        // Query after BeginWriting: the system codec has not been initialized before it.
        DWORD passes{}; check(s.preprocess->GetMaxPreprocessingPasses(0,0,&passes));
        require(passes>=1,HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED));
        check(s.preprocess->SetNumPreprocessingPasses(0,0,1));
        check(s.preprocess->BeginPreprocessingPass(0,0));
    }
}
void WmaSession::write(const short* pcm,DWORD frames) {
    auto& s=*impl_; require(s.active,E_UNEXPECTED);
    const DWORD bytes=frames*s.format.nBlockAlign;
    ComPtr<INSSBuffer> buffer; check(s.writer->AllocateSample(bytes,buffer.put()));
    BYTE* data{}; check(buffer->GetBuffer(&data)); std::memcpy(data,pcm,bytes); check(buffer->SetLength(bytes));
    const QWORD time=(s.frames/s.format.nSamplesPerSec)*10000000+
        ((s.frames%s.format.nSamplesPerSec)*10000000)/s.format.nSamplesPerSec;
    if (s.preprocess) {
        DWORD written{};
        wincheck(WriteFile(s.spool,&bytes,sizeof(bytes),&written,nullptr));
        require(written==sizeof(bytes),STG_E_WRITEFAULT);
        wincheck(WriteFile(s.spool,pcm,bytes,&written,nullptr));
        require(written==bytes,STG_E_WRITEFAULT);
        check(s.preprocess->PreprocessSample(0,time,0,buffer.p));
    } else check(s.writer->WriteSample(0,time,0,buffer.p));
    s.frames+=frames;
}
void WmaSession::finish() {
    auto& s=*impl_;
    if (!s.active) return;
    if (s.preprocess) {
        check(s.preprocess->EndPreprocessingPass(0,0));
        LARGE_INTEGER beginning{};
        wincheck(SetFilePointerEx(s.spool,beginning,nullptr,FILE_BEGIN));
        std::uint64_t frames{};
        for (;;) {
            DWORD bytes{}, read{};
            wincheck(ReadFile(s.spool,&bytes,sizeof(bytes),&read,nullptr));
            if (!read) break;
            require(read==sizeof(bytes) && bytes && bytes<=4096*s.format.nBlockAlign && bytes%s.format.nBlockAlign==0,STG_E_READFAULT);
            ComPtr<INSSBuffer> buffer; check(s.writer->AllocateSample(bytes,buffer.put()));
            BYTE* data{}; check(buffer->GetBuffer(&data));
            wincheck(ReadFile(s.spool,data,bytes,&read,nullptr));
            require(read==bytes,STG_E_READFAULT);
            check(buffer->SetLength(bytes));
            QWORD time=(frames/s.format.nSamplesPerSec)*10000000+
                ((frames%s.format.nSamplesPerSec)*10000000)/s.format.nSamplesPerSec;
            check(s.writer->WriteSample(0,time,0,buffer.p)); frames+=bytes/s.format.nBlockAlign;
        }
        CloseHandle(s.spool); s.spool=INVALID_HANDLE_VALUE;
        s.preprocess.reset();
    }
    s.active=false; check(s.writer->EndWriting());
}
}
