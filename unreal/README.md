# Unreal setup

Target: **Unreal Engine 5.8.1, Windows x64**, precompiled engine **BuildId 55116800**. Plugins require a matching engine build; do not rewrite .modules BuildIds to force-load incompatible binaries.

1. Install Unreal through the [Epic Games Launcher](https://www.unrealengine.com/download), using the matching build or rebuilding the included source with Unreal's C++ toolchain.
2. Download **Tanuki-Unreal-0.2.0-preview.3.zip** from the [editor release](https://github.com/Radbot-Productions/TanukiMapEditors/releases/tag/editors-v0.2.0-preview.3), and extract to a new writable folder.
3. Open `TanukiMapAuthoring.uproject`. Keep the complete Plugins folders together. PortableMaps, glTFRuntime and TanukiAuthoringPreview must be enabled.
4. Open **Tools → Tanuki Map Editor**. Use New → Empty Map, Room, or Courtyard. The Tanuki toolbar dropdown also exposes components and material creation.

Keep static scenery under the Portable Map Root, one play boundary, and four or more player starts. New sample boundaries extend beneath the floor. Use native mesh collision or separate invisible collider meshes. The generated Tanuki/Tengu scale references use baked static meshes, align their feet to the floor and stay clear of sample blocks; they are excluded from export.

Lighting Settings selects Cartoon Street Day/Night on the root. Enable Override Sun to adjust its rotation, color, lux and shadow setting in Details. The linked sky and illumination switch together; extra Point/Spot/Directional lights remain independent. Point/Spot units are candela and Directional units are lux. The standalone editor approximates the common profiles; Play in Tanuki uses the original game lighting levels.

Use Tanuki → Materials to create native Tanuki material instances in your selected Content Browser folder. Edit supported textures/scalars in the normal Material Instance Editor. Save your source `.umap` and content separately. Arbitrary Blueprints, gameplay actors and scripts do not travel in `.tanukimap` files.

The regular Unreal toolbar Play is the local authoring helper. **Play in Tanuki** launches the shared actual-game movement preview described below. The public source includes authoring plugins/content, not the private game or Unreal Engine source.

## Play in Tanuki

Press **Play in Tanuki**. It automatically finds the active scene's single map and exports its current state, including unsaved scene edits. **No manual map save/export, executable picker, or Workshop refresh is required.** Close and relaunch after further edits; this is a fresh snapshot, not live synchronization.

The first use downloads the separate [Windows preview player](https://github.com/Radbot-Productions/TanukiMapEditors/releases/tag/player-v0.2.0-preview.3), about **4.34 GB**. Progress appears in the editor. The plugins verify the download and install a shared copy under `%LOCALAPPDATA%/RadbotProductions/TanukiMapPreview`; missing/outdated players install automatically, and a failed startup can trigger one repair. Keep internet access available for first installation or repair. Windows prerequisites may need installation on a fresh computer. No game source, Unreal installation, or .NET installation is needed for this self-contained preview helper.

The player uses Tanuki climbing/gliding/movement and Tengu movement. **Escape** opens the menu to switch character/free camera or exit. It contains the authored materials, collision, lights and selected base lighting. Native editor rendering can differ in exposure and shading.

## Steam Workshop

Open Steam and sign in with Tanuki Tag access. Opening the Tanuki panel attempts a Steam connection automatically. Open browses subscribed maps; Export creates or updates an item. **Refresh** updates the displayed list; it is not required for local map preview. Save the editor project after publishing to retain the Workshop item association. Opening another creator's map forks its identity.

## Troubleshooting and compatibility

- New exports use **schema 5**, including editable sun overrides. This preview player reads schemas 1–5. The in-game Workshop runtime also needs schema-5 support before it can play newly exported maps; updating an editor does not update the Steam game automatically.
- Exactly one map root must exist in the active scene. An ambiguous scene produces an error instead of choosing a random map.
- A running preview retains its old snapshot. Close it and launch again after edits, especially lighting changes.
- If the setup helper is missing, reinstall the complete editor kit, rather than GitHub's Source code ZIP. Check editor status and preview logs for download/startup failures.
- Save projects before replacing plugins. Keep user maps and materials outside plugin installation folders. Report the editor/plugin version and relevant log in an [issue](https://github.com/Radbot-Productions/TanukiMapEditors/issues).

See [material inputs](https://github.com/Radbot-Productions/TanukiMapEditors/blob/main/docs/PortableMapMaterials.md) and [light units](https://github.com/Radbot-Productions/TanukiMapEditors/blob/main/docs/PortableMapLights.md).
