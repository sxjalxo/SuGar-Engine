#pragma once

#include <vector>

#include <glm/glm.hpp>

// Phase 18D — shared math for drawing 3D primitives over the viewport image.
//
// Extracted from the navigation overlay (18C) because none of it is about
// navigation: physics contacts, audio attenuation spheres, camera frusta, AI
// perception cones and every future viewport visualization need exactly the same
// behaviour, and each one would otherwise rediscover the same bug.
//
// **The bug worth encoding in a type.** The instinct when a point fails to project
// is `projection failed -> skip the primitive`. But perspective projection is not
// *undefined* for `w <= 0` in the sense of "no answer" — it means the primitive
// **crosses the near plane**. The primitive does not disappear; part of it is behind
// the viewer. Phrased that way, clipping is not a refinement, it is the only correct
// response, and the pipeline is:
//
//     3D primitive -> clip against near plane -> project -> draw
//
// not:
//
//     project -> if projection failed, skip
//
// The second version fails exactly when it matters: stand on a large ground quad and
// every corner behind you drops out, so the overlay vanishes at the moment you are
// close enough to need it.
//
// Deliberately free of ImGui and Vulkan — it is pure math over matrices, so it is
// headless-testable (Rule 9). Callers convert the resulting glm::vec2 to whatever
// their draw list wants.
namespace ViewportOverlay {

// Maps world space onto a rectangle of screen pixels, clipping at the near plane.
//
// `viewProj` must already have any renderer-specific Y convention applied (SuGar's
// Vulkan projection is Y-flipped relative to screen space; the editor un-flips it
// before constructing a Projector, as ImGuizmo requires too).
class Projector {
public:
    Projector(const glm::mat4& viewProj, float minX, float minY, float width, float height)
        : viewProj_(viewProj), minX_(minX), minY_(minY), width_(width), height_(height) {}

    glm::vec4 toClip(const glm::vec3& world) const {
        return viewProj_ * glm::vec4(world, 1.0f);
    }

    // In front of the near plane, i.e. safe to divide by w.
    static bool inFront(const glm::vec4& clip) { return clip.w >= kNearW; }

    // Only valid for a point already known to be in front — clip first.
    glm::vec2 toScreen(const glm::vec4& clip) const {
        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        return glm::vec2(minX_ + (ndc.x * 0.5f + 0.5f) * width_,
                         minY_ + (ndc.y * 0.5f + 0.5f) * height_);
    }

    // Sutherland-Hodgman against the near plane. Returns the clipped polygon, which
    // may have *more* vertices than the input (a corner cut off adds two), fewer, or
    // none at all when the whole polygon is behind the viewer.
    std::vector<glm::vec4> clipPolygon(const std::vector<glm::vec4>& polygon) const {
        std::vector<glm::vec4> result;
        if (polygon.size() < 2) {
            return result;
        }
        for (std::size_t i = 0; i < polygon.size(); ++i) {
            const glm::vec4& current = polygon[i];
            const glm::vec4& next = polygon[(i + 1) % polygon.size()];
            const bool currentIn = inFront(current);
            const bool nextIn = inFront(next);

            if (currentIn) {
                result.push_back(current);
            }
            // Crossing the plane in either direction contributes the intersection.
            if (currentIn != nextIn) {
                const float t = (kNearW - current.w) / (next.w - current.w);
                result.push_back(current + (next - current) * t);
            }
        }
        return result;
    }

    // World polygon straight to screen points. Empty when nothing survives clipping
    // (including the <3 vertex case, which is not drawable).
    std::vector<glm::vec2> projectPolygon(const std::vector<glm::vec3>& world) const {
        std::vector<glm::vec4> clip;
        clip.reserve(world.size());
        for (const glm::vec3& point : world) {
            clip.push_back(toClip(point));
        }

        const std::vector<glm::vec4> visible = clipPolygon(clip);
        std::vector<glm::vec2> screen;
        if (visible.size() < 3) {
            return screen;
        }
        screen.reserve(visible.size());
        for (const glm::vec4& vertex : visible) {
            screen.push_back(toScreen(vertex));
        }
        return screen;
    }

    // Segment version for polylines: false when the whole segment is behind the
    // viewer, otherwise the offending endpoint is trimmed onto the near plane. Done
    // per-segment so a route running past the camera keeps the part in front of it
    // rather than disappearing wholesale.
    bool clipSegment(glm::vec4 a, glm::vec4 b, glm::vec2& outA, glm::vec2& outB) const {
        const bool aIn = inFront(a);
        const bool bIn = inFront(b);
        if (!aIn && !bIn) {
            return false;
        }
        if (!aIn) {
            a = a + (b - a) * ((kNearW - a.w) / (b.w - a.w));
        } else if (!bIn) {
            b = b + (a - b) * ((kNearW - b.w) / (a.w - b.w));
        }
        outA = toScreen(a);
        outB = toScreen(b);
        return true;
    }

private:
    // Not zero: w == 0 is the projection singularity itself, and dividing by a
    // denormal just past it produces coordinates large enough to break draw lists.
    static constexpr float kNearW = 1e-3f;

    glm::mat4 viewProj_;
    float minX_;
    float minY_;
    float width_;
    float height_;
};

} // namespace ViewportOverlay
