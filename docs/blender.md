# Set up Blender

Tested on Windows x64 with **Blender 4.5.15 LTS**. Install Blender from [blender.org](https://www.blender.org/download/lts/4-5/).

## Install the Tanuki plugin

1. Download the **Tanuki-Blender-…zip** asset from [Tanuki Map Editors releases](https://github.com/Radbot-Productions/TanukiMapEditors/releases). Choose the Blender asset, not GitHub's automatic **Source code** ZIP.
2. Leave the plugin ZIP compressed.
3. Open Blender and choose **Edit → Preferences → Add-ons**.
4. Click the small **▼ menu at the top right**, beside the search/filter controls. Choose **Install from Disk…**. It is inside this menu, not a large button in the main panel.
5. Select the plugin ZIP and confirm installation.
6. Search for **Tanuki Map Editor** and enable its checkbox. Save Preferences if Blender does not save them automatically.
7. Return to the 3D Viewport. Press **N** to show the sidebar, then click the **Tanuki** tab on its right edge.

Blender's [official add-on installation guide](https://docs.blender.org/manual/en/4.5/editors/preferences/addons.html) describes the same installation path.

## Make a first map

1. Expand **Map Files → New**. Choose **Room Sample**, **Courtyard Sample**, or **Create Empty Map**.
2. Move and resize scenery with Blender's normal tools. Parent new scenery beneath the map root using **Add Selection to Map**.
3. Keep exactly one **Play Boundary** and at least four **Player Starts**. New sample boundaries extend below the floor so their bottom face does not overlap the walking surface.
4. Save your editable project as a **.blend** file. The portable **.tanukimap** is the game/export format, not a replacement for your Blender project.

## Materials, collision and lighting

- Use **Load Material Presets**, then normal Material Properties and the Shader Editor. Duplicate **Tanuki PBR**, **Tanuki Cutout**, or **Tanuki Transparent**. Their supported texture and scalar inputs export to the game.
- In **Selected Element**, choose **Scenery + Collision** for visible blocking geometry, or **Collision Only** for a separate invisible collider mesh. Visible scenery alone does not automatically collide.
- Expand **Collision** to show collision edges through geometry or temporarily inspect collision only.
- Add **Point Light**, **Spot Light**, or **Directional Light** from **Components**. Select a light and edit its Tanuki fields; those portable fields determine game export.
- **Components → Lighting Settings** selects **Cartoon Street Day** or **Cartoon Street Night**. **Skybox** follows the same profile. Extra authored lights remain when switching presets.
- To see scene lighting inside Blender, use **Z → Rendered**. Material Preview needs **Scene Lights** and **Scene World** enabled in its shading dropdown. Blender's rendering is approximate; check final lighting in Play in Tanuki.

These environment controls are part of the upcoming 0.2 editor packages. The older Blender 0.1 download has materials, individual lights and collision tools, but does not include the new preset/sky/reference setup.

## Play in Tanuki

The editor plugin and the **Tanuki gameplay-preview player** are separate downloads. The plugin ZIP does not contain Unreal Engine, the game, or its source. If you have not been provided a compatible gameplay-preview package, local authoring and export still work.

1. Extract the complete gameplay-preview package. Keep its **TanukiMapPreview.exe**, `preview.json`, `Tanuki`, and `Engine` files/folders together. Do not copy out only the executable.
2. Open **Edit → Preferences → Add-ons**, expand **Tanuki Map Editor**, and set **Tanuki Preview Executable** to that package's `TanukiMapPreview.exe`.
3. Click **Play in Tanuki** in the sidebar.

Each click exports the current scene to a temporary snapshot, including unsaved edits. **You do not need to save or manually export first.** An already-open preview does not update live: close it and click Play in Tanuki again after editing.

Escape opens the preview menu for returning to movement, free camera, switching Tanuki/Tengu, restarting at a spawn, settings, and exiting. The shared player supplies production movement; Blender's viewport is for authoring.

## Steam Workshop

1. Start the Steam desktop client and sign in to the account that will publish the map.
2. That account needs access to **Tanuki Tag** and its currently restricted Workshop. Installing the plugin does not grant access.
3. In **Map Files → Open**, refresh/browse subscribed Workshop maps or open a `.tanukimap` file.
4. For publishing, use **Map Files → Export → Validate Map**, then **Publish / Update Workshop**. Set the map name and optional preview image here.
5. Save the `.blend` after publishing so its Workshop item association is retained for updates.

Opening another creator's map forks its identity. Do not create a second item if an upload was interrupted; inspect the existing job/item before retrying.

## Troubleshooting and updates

- **No Install from Disk:** use the top-right ▼ menu on Preferences → Add-ons.
- **No Tanuki tab:** put the mouse over the 3D Viewport, press N, then select Tanuki. This sidebar does not belong to the Preferences window.
- **Missing native Workshop helper:** reinstall the complete release ZIP. A GitHub source ZIP is not the packaged plugin.
- **Old player/map-format error:** update the complete gameplay-preview package and choose its executable again.
- **Unexpected lighting:** confirm the map's Lighting Settings, then close and relaunch the preview. An older player window retains its old snapshot.
- **Failed launch:** use **Open Preview Log** when offered. Include that log and your Blender/plugin versions in an [issue](https://github.com/Radbot-Productions/TanukiMapEditors/issues).
- Before updating the plugin, save `.blend` projects and close Blender. Install the new plugin ZIP, enable it, and reopen your project. Keep your own materials and maps outside the installed plugin folder.
