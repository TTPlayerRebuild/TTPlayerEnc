#include "lame_backend.h"

namespace ttp::enc {
const LameBackend& lame_3101_backend();
const LameBackend& lame_40_backend();
const LameBackend& lame_41beta1_backend();

const LameBackend& lame_backend(LameVersion version) {
    switch(version) {
    case LameVersion::V3101: return lame_3101_backend();
    case LameVersion::V41Beta1: return lame_41beta1_backend();
    default: return lame_40_backend();
    }
}
}
