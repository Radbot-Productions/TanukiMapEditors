# Changelog

## 0.1.0-preview.9 (unreleased)

- Open Tanuki Map Editor automatically on first launch in a new project.
- Include a ready-to-open Unity 6000 starter project in the authoring setup bundle.

## 0.1.0-preview.8 (unreleased)

- Make New Empty Map and both samples ready to play, with ground, four starts, boundary, lighting and the preview player; keep preview helpers out of exports.
- Keep Refresh beside the Workshop dropdown in both Open and Export.
- Run scene creation and file operations after the GUI event to avoid layout errors.
- Block export when scene geometry is outside the map root, naming the objects to move.
- Accept Unity's plain default material when its emission is black.
- Tune preview dimensions, speed, jump, acceleration and air control against Tanuki defaults; add held-Space glide with release and landing cleanup.
- Match Unreal ground-friction steering and braking so movement turns and stops promptly.
- Fix high-frame-rate movement stalls by allowing sub-millimeter CharacterController movement steps.
- Separate Back navigation into a small fixed footer instead of mixing it with map choices.
- Preserve existing Workshop item visibility when updating content.
- Replace decorative samples with plain eight-object blockouts using flat colors; preserve previously created scenes.

- Put New, Open and Export tabs in one Tanuki Map Editor; keep map settings on the scene root.
- Replace the current scene without a save prompt when choosing New/Open, and keep the root name and map name synchronized.
- Offer explicit Workshop update/create choices.
- Add a Tanuki-sized third-person climbing proxy and switchable free camera for Unity playtesting.
- Open subscribed Workshop maps without entering IDs; preserve your own publishing link and fork other creators' maps automatically.
- Create Box/Cylinder boundaries automatically and require exactly one boundary and at least four player starts at export.
- Validate automatically, remember file destinations, and default Workshop titles and previews.
- Write playable-map manifest version 2 so older runtimes reject maps whose boundaries they cannot enforce. Continue opening legacy files for repair.

## 0.1.0-preview.7

- Open a local `.tanukimap` as an editable scene with persistent mesh, material and texture assets.
- Browse subscribed Workshop maps and download them directly into Unity. Continue your own item or fork a new map identity.
- Preserve supported object names, hierarchy, transforms, colliders and player starts through optional authoring metadata; older exports still open.
- Retain other saved scenes and reject overwriting an existing import folder. Save an untitled scene before importing.
- Keep Workshop operation recovery working when a diagnostic receipt cannot be written.

## 0.1.0-preview.6

- Publish directly through the logged-in Steam client inside Unity Editor; removed game executable/project path setup.
- Display the connected Steam account and expose reconnect/pending readiness.
- Recover uploads across script reloads, retain created item IDs before submission, and keep preview/receipt files outside Workshop map content.
- Include pinned, editor-only Steamworks.NET bindings and the matching Windows redistributable; reject conflicting Steam integrations.

## 0.1.0-preview.5

- Start Workshop helper jobs in the neutral entry world for both source and packaged game executables, keeping rendered splash/lobby UI out of the graphics-free uploader.

## 0.1.0-preview.4

- Added Export & Upload to Workshop and Export & Update Workshop Item to the authoring window.
- Uploads export the current scene and use Tanuki's Steam backend through an immutable file job.
- Remember Workshop item IDs per map, track pending/results across editor reloads, and retain job logs for interrupted submissions.
- Requires Windows and a Tanuki build with the editor-job uploader; current testing uses the developer-only Workshop.
- Leaving the preview image empty generates a framed map thumbnail in an isolated preview scene.

## 0.1.0-preview.3

- Export now uses a file-save dialog and writes only the chosen `.tanukimap` file.
- JSON, GLB, textures, collision and player starts are embedded in that single file; normal exports no longer write loose inspection copies.
- Failed replacement preserves the previous valid file and supports corrected retries.

## 0.1.0-preview.2

- Added a stylized courtyard sample with timber shops, terracotta roofs, shoji windows, faceted trees, an entry gate and courtyard props.
- Made Open Sample Map open the courtyard. The original room remains available as the basic export regression fixture.
- Added Unity preview lighting/camera outside the export root and a rendered sample preview helper.

## 0.1.0-preview.1

- Added the Tanuki Maps editor panel for choosing/creating map roots, placing player starts, adding box collision, validation and export.
- Added Undo support for map/player-start creation and collider authoring.
- Added the sample room and PlayerStart prefab generator.
- Added static GLB geometry, embedded base-color textures, primitive/triangle collision and player-start export into one `.tanukimap` file.
- Made validation and export share the same conversion path.

This is an authoring prototype. Portable gameplay
scripting, embedded Tanuki gameplay preview, and Unreal/Godot authoring frontends
are planned separately. This package has not been submitted to OpenUPM.
