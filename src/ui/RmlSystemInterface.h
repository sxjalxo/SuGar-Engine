#pragma once

#include <RmlUi/Core/SystemInterface.h>

// SuGar's RmlUi SystemInterface (Phase 16B). RmlUi calls back into this for the two
// things it can't provide itself: elapsed time (it drives transitions/animations
// from it) and log routing. Engine-layer only — RmlUi never enters SuGarCore, which
// must stay free of platform UI (RULES.md Rule 15).
class RmlSystemInterface : public Rml::SystemInterface {
public:
    double GetElapsedTime() override;
    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override;
};
