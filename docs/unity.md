# Unity setup

Tested on Windows x64 with **Unity 6000.6.7f1**, installed through [Unity Hub](https://unity.com/download). The ready project uses Built-in rendering; URP integration is optional.

1. Download **Tanuki-Unity-0.2.0-preview.3.zip** from the [editor release](https://github.com/Radbot-Productions/TanukiMapEditors/releases/tag/editors-v0.2.0-preview.3).
2. Extract to a new writable folder. In Unity Hub, add the folder containing Assets, Packages and ProjectSettings, and open it with the tested editor.
3. Wait for imports/compilation, then open **Tanuki → Map Editor**.
4. Choose **New → Create Empty Map**, Room, or Courtyard. Save the scene and assets normally.

For an existing project, keep the kit in a permanent folder. In **Window → Package Manager → Install package from disk**, select `Packages/com.radbot.tanuki-map-authoring/package.json` inside the kit. Do not select the project's manifest.json. Save/commit before replacing an existing plugin.

Keep scenery beneath the map root. Use Unity colliders for blocking geometry; colliders may be separate from visible meshes. Keep one boundary and at least four starts. The Components menu adds starts, boundaries, lights, Lighting Settings and Skybox. Lighting Settings selects Cartoon Street Day/Night. Enable Override Sun to edit rotation, color, lux and shadows through the normal Inspector. Additional lights remain independent. The generated character references show scale and are excluded from export.

Create materials through **Tanuki → Materials** and edit their Tanuki shaders with Unity's normal Material Inspector. The texture/scalar controls match portable inputs; arbitrary Standard/URP shader graphs are not equivalent. Do not directly edit generated environment helpers.

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
