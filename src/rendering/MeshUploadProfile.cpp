#include "rendering/MeshUploadProfile.h"

#include <sstream>

namespace MeshUploadProfile {

namespace {
Counters g_counters;

std::string ms(double value) {
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(2);
    out << value;
    return out.str();
}
} // namespace

Counters& counters() { return g_counters; }

void reset() { g_counters = Counters{}; }

std::string Counters::describe() const {
    const double perMesh = meshes > 0 ? totalMs() / static_cast<double>(meshes) : 0.0;
    std::ostringstream out;
    out << meshes << " meshes, " << buffers << " buffers, " << (bytes / 1024u) << " KiB: total "
        << ms(totalMs()) << " ms (" << ms(perMesh) << " ms/mesh)"
        << " [validate " << ms(validateMs) << " translate " << ms(translateMs)
        << " bufferCreate " << ms(bufferCreateMs) << " mapCopy " << ms(mapCopyMs)
        << " command " << ms(commandMs) << " submitWait " << ms(submitWaitMs)
        << " destroy " << ms(destroyMs) << "]"
        << " allocations " << deviceAllocations;
    if (maxAllocationsAllowed > 0) {
        out << " (driver limit on live allocations " << maxAllocationsAllowed << ")";
    }
    return out.str();
}

} // namespace MeshUploadProfile
