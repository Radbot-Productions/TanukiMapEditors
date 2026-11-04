# Map editors 0.2.0-preview.3

Windows authoring kits for Blender 4.5.15 LTS, Unity 6000.6.7f1, Godot 4.7.2 .NET / SDK 10, and Unreal 5.8.1 (BuildId 55116800).

- Play in Tanuki automatically discovers or installs the separate shared preview player. No EXE picker or Workshop refresh is required. Each launch snapshots current scene edits.
- Day/night lighting presets, linked skybox, and editable sun rotation/color/intensity/shadows across all four editors.
- Native Tanuki material workflows, authored lights, separate collision geometry, and corrected character scale references.
- Blender exports supported active-scene content by default, with an Include in Export opt-out.
- Steam connection is attempted when opening the authoring panel.

New exports use schema 5. The preview player supports schemas 1–5; the Steam Workshop game runtime must also support 5 before it can play new exports. Native editor rendering can differ from the game.

## Install

Download your editor ZIP below, rather than the Source code ZIP. Follow the [setup guide for your editor](https://github.com/Radbot-Productions/TanukiMapEditors/blob/main/README.md). The first Play in Tanuki downloads about 4.34GB from the [separate player release](https://github.com/Radbot-Productions/TanukiMapEditors/releases/tag/player-v0.2.0-preview.3).

## Validation

72 Blender export/UI fixtures; Unity/Godot environment and sun round trips; automatic player launch from Blender/Unity/Godot and the extracted Unreal kit; Unreal reference-placement regression; 13 installer checks; anonymous public player download/install with checksums. Workshop publishing still requires Steam sign-in and Tanuki Tag access; these checks do not qualify every account or an in-game Steam deployment.

Checksums and sizes are in release-manifest.json. Save projects before replacing plugins.
