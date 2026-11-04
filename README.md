# Tanuki Map Editors

Map authoring tools for Tanuki Tag. This public preview currently provides the
Blender addon. Unity, Godot and Unreal downloads will be added here separately.

## Install Blender

Download **Tanuki-Blender-0.1.0-preview.5.zip** from [Releases](https://github.com/Radbot-Productions/TanukiMapEditors/releases).
In Blender 4.5 LTS, open **Edit > Preferences > Add-ons > Install from Disk**,
choose the complete ZIP and enable **Tanuki Map Editor**. Open the viewport
sidebar with **N**, then select **Tanuki**.

See [Blender instructions](blender/README.md), [materials](docs/PortableMapMaterials.md)
and [lights](docs/PortableMapLights.md). For scene lights, use **Z > Rendered**,
or enable **Scene Lights** and **Scene World** in Material Preview's dropdown.

## Preview status

Create Room/Courtyard samples, inspect collision, author portable PBR materials
and Point/Spot/Directional lights, import/export `.tanukimap` files, and browse
or publish through Steam Workshop on Windows. Steam Workshop requires the Steam
client and Tanuki Tag access. Local authoring does not require Steam.

**Play in Tanuki** requires a separately distributed Tanuki gameplay-preview
player; the addon ZIP does not include the game, Unreal Engine or private game
source. Blender's native lighting is approximate, particularly exposure and
intensity. Final appearance must be checked in the Tanuki player.

Base-lighting presets, character scale references, Tengu preview movement and
the simplified preview menus are work in progress and are not in this release.

## Source layout

`blender/tanuki_maps` is the addon source; `blender/samples` contains native
material assets. `blender/WorkshopBridge` and `shared` contain the Windows Steam
helper. Build it with .NET 10 using `dotnet publish blender/WorkshopBridge -c Release
-r win-x64 --self-contained true -p:PublishSingleFile=true` and place its outputs
in `tanuki_maps/native` when assembling an addon ZIP.

Third-party bindings and their license/provenance are under `vendor/SteamworksNET`.
Tanuki names, artwork and original project files are copyright Radbot Productions.
Report bugs through this repository's Issues, including Blender version and an
export/validation error when available. Do not attach private maps without permission.

