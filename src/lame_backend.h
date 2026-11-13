#pragma once
#include "encoder.h"

namespace ttp::enc {
// Each backend owns its opaque context. Never pass it to another LAME version.
struct LameBackend {
    void* (*open)(const WAVEFORMATEX&, const Options&);
    void (*close)(void*);
    int (*encode)(void*, short*, int frames, bool stereo, BYTE*, int capacity);
    int (*flush)(void*, BYTE*, int capacity);
    size_t (*tag)(void*, BYTE*, size_t capacity);
};
const LameBackend& lame_backend(LameVersion);
}
