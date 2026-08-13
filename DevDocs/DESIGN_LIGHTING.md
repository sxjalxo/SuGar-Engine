# Design record — lights as components (directional / point / ambient)

*Written before the code. M4 Level 3 (voxel game: day-night cycle + placed torches)
forced it.*

## The forcing problem

Lights were a **scene-level `std::vector<Light>`** owned by the app, with
`Light { position, color }` and nothing else. Three consequences the voxel game hit in
the same afternoon:

1. **A game module cannot touch them.** The vector lives in the engine layer; a behavior
   links only Core. So a day-night cycle — the single most ordinary thing a survival game
   does — was not expressible at all.
2. **There is no sun.** Every light is a positional point light with no falloff, so
   "sunlight" had to be faked by a point light parked 120 units up, which lights the world
   unevenly and gives the shadow pass a bad frustum.
3. **A torch cannot be placed.** Placing a light at runtime means creating one, and
   creation is engine-side.

Plus two limits the same game reaches immediately: `MAX_LIGHTS = 4` (a handful of torches
exhausts it) and a hardcoded `0.12` ambient term in the fragment shader (so "night" cannot
be darker than noon).

## The decision — the camera seam, again

The engine already answered this exact shape of question once, for cameras (M4 L3 seam
#16): the thing a game needs to drive becomes a **component**, and its *pose* is derived
from the entity's world transform rather than stored twice.

```cpp
struct LightComponent {          // rendering/LightComponent.h, Core
    LightType type;              // Directional | Point | Ambient
    glm::vec3 color;
    float intensity;
    float range;                 // Point only
    bool castsShadow;
    bool active;
};
```

- **Position and direction are NOT fields.** Position = the entity's world translation;
  direction = world rotation · −Z. Same convention as `CameraComponent`, same reason
  (Rule 21b): a second copy of a pose is a second owner and desyncs on snapshot restore.
- The engine collects light entities each frame and produces the draw list's lights. The
  existing scene-level `lights` array keeps working unchanged — old scenes and the editor's
  default lighting must not break — and component lights are simply appended.
- A game drives the sun by rotating an entity, and places a torch by creating one. Both
  are ordinary Core operations, so both work from a behavior.

### Types

`Directional` (sun/moon: direction only, infinite reach), `Point` (torch: position +
range), `Ambient` (the sky term: no position at all). Ambient is a *light type* rather
than a scene setting because the game animates it across the day exactly like the sun, and
a setting that only the engine can change would put us back where we started. Unity keeps
ambient in scene lighting settings and Unreal uses a Sky Light actor; the actor/component
form is the one a game can drive.

### What the shader gets

The GPU-side representation stays two `vec4`s per light, using the classic homogeneous
convention rather than a new field:

- `lightPositions[i].xyz` = world position (point) or **direction toward the light**
  (directional); `.w` = `0` marks directional, otherwise it is the point light's **range**.
- `lightColors[i].rgb` = colour **premultiplied by intensity** (the shader should not have
  to know what an intensity is); `.w` unused.
- A new `ambient` vec4 replaces the hardcoded `0.12`.
- Point lights attenuate as `clamp(1 − d/range, 0, 1)²` — the cheap, artist-predictable
  falloff, and range-limited rather than physical inverse-square so a torch's influence
  ends where the author says it does.

`MAX_LIGHTS` 4 → 8. Not "many": the renderer is forward and single-pass, so every light
costs every fragment. Eight is what a torch-lit cave scene needs; clustered/deferred
lighting is a renderer rewrite and is not forced (Rule 8, and the M4 not-required list).

### Which 8, when a world has 200 torches

The engine picks: the shadow-casting directional light first (so shadow indexing stays
`i == 0`), then the point lights **nearest the camera**, ties broken by entity id so the
choice is deterministic. Selection is derived per frame — it is a function of the camera
and the light set, so it is not state (Rule 21b).

### Shadows

Unchanged in scope: one shadow caster, index 0. It becomes *better* only in that a
directional caster now gives the shadow pass a real direction — the virtual light position
is `sceneCentre − direction × distance`, which is what the existing fit-to-scene frustum
wants. Multi-caster shadows stay deferred (C19), still unforced.

## Scope built now

1. Core `LightComponent` + `LightType`; `Light` (the render-list struct) grows
   `type`/`intensity`/`range` so scene-level and component lights converge on one form.
2. `Registry::lights` storage + `ComponentType::Light` + destroy/reset.
3. Engine: gather + select ≤ 8 per frame; upload; shadow caster selection.
4. Shader: directional/point/ambient + range attenuation; `MAX_LIGHTS` 8.
5. Serialization: optional `"light"` block per entity; scene `lights` array gains optional
   `type`/`intensity`/`range` (absent ⇒ today's point light, full back-compat).
6. Editor: inspector section.
7. Self-test: round-trip + selection order.

Not built: shadow cascades, multiple shadow casters, light cookies/IES, baked GI, area
lights, per-light shadow bias. None forced.
