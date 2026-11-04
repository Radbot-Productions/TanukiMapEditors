# Set up Unreal Engine

Target: **Unreal Engine 5.8.1 on Windows x64**. Precompiled Unreal plugins are tied to an engine BuildId. Check the release receipt before installing; the version label alone does not prove binary compatibility.

1. Install Unreal through the [Epic Games Launcher](https://www.unrealengine.com/download).
2. Download the **Tanuki-Unreal-…zip** asset from [Releases](https://github.com/Radbot-Productions/TanukiMapEditors/releases), checking its engine version/BuildId note.
3. Extract to a new writable folder. Keep the complete project and its `Plugins` folders together.
4. Open `TanukiMapAuthoring.uproject` with the matching engine.
5. If prompted, enable **PortableMaps**, **glTFRuntime**, and **TanukiAuthoringPreview**, then restart this editor.
6. Open **Tools → Tanuki Map Editor**. The **Tanuki** toolbar dropdown also exposes authoring components and material creation.

If Unreal reports missing modules or a different engine build, use the matching kit or rebuild the included plugin source using Unreal's C++ toolchain. Do not edit `.modules` BuildIds to force-load incompatible DLLs. See Epic's [plugin documentation](https://dev.epicgames.com/documentation/unreal-engine/plugins-in-unreal-engine?lang=en-US). The public source project contains editor plugins and preview content, not the private Tanuki game project or Unreal Engine source.

## First map

Choose **New → Create Empty Map**, Room or Courtyard. Keep scenery under the map root, one Play Boundary, four or more Player Starts and static collision. New boundaries extend beneath the floor rather than sharing its top plane.

Use Components for Player Starts, Point/Spot/Directional lights, **Lighting Settings** and **Skybox**. Lighting Settings selects the root's Cartoon Street Day/Night profile; the linked sky and base illumination change together. Edit ordinary lights through native component Details, using candela for Point/Spot and lux for Directional. Extra lights are independent of base lighting.

Use **Tanuki → Materials** to create editable native Tanuki PBR, Cutout or Transparent material instances in the selected Content Browser folder. Use the normal Material Instance Editor and exposed texture/scalar parameters. Scale references and generated preview helpers stay outside the portable map data.

These environment controls are part of the upcoming 0.2 kit. The standalone editor approximates the common sky profiles; the private game player uses the original game lighting levels.

## Preview and Workshop

The regular Unreal toolbar **Play** runs the kit's local movement preview. **Play in Tanuki** uses the separately provided full gameplay-preview package. Choose its `TanukiMapPreview.exe` with **Choose Tanuki Preview**, retaining the full package directory. Each launch snapshots current scene state; close/relaunch after edits. No manual export or map save is required for the snapshot.

Save your source `.umap` and content assets separately. Export writes a single `.tanukimap`; arbitrary Blueprints, scripts, materials and gameplay actors do not travel with it.

Workshop uses Steam's logged-in account and Tanuki Tag's current access restrictions. Use Open to browse subscribed maps and Export to create/update an item. Save after publishing to retain the Workshop association. Include the engine version/BuildId, plugin version and relevant Output Log when reporting problems.
