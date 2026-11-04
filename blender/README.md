# Tanuki Map Editor for Blender

Windows authoring preview targeting Blender 4.5.15 LTS. Install the complete ZIP
through **Edit > Preferences > Add-ons > Install from Disk**, enable **Tanuki Map
Editor**, then open the 3D viewport sidebar (N), **Tanuki** tab.

- **New:** Empty Map, Room or Courtyard. Each creates a separate scene, preserving
  existing unsaved work, with a floor, one boundary, four starts and preview sun.
- **Open:** a `.tanukimap` file or Steam Workshop map. Imported geometry, textures,
  collision, starts and lights remain editable. Save the scene as a `.blend` file.
- **Export:** validate, save one `.tanukimap`, or publish/update through Steam.
  Save your `.blend` after publishing to retain its item association. Opening a
  different author's Workshop map creates a new map identity.

Use ordinary Blender transforms to build under the Map Root. Keep root scale at
one and scene units in metres. **Add Selection to Map** parents new geometry while
retaining its placement; export rejects scenery accidentally left outside the root.
**New PBR Material** creates a portable material in the selected mesh's active slot.
Select a mesh and choose **Export As**: Scenery,
Scenery + Collision, Collision Only, or Exclude. Modifiers are evaluated for both
exported geometry and the green collision overlay. Collision-only meshes export
as invisible blocking triangles; visible scenery does not automatically collide.
Unsupported dynamic/animated content and material features produce export errors.
Use Principled BSDF materials with embedded PNG textures; bake procedural shaders.

**Show Collision Through Geometry** draws collision edges in green with X-ray
visibility. **Collision Only / Restore View** hides scenery without collision and
restores the previous visibility when toggled again. Neither changes export use.
Blue wire outlines show the boundary; use Box or Cylinder, with equal X/Y cylinder
dimensions. Export needs exactly one boundary and at least four player starts.
Cyan arrows show player-start facing (local -Y in Blender).

**Elements** adds starts, boundaries and Point, Spot or Directional lights. Native
Blender light objects represent those lights in the viewport; edit their portable
settings in the Tanuki panel. Point/Spot intensity is candela, Directional is lux;
range is metres and spot angles are half angles in degrees. Color, range, cone
angles and shadows survive map round trips. Native preview lighting/exposure can
differ between editors. Plane/area lights are outside this version's contract.
The sun created outside the map root is local preview illumination, not exported.

**Play in Tanuki** exports a snapshot and launches `TanukiMapPreview.exe`, the
dedicated gameplay player using the real Tanuki movement, climbing, gliding and
camera code. Kits can bundle it; source-only kits select its executable in add-on
preferences. Escape opens Resume, Free Camera / Tanuki Movement, Restart at Spawn,
Settings and Exit Preview. Free camera uses WASD, Q/E, mouse look and Shift for
speed. Closing it retires the snapshot while Blender stays open. Native editor
lighting may still differ; final multiplayer behavior needs separate game checks.

Workshop uses the logged-in Steam client and Tanuki Tag app 5128560. The Windows
helper bundles its .NET runtime and the same pinned Steamworks.NET bindings as
the other tools. No password or game process is required. The Workshop remains
subject to its current access restrictions. Requests/results and any assigned
item ID remain under Blender's configuration `tanuki_maps/workshop` folder.
Inspect an interrupted job before creating another item. Uploads are external
operations; local export works without Steam. Publishing without a selected PNG
generates an overview from exported scenery only.

Light maps use manifest schema 3 and need an updated runtime/editor. Maps without
lights retain schema 2. Earlier schemas remain importable. Geometry stays baked
in map space; this is not an archive of original Blender modifier history or
arbitrary shader graphs. Supported names, hierarchy and transforms travel through
the shared optional authoring metadata; collision and scenery retain paired
ownership when provided by the source editor.

Development: add this tool directory to Blender's Python path, import
`tanuki_maps`, then call `register()`. Build the distributable with
`Scripts/Build-TanukiBlenderKit.ps1` through `Invoke-TanukiAutomationScope.ps1`.

Use **Load Material Presets**, then Blender's normal Material Properties and
Shader Editor. The reusable **Tanuki PBR**, **Tanuki Cutout** and **Tanuki Transparent**
shader groups expose supported inputs; PBR emission is optional. Duplicate a sample
before editing it. `samples/TanukiMaterials.blend` contains material and shader-group
assets, including an emission-enabled PBR example. Assign textures through native
image nodes and use Non-Color data for roughness/metallic/normal/AO. Connect normal
image color to **Normal Texture**; the group applies Normal Strength. Without
textures, use scalar inputs. Cutout exposes Alpha and Alpha Cutoff. No custom
material window or preview rebuild is required. Export reads the group inputs
without changing the authored shader. See `Docs/PortableMapMaterials.md`
in the Tanuki repository for the cross-engine parameter and packing conventions.
