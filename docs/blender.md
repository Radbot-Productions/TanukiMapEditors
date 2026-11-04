# Blender setup

Tested on Windows x64 with Blender **4.5.15 LTS**. Install [Blender 4.5 LTS](https://www.blender.org/download/lts/4-5/).

1. Download **Tanuki-Blender-0.2.0-preview.3.zip** from the [editor release](https://github.com/Radbot-Productions/TanukiMapEditors/releases/tag/editors-v0.2.0-preview.3). Keep it zipped; do not use GitHub's Source code ZIP.
2. Open **Edit → Preferences → Add-ons**. Click the small **▼ at the top right**, beside the search/filter controls, then **Install from Disk…**.
3. Select the ZIP, install, search for **Tanuki Map Editor**, and enable it.
4. Return to the 3D Viewport, press **N**, and open the **Tanuki** sidebar tab.
5. Expand **Map Files → New** and create an Empty Map, Room, or Courtyard sample.

All supported objects in the **active scene** export by default. Select an object and clear **Include in Export** to opt it out; generated lighting/sky/character helpers are excluded automatically. There is no Add Selection to Map step. Keep exactly one map root, one play boundary and at least four player starts. Save your editable project as a `.blend`; export produces a separate `.tanukimap`.

Use **Load Material Presets**, then normal Material Properties/Shader Editor inputs. Choose Scenery + Collision for visible blocking meshes, Collision Only for separate invisible colliders, or Scenery for nonblocking visuals. The collapsible Collision section displays collision through geometry or isolates it temporarily.

Components adds Point, Spot and Directional lights. For native Blender lights without Tanuki metadata, export reads native light settings. For Tanuki lights, use their portable fields in Selected Element. Point/Spot intensity is candela, Directional intensity is lux. Lighting Settings selects Cartoon Street Day/Night; **Override Preset Sun** exposes its rotation, color, lux and shadows. Skybox follows the preset. Use Rendered shading, or enable Scene Lights and Scene World in Material Preview, to see native lighting. Native exposure differs; check final appearance in Play in Tanuki.

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

See [material inputs](PortableMapMaterials.md) and [light units](PortableMapLights.md).
