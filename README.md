# Tanuki Map Editors

Author Tanuki Tag maps in **Blender, Unity, Godot, or Unreal**. Each kit includes its plugin, native material presets, environment/sky controls, collision tools, scale references and Steam Workshop frontend.

| Editor | Tested Windows editor | Download | Setup |
|---|---|---|---|
| Blender | 4.5.15 LTS | [Blender kit](https://github.com/Radbot-Productions/TanukiMapEditors/releases/download/editors-v0.2.0-preview.3/Tanuki-Blender-0.2.0-preview.3.zip) | [Blender instructions](docs/blender.md) |
| Unity | 6000.6.7f1 | [Unity kit](https://github.com/Radbot-Productions/TanukiMapEditors/releases/download/editors-v0.2.0-preview.3/Tanuki-Unity-0.2.0-preview.3.zip) | [Unity instructions](docs/unity.md) |
| Godot | 4.7.2 .NET / .NET SDK 10 | [Godot kit](https://github.com/Radbot-Productions/TanukiMapEditors/releases/download/editors-v0.2.0-preview.3/Tanuki-Godot-0.2.0-preview.3.zip) | [Godot instructions](docs/godot.md) |
| Unreal | 5.8.1 / BuildId 55116800 | [Unreal kit](https://github.com/Radbot-Productions/TanukiMapEditors/releases/download/editors-v0.2.0-preview.3/Tanuki-Unreal-0.2.0-preview.3.zip) | [Unreal instructions](docs/unreal.md) |

**Play in Tanuki installs its separate Windows player automatically.** You do not choose an EXE. First use downloads about 4.34 GB from the [preview-player release](https://github.com/Radbot-Productions/TanukiMapEditors/releases/tag/player-v0.2.0-preview.3). It uses the game's Tanuki/Tengu movement, materials, collision and lighting; Escape opens character/free-camera/exit controls. Each launch snapshots current scene edits, without a manual export or Workshop refresh.

## Authoring features

- Native Tanuki PBR, Cutout and Transparent materials, with textures or scalar values, normal strength, AO and emission. Use each editor's normal material workflow.
- Visible collision or separate invisible collision geometry, plus editor visualization.
- Point, Spot and Directional lights, Cartoon Street Day/Night base lighting, editable preset sun, and linked skybox.
- Room/Courtyard samples, boundaries below the floor, four starts, and static Tanuki/Tengu scale references.
- Steam connection on panel open; subscribed map import and Workshop create/update. Steam access is needed for Workshop, not local authoring or preview.
- Blender exports supported active-scene objects by default, with an Include in Export opt-out.

New exports use **schema 5**. The preview player reads 1–5; the Steam game's Workshop runtime must also be updated before it can play schema-5 exports. Native editor lighting is approximate; check the result with Play in Tanuki.

## Documentation and source

Start with the setup link above. See [materials](docs/PortableMapMaterials.md), [lights](docs/PortableMapLights.md), and [release notes](https://github.com/Radbot-Productions/TanukiMapEditors/releases/tag/editors-v0.2.0-preview.3).

Source is under blender, unity, godot, unreal, shared and vendor. Packaged kits include compiled Windows helpers; GitHub's automatic Source code ZIP is not an installable editor kit. Original Tanuki names/artwork/project files are copyright Radbot Productions; third-party and package licenses remain with their respective files. The preview release contains cooked runtime binaries/assets and no private game source.
