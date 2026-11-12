#include "audiooutput.h"

namespace dss {

const char *AudioOutput::stateName(State state)
{
    switch (state) {
    case State::NoPipeWire:
        return "no_pipewire";
    case State::NoTarget:
        return "no_target";
    case State::Missing:
        return "missing";
    case State::Connecting:
        return "connecting";
    case State::Playing:
        return "playing";
    case State::Error:
        return "error";
    }
    return "error";
}

} // namespace dss
