#pragma once
#include "ttp_enc_abi.h"
#include <cstddef>

namespace ttp::enc {
// Input format has already been validated by the encoder. Source stays intact.
void convert_pcm16(const BYTE* source, short* output, size_t samples, const WAVEFORMATEX& format);
}
