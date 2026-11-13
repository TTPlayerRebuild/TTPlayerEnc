#include "lame_backend.h"
#include "lame_namespace.h"
#include <lame.h>
#include <climits>

// Only the tag writer is bridged; quantization/psychoacoustics stay upstream.
extern "C" size_t CDECL lame_get_lametag_frame(lame_t, unsigned char*, size_t, const unsigned char*, unsigned long);
extern "C" size_t ttp_lame_3931_tag_size(lame_t);

namespace ttp::enc {
namespace {
struct Context {
    lame_t lame;
    size_t header_offset;
    std::uint64_t bytes{};
    BYTE header[4]{};
    size_t header_bytes{};
    void emitted(const BYTE* data, int size) {
        if(size<=0) return;
        const auto start=std::uint64_t(header_offset)+header_bytes;
        if(start>=bytes && start<bytes+size && header_bytes<sizeof(header)) {
            const auto skip=size_t(start-bytes);
            const auto count=std::min(sizeof(header)-header_bytes,size_t(size)-skip);
            std::memcpy(header+header_bytes,data+skip,count); header_bytes+=count;
        }
        bytes+=size;
    }
};
void* open(const WAVEFORMATEX& format,const Options& options) {
    auto lame=lame_init(); require(lame!=nullptr,E_OUTOFMEMORY);
    try {
        require(lame_set_in_samplerate(lame,int(format.nSamplesPerSec))>=0);
        require(lame_set_out_samplerate(lame,int(format.nSamplesPerSec))>=0);
        require(lame_set_num_channels(lame,format.nChannels)>=0);
        lame_set_mode(lame,format.nChannels==1?MONO:options.joint?JOINT_STEREO:STEREO);
        lame_set_quality(lame,2);
        lame_set_copyright(lame,1); lame_set_original(lame,1);
        lame_set_error_protection(lame,1); lame_set_extension(lame,1);
        if(options.mp3_mode==0) { lame_set_VBR(lame,vbr_off); lame_set_brate(lame,options.mp3_value); }
        else if(options.mp3_mode==1) { lame_set_VBR(lame,vbr_abr); require(lame_set_preset(lame,options.mp3_value)>=0); }
        else if(options.mp3_value>=1100) { lame_set_VBR(lame,vbr_default); lame_set_VBR_q(lame,options.mp3_value-1100); }
        else {
            // Match the legacy preset entry path. In 3.93.1, Insane changes
            // bitrate/tuning but leaves this initial VBR mode for init_params.
            lame_set_VBR(lame,vbr_mtrh);
            const int presets[]{R3MIX,STANDARD,EXTREME,INSANE,STANDARD_FAST,EXTREME_FAST,MEDIUM,MEDIUM_FAST};
            require(lame_set_preset(lame,presets[options.mp3_value-1000])>=0);
        }
        // 3.93.1 has no automatic-ID3 toggle. With no id3tag_set_* calls its
        // unchanged tag flags emit no ID3; the host still owns metadata.
        lame_set_bWriteVbrTag(lame,options.mp3_mode!=0);
        require(lame_init_params(lame)>=0,E_INVALIDARG);
        return new Context{lame,ttp_lame_3931_tag_size(lame)};
    } catch(...) { lame_close(lame); throw; }
}
void close(void* context) {
    auto* c=static_cast<Context*>(context); lame_close(c->lame); delete c;
}
int encode(void* context,short* pcm,int frames,bool stereo,BYTE* output,int capacity) {
    auto* c=static_cast<Context*>(context);
    const int n=stereo?lame_encode_buffer_interleaved(c->lame,pcm,frames,output,capacity):
        lame_encode_buffer(c->lame,pcm,pcm,frames,output,capacity);
    c->emitted(output,n); return n;
}
int flush(void* context,BYTE* output,int capacity) {
    auto* c=static_cast<Context*>(context);
    const int n=lame_encode_flush(c->lame,output,capacity); c->emitted(output,n); return n;
}
size_t tag(void* context,BYTE* output,size_t capacity) {
    auto* c=static_cast<Context*>(context);
    if(!lame_get_bWriteVbrTag(c->lame)) return 0;
    // Upstream's FILE tag writer uses signed 32-bit lengths. Fail explicitly
    // rather than wrap a >2 GiB legacy VBR tag; CBR has no such tag operation.
    require(c->bytes<=LONG_MAX,E_NOTIMPL);
    const auto size=lame_get_lametag_frame(c->lame,nullptr,0,c->header,static_cast<unsigned long>(c->bytes));
    if(size) require(c->header_bytes==sizeof(c->header),E_FAIL);
    return output?lame_get_lametag_frame(c->lame,output,capacity,c->header,static_cast<unsigned long>(c->bytes)):size;
}
constexpr LameBackend backend{open,close,encode,flush,tag};
}
const LameBackend& TTP_LAME_BACKEND() { return backend; }
}
