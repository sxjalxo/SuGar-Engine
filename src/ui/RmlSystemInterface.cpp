#include "ui/RmlSystemInterface.h"

#include <chrono>
#include <iostream>

double RmlSystemInterface::GetElapsedTime() {
    using namespace std::chrono;
    static const auto start = steady_clock::now();
    return duration<double>(steady_clock::now() - start).count();
}

bool RmlSystemInterface::LogMessage(Rml::Log::Type /*type*/, const Rml::String& message) {
    // Rml::String is std::string-compatible; route everything to stdout for now.
    std::cout << "[RmlUi] " << message << "\n";
    return true;
}
