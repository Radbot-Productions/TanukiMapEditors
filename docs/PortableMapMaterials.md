# Portable map materials

Use the supplied Tanuki shaders and sample materials through each engine's regular material inspector or shader editor. There is no separate Tanuki material window. The shaders expose the inputs consumed by the game's glTF material loader. Native lighting and shaders still differ; matching inputs does not promise identical rendered pixels.

| Input | Meaning |
| --- | --- |
| Base Color / Texture | Linear tint multiplied by the sRGB color image. |
| Roughness / Texture | Scalar without an image; native texture/factor controls with an image. Higher values are rougher. |
| Metallic / Texture | Scalar without an image; native texture/factor controls with an image. |
| Normal Texture / Strength | Tangent-space OpenGL normal image, with a separate strength. |
| AO Texture | Optional occlusion image, exported to the game's occlusion input. |
| Emission Color / Texture / Strength | Optional PBR emission: color, optional sRGB image, and linear multiplier. |
| Opacity / Alpha Cutoff | Cutout and Transparent surfaces; base-image alpha multiplies opacity. |
| UV Tiling / Offset | Shared UV0 transform for all slots. |

PNG/JPEG images up to 8192 pixels are portable. Data maps use raw channel values. The three surfaces are PBR, Cutout and Transparent. Emission is an optional PBR input, with an emission-enabled PBR example. Color pickers use their engine's conventions; export converts colors to linear glTF factors. Duplicate a sample or create a material instance before customizing it. Extra shader features and arbitrary graphs require baking or material repair.

## Editor entry points

- **Blender:** use **Load Material Presets**, then Blender's Material Properties and Shader Editor. `samples/TanukiMaterials.blend` (installed under `tanuki_maps/materials`) contains material assets and reusable **Tanuki PBR**, **Tanuki Cutout**, and **Tanuki Transparent** shader groups. Connect ordinary image nodes to the group inputs; use Non-Color data for roughness, metallic, normal and AO. Separate Color nodes select scalar texture channels. Normal Texture accepts the image color; the group applies Normal Strength. Cutout exposes Alpha and Alpha Cutoff. PBR emits when its emission color/strength is nonzero. Use group inputs for material edits; changing the shader definition requires baking or extending the exporter. Export uses a disposable standard graph, leaving the authored group intact.
- **Unity:** choose **Tanuki > Materials > PBR / Cutout / Transparent**. Each command creates an editable sample copy in the selected Project folder. Shader definitions and samples live inside the plugin (`Shaders` and `Runtime/Materials`); the normal Material Inspector shows only supported shader inputs. Choose **Tanuki/PBR**, **Tanuki/Cutout**, or **Tanuki/Transparent** for Built-in rendering. URP projects additionally install the included **Tanuki Materials for URP** package at `Integrations~/URP/package.json`; its shaders also remain plugin-owned. Roughness is exposed directly. The combined metallic/roughness image uses **green for roughness, blue for metallic**; its scalar factors multiply those channels. Disable sRGB on metallic/roughness and AO images, and import normals as Normal Map. PBR has an optional Emission toggle. There is no material entry in the map window. Existing Standard/URP Lit materials remain export-compatible and can be converted with **Tanuki > Materials > Use Tanuki Shaders on Selected Map**.
- **Godot:** duplicate a sample `.tres` under `res://addons/tanuki_maps/materials` and edit its native `ShaderMaterial` Inspector. The three Tanuki shaders live under `addons/tanuki_maps/shaders`. Roughness, metallic and AO texture channels are selectable; scalar factors multiply their textures. PBR has optional emission. The exporter projects these inputs into a temporary `StandardMaterial3D`; the authored shader stays intact. Compatible imported materials using other sampler modes retain their native material so import does not change filtering or wrap behavior.
- **Unreal:** the **Tanuki** dropdown beside toolbar Play has **Actors** and **Materials** sections. Select a writable project Content Browser folder, then choose a PBR, Cutout or Transparent material instance. Each instance receives a unique name and the plugin sample as parent; edit it in the normal Material Instance Editor. Bases and samples live under `/PortableMaps/Materials`. Parameters include `baseColorFactor`, `roughnessFactor`, `metallicFactor`, `normalStrength`, shared `uvTilingX/Y` and `uvOffsetX/Y`, and supported texture inputs. PBR adds optional `emissiveFactor` and `emissiveStrength`; Cutout adds `alphaCutoff`. Metallic/roughness images use green and blue, with sRGB disabled.

Native material values are authoritative at export. There is no generated-preview rebuild step. Removing a texture uses the native scalar/color value; assigning a texture follows that engine's supported texture/factor semantics. Existing compatible native materials can export without adopting a separate recipe.

The serialized runtime payload remains standard glTF PBR inside `.tanukimap`; this feature adds no manifest version. AO reaches the runtime shader's occlusion input; its visible contribution depends on game lighting. Validate final appearance in the game.

## Validation

`Scripts/Test-TanukiPortableMaterials.ps1` runs through `Scripts/Invoke-TanukiAutomationScope.ps1`. It checks Blender surface/scalar/texture exports, Unity persistence and channel packing, Godot material round trips, Unity-to-Godot import/export, and embedded PNG channel values. `Scripts/Test-TanukiNativeMaterials.ps1` accepts that run's directory as `InputDirectory` and also runs through an automation scope. It discovers and runs `Radbot.PortableMaps.SharedMaterialInputs` and `SharedMaterialRoundTrip`, then compares the final exported material factors and texture slots. The wrapper sets and restores `PortableMaps.MaterialFixtureDirectory` automatically.

Blender sample cube winding is outward. Checks include every generated floor, wall, block and ramp, alongside the mirrored-hierarchy export regression.
