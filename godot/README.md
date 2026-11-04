# Godot setup

Use **Godot 4.7.2 .NET on Windows x64** and **.NET SDK 10**. Install from [Godot](https://godotengine.org/download/windows/) and [Microsoft](https://dotnet.microsoft.com/download/dotnet/10.0). The non-.NET editor cannot load this C# addon.

1. Download **Tanuki-Godot-0.2.0-preview.3.zip** from the [editor release](https://github.com/Radbot-Productions/TanukiMapEditors/releases/tag/editors-v0.2.0-preview.3), and extract to a new writable folder.
2. In Project Manager, import the extracted `project.godot` and open it.
3. Let imports finish. If rebuilding is requested, use Godot's Build button. Enable Tanuki Map Editor under Project Settings → Plugins if needed.
4. Open **Project → Tools → Tanuki Map Editor** if the dock is hidden. Choose New → Empty Map, Room, or Courtyard, and save a `.tscn` scene.

Keep scenery under the TanukiMapRoot. Collision shapes may be authored separately from visible meshes. Keep one boundary and at least four starts. Components adds native Point/Spot/Directional map lights, Lighting Settings and Skybox. Edit the root's Base Lighting preset and Override Sun properties through the normal Inspector; the sky follows the selected profile. Generated reference characters and environment helpers are excluded from export.

Use the bundled Tanuki ShaderMaterial presets through Godot's standard material Inspector. The supported inputs include textures, PBR scalars and normal strength. Native rendering is approximate; use Play in Tanuki to check final appearance.

The ready project is the recommended route. Existing-project integration requires the whole addon, C# dependencies, unsafe-code settings and native helper files; copying plugin.cfg alone is insufficient. Keep your own maps/materials outside the addon folder when updating.

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
