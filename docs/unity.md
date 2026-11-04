# Set up Unity

Tested on Windows x64 with **Unity 6000.6.7f1**. Install that editor through [Unity Hub](https://unity.com/download), completing Unity's normal license setup. The starter project uses the Built-in renderer; URP integration is optional.

## Recommended: ready authoring project

1. Download **Tanuki-Unity-…zip** from [Releases](https://github.com/Radbot-Productions/TanukiMapEditors/releases).
2. Extract it to a new writable folder. Do not extract over an existing Unity project.
3. In Unity Hub, choose **Add project from disk** and select the extracted folder containing `Assets`, `Packages`, and `ProjectSettings`.
4. Open it with Unity 6000.6.7f1. Let package resolution, imports and compilation finish.
5. Open **Tanuki → Map Editor** if the window did not open automatically.

## Existing Unity project

Back up or commit the project first. Keep the extracted kit in a permanent folder. Open **Window → Package Manager**, choose **Install package from disk**, and select `Packages/com.radbot.tanuki-map-authoring/package.json` inside the kit. Do not select the project's `Packages/manifest.json`.

See Unity's [local package installation documentation](https://docs.unity.com/en-us/engine/6000.0/manual/packages-list/managing-packages-window/upm-ui-actions/upm-ui-local). For URP, follow the optional integration's own README; installing the main package does not switch your project's render pipeline.

## First map and supported tools

1. Choose **Tanuki → Map Editor → New → Create Empty Map**, or start from Room/Courtyard.
2. Keep scenery beneath the map root. Add Unity colliders to blocking surfaces; invisible collider objects can be separate from visible meshes.
3. Use **Tanuki → Components** for Player Starts, the single Map Boundary, Point/Spot/Directional lights, **Lighting Settings**, and **Skybox**.
4. Select **Lighting Settings** to switch Cartoon Street Day/Night. Skybox follows the preset. Extra map lights remain. Tanuki/Tengu sample references show scale and stay out of map export.
5. Create materials through **Tanuki → Materials** and Unity's normal Material Inspector. Use the **Tanuki** shaders, with their supported texture/scalar parameters; native Standard/URP material compatibility does not expose all Tanuki-specific controls.
6. Save the Unity scene and its assets. Export requires exactly one boundary, at least four starts, and supported static geometry/materials.

These environment features are included in the upcoming 0.2 packages; older Unity packages are not feature-equivalent.

## Play and Workshop

Unity's ordinary **Play** button runs the local authoring helper. **Play in Tanuki** launches the separately provided shared gameplay-preview player. Use **Choose Tanuki Preview** to select its `TanukiMapPreview.exe`, keeping the entire player package intact. If you do not have that player package, local authoring and `.tanukimap` export still work.

Play in Tanuki exports current scene state automatically. Close and relaunch it after changes; it does not hot-reload the scene. The shared player checks actual game movement and lighting. Native editor previews can differ in tone mapping, attenuation and exposure.

Use **Map Editor → Open** for map files/subscribed Workshop maps and **Export** for local export or Workshop publishing. Workshop requires the Steam client, sign-in and Tanuki Tag Workshop access. Save the scene after publishing to preserve its item association.

If menus are absent, resolve all Console compile errors and verify the package is installed. If a light looks different in the game, edit its **Portable Map Light** settings, not only the generated native Light component. Save/commit your project before updating packages. Report the Unity version, renderer, plugin version and relevant Console/log output in an issue.
