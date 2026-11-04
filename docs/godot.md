# Set up Godot

Use **Godot 4.7.2 .NET on Windows x64** and **.NET SDK 10**. The standard non-.NET Godot download cannot load this C# addon. Install from [Godot's official downloads](https://godotengine.org/download/windows/) and [Microsoft's .NET SDK downloads](https://dotnet.microsoft.com/download/dotnet/10.0).

1. Download **Tanuki-Godot-…zip** from [Releases](https://github.com/Radbot-Productions/TanukiMapEditors/releases).
2. Extract it to a new writable folder.
3. In Godot's Project Manager, choose **Import**, select the extracted `project.godot`, and open it with the .NET editor.
4. Let imports finish and build the C# project if prompted. The kit includes the addon, native Steam helper/bindings, shaders, materials, references and sample maps.
5. Open **Project → Project Settings → Plugins** and enable **Tanuki Map Editor** if it is not already enabled.
6. Use the **Tanuki Map Editor** dock's **New**, **Open**, and **Export** tabs.

Godot documents [plugin installation](https://docs.godotengine.org/en/stable/tutorials/plugins/editor/installing_plugins.html) and [C# prerequisites/building](https://docs.godotengine.org/en/stable/tutorials/scripting/c_sharp/c_sharp_basics.html). Rebuild C# after changing tool scripts or exported properties.

## First map

Create Empty Map or Room/Courtyard. Edit nodes with the normal Scene tree, Inspector and 3D viewport. Keep one Play Boundary, at least four Player Starts, supported static meshes and collision shapes. Visual meshes do not automatically imply blocking collision.

**Components → Lighting Settings** selects the map root's Cartoon Street Day/Night preset. **Skybox** shares that selection. Add Point/Spot/Directional lights from Components and edit their portable Inspector fields. Scale-reference characters and environment helpers are excluded from export.

Use the native **Tanuki PBR**, **Tanuki Cutout** and **Tanuki Transparent** ShaderMaterials under `addons/tanuki_maps/materials`. Duplicate resources into your map's own folder and edit them in the normal Inspector; textures are optional where scalar inputs are provided.

These environment features are included in the upcoming 0.2 packages. Save your editable map as a Godot scene; `.tanukimap` is its portable export.

## Preview and Workshop

Godot's native Play runs the local authoring helper. **Play in Tanuki** uses the separate gameplay-preview package; choose its `TanukiMapPreview.exe` with **Choose Tanuki Preview**. Keep the whole player directory together. Each launch exports a fresh snapshot, including scene edits; close/relaunch after changes. The player is not included in the plugin kit.

Workshop requires Steam running and signed in, Tanuki Tag access and current Workshop permission. Use Open to browse subscribed maps, and Export to create/update an item. Save the Godot scene after publishing to keep the item association.

Missing dock: verify the .NET editor/SDK, rebuild and enable the plugin. C# load errors: include the build error, Godot/.NET/plugin versions in an issue. Do not overwrite an existing project's addon, project settings or maps when updating; save/commit first. The ready project is the recommended installation route. Advanced existing-project integration also needs the kit's C# project dependencies, unsafe-code settings and native Steam files; copying only `plugin.cfg` is insufficient.
