#include "lame_backend.h"
#include "lame_namespace.h"
#include <lame.h>

// Compiled separately against each version's own public header and symbols.
namespace ttp::enc {
namespace {
void* open(const WAVEFORMATEX& format, const Options& options) {
    auto lame=lame_init(); require(lame!=nullptr,E_OUTOFMEMORY);
    try {
        require(lame_set_in_samplerate(lame,int(format.nSamplesPerSec))>=0);
        require(lame_set_out_samplerate(lame,int(format.nSamplesPerSec))>=0);
        require(lame_set_num_channels(lame,format.nChannels)>=0);
        lame_set_mode(lame,format.nChannels==1?MONO:options.joint?JOINT_STEREO:STEREO);
        lame_set_quality(lame,2);
        lame_set_copyright(lame,1);
        lame_set_original(lame,1);
        lame_set_error_protection(lame,1);
        lame_set_extension(lame,1);
        if(options.mp3_mode==0) { lame_set_VBR(lame,vbr_off); lame_set_brate(lame,options.mp3_value); }
        else if(options.mp3_mode==1) { lame_set_VBR(lame,vbr_abr); require(lame_set_preset(lame,options.mp3_value)>=0); }
        else if(options.mp3_value>=1100) { lame_set_VBR(lame,vbr_default); lame_set_VBR_q(lame,options.mp3_value-1100); }
        else {
            const int presets[]{R3MIX,STANDARD,EXTREME,INSANE,STANDARD_FAST,EXTREME_FAST,MEDIUM,MEDIUM_FAST};
            require(lame_set_preset(lame,presets[options.mp3_value-1000])>=0);
        }
        lame_set_write_id3tag_automatic(lame,0);
        lame_set_bWriteVbrTag(lame,options.mp3_mode!=0);
        require(lame_init_params(lame)>=0,E_INVALIDARG);
        return lame;
    } catch(...) { lame_close(lame); throw; }
}
void close(void* context) { lame_close(static_cast<lame_t>(context)); }
int encode(void* context, short* pcm, int frames, bool stereo, BYTE* output, int capacity) {
    auto lame=static_cast<lame_t>(context);
    return stereo?lame_encode_buffer_interleaved(lame,pcm,frames,output,capacity):
        lame_encode_buffer(lame,pcm,pcm,frames,output,capacity);
}
int flush(void* context, BYTE* output, int capacity) {
    return lame_encode_flush(static_cast<lame_t>(context),output,capacity);
}
size_t tag(void* context, BYTE* output, size_t capacity) {
    return lame_get_lametag_frame(static_cast<lame_t>(context),output,capacity);
}
constexpr LameBackend backend{open,close,encode,flush,tag};
}
const LameBackend& TTP_LAME_BACKEND() { return backend; }
}
