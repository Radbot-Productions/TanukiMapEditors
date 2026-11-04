# Portable map lights

Blender, Unity, Godot and Unreal support **Point**, **Spot**, and **Directional**
lights as authored map elements. Add them from Blender's Tanuki sidebar,
Unity's Tanuki > Components, Godot's Components menu, or Unreal's Tanuki Components
menu. Edit the portable light settings in Blender/Unity/Godot; Unreal uses the
native component Details. Local preview sun/sky helpers stay outside the map root.

| Field | Shared meaning |
| --- | --- |
| `type` | `point`, `spot`, `directional` |
| `position`, `rotation` | Unreal centimetres; unit quaternion; optical forward +X |
| `color` | Linear RGB, each channel 0–1 |
| `intensity` | Candela for Point/Spot, lux for Directional; 0–10,000,000 |
| `range` | Positive centimetres, at most 10,000,000; ignored by Directional |
| `innerCone`, `outerCone` | Half angles in degrees; 0 <= inner <= outer < 90; outer > 0 |
| `shadows` | Boolean shadow enable |

The light array is limited to 256 entries and shares the map's global element-ID
namespace. Maps containing lights use **manifest schema 3**. The outer TMAP
container remains version 1. Maps without lights retain schema 2, and schemas
1/2 remain readable. Old readers reject schema 3 rather than silently losing
lights. Update all participating tools and the game runtime before exchanging
light-containing maps. Area/plane lights are not supported.

Unity/Godot portable components own photometric values independently of their
local renderer's exposure/energy conventions. Blender uses the glTF-style 683
lm/W conversion for its native preview; portable values remain authoritative.
Preview brightness, soft shadows, spot falloff, environment light and tone mapping
can differ across editors. These settings do not promise identical rendered pixels.

Unreal Point/Spot lights require candela units, inverse-square falloff and zero
source dimensions. Only the shared fields above are portable. Static scenery
and collision remain in the GLB; lights live in the manifest, so GLB importers
cannot independently instantiate a second copy of the lights.

The runtime owner is `PortableMaps`, including `PortableMapLighting` and
`APortableMapScene`. `UPortableMapBuildTask` stages light components inactive,
alongside the existing mesh/collision preparation. Its current-request query
and cancellation owner gate completion. Transactional replacement and prepared
resource reuse retain lights; failure/staleness preserves the previous map.
The existing integrating session/lifecycle owner calls `SetContentActive` to
enable or retire all map presentation, including lights. `Clear`, cancellation
and `EndPlay` destroy owned components. No light-specific lifecycle coordinator
or participant-phase inference is introduced.

Validation entrypoints (run PowerShell drivers through
`Scripts/Invoke-TanukiAutomationScope.ps1` with `RunDirectory={AutomationRoot}`):

- `Test-TanukiBlender.ps1`: Blender templates, collision views, import/export,
  light/transform round trips, invalid inputs and transactional file preservation.
- `Test-TanukiPortableLights.ps1`: Blender ↔ Unity and Blender ↔ Godot transfers
  with all three types, positions, orientation, color and photometric values.
- `Test-TanukiPortableLightRuntime.ps1`: discovers the three native Light tests,
  rejects missing/empty batches, then verifies Blender ↔ Unreal transfer.
- `Test-TanukiBlenderInterface.ps1`: install the distributable in isolated Blender
  preferences and capture the actual panel and collision rendering for review.

Native tests are `Radbot.PortableMaps.LightContract`, `LightEditorRoundTrip`, and
`LightResources`. The resource test checks inactive preparation, activation,
deactivation, async prepared reuse, stale-request rejection, async file loading
and teardown. It creates its own temporary world; it never replaces the live
editor map. The editor round trip uses fresh unsaved packages and does not save
authored assets. Native/model checks do not prove game presentation, multiplayer
lighting, physical input, or final rendered material/lighting parity.
