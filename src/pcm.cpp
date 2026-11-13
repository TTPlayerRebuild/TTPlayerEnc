#include "pcm.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <emmintrin.h>

namespace ttp::enc {
namespace {
__m128i load128(const BYTE* source) {
    __m128i value;
    std::memcpy(&value,source,sizeof(value));
    return value;
}
__m128i round_pcm16(__m128d value) {
    // Classify the exponent as integer bits before any FP operation: both
    // infinities and all NaNs map to zero, as in the original scalar path.
    const auto exponent=_mm_set_epi32(0x7ff00000,0,0x7ff00000,0);
    auto invalid=_mm_cmpeq_epi32(_mm_and_si128(_mm_castpd_si128(value),exponent),exponent);
    invalid=_mm_shuffle_epi32(invalid,_MM_SHUFFLE(3,3,1,1));
    value=_mm_andnot_pd(_mm_castsi128_pd(invalid),value);
    value=_mm_max_pd(value,_mm_set1_pd(-1.0));
    value=_mm_min_pd(value,_mm_set1_pd(32767.0/32768.0));
    const auto scaled=_mm_mul_pd(value,_mm_set1_pd(32768.0));
    const auto whole=_mm_cvttpd_epi32(scaled);
    const auto fraction=_mm_sub_pd(scaled,_mm_cvtepi32_pd(whole));
    // lround ties away from zero, independently of MXCSR's rounding mode.
    // Adding +/-0.5 before truncation is NOT equivalent immediately below a
    // half-integer: that addition can itself round up. Compare the remainder.
    const auto up=_mm_and_pd(_mm_cmpge_pd(fraction,_mm_set1_pd(0.5)),_mm_set1_pd(1.0));
    const auto down=_mm_and_pd(_mm_cmple_pd(fraction,_mm_set1_pd(-0.5)),_mm_set1_pd(-1.0));
    return _mm_add_epi32(whole,_mm_cvttpd_epi32(_mm_or_pd(up,down)));
}
void store4(short* output, __m128i low, __m128i high) {
    const auto words=_mm_unpacklo_epi64(low,high);
    const auto packed=_mm_packs_epi32(words,words);
    std::memcpy(output,&packed,4*sizeof(short));
}
short float_tail(const BYTE* source, WORD bits) {
    double value;
    if(bits==64) std::memcpy(&value,source,sizeof(value));
    else { float sample; std::memcpy(&sample,source,sizeof(sample)); value=sample; }
    if(!std::isfinite(value)) value=0;
    value=std::clamp(value,-1.0,32767.0/32768.0);
    return short(std::lround(value*32768.0));
}
}
void convert_pcm16(const BYTE* source, short* output, size_t samples, const WAVEFORMATEX& format) {
    if(!samples) return;
    if(format.wFormatTag==WAVE_FORMAT_PCM) {
        switch(format.wBitsPerSample) {
        case 16:
            std::memcpy(output,source,samples*sizeof(short));
            return;
        case 8:
            for(size_t i=0;i<samples;++i) output[i]=short((int(source[i])-128)*256);
            return;
        case 24:
            for(size_t i=0;i<samples;++i) output[i]=short(WORD(source[i*3+1])|(WORD(source[i*3+2])<<8));
            return;
        default: // validated 32-bit PCM, retaining the original high 16 bits
            for(size_t i=0;i<samples;++i) output[i]=short(WORD(source[i*4+2])|(WORD(source[i*4+3])<<8));
            return;
        }
    }
    size_t i=0;
    if(format.wBitsPerSample==64) {
        for(;i+4<=samples;i+=4) {
            const auto low=round_pcm16(_mm_castsi128_pd(load128(source+i*8)));
            const auto high=round_pcm16(_mm_castsi128_pd(load128(source+i*8+16)));
            store4(output+i,low,high);
        }
    } else {
        const auto exponent=_mm_set1_epi32(0x7f800000);
        for(;i+4<=samples;i+=4) {
            auto bits=load128(source+i*4);
            const auto invalid=_mm_cmpeq_epi32(_mm_and_si128(bits,exponent),exponent);
            // Clear float NaNs before widening too (including signalling NaNs).
            const auto value=_mm_castsi128_ps(_mm_andnot_si128(invalid,bits));
            const auto low=round_pcm16(_mm_cvtps_pd(value));
            const auto high=round_pcm16(_mm_cvtps_pd(_mm_movehl_ps(value,value)));
            store4(output+i,low,high);
        }
    }
    const size_t stride=format.wBitsPerSample/8;
    for(;i<samples;++i) output[i]=float_tail(source+i*stride,format.wBitsPerSample);
}
}
