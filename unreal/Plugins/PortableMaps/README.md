# Portable Maps

`PortableMaps` loads a bounded, single-file `.tanukimap` containing a manifest,
GLB geometry and embedded textures. Supported content is static scenery, triangle
or primitive collision and player starts. It does not execute source scripts or
instantiate classes supplied by a map file.

`PortableMapsEditor` imports those files into new, persistent Unreal assets and
exports an editor-only `APortableMapAuthoringRoot` with its attached supported
actors. The Tanuki **Tools > Portable Map Viewer** is the editor frontend; generic
console entrypoints are also available:

```text
PortableMaps.Authoring.Import "E:/Maps/MyMap.tanukimap" /Game/CustomMaps/MyNewFolder Fork
PortableMaps.Authoring.Export "E:/Maps/MyEditedMap.tanukimap"
```

Import requires a new unused asset folder. Fork creates a new map ID. Omitting
Fork retains the source ID. Only the Tanuki Workshop frontend associates an owned
map with a Workshop item; the file itself does not contain upload credentials or
an authoritative Workshop item ID.

Imported meshes, embedded textures and portable material instances are durable
assets. Edit/duplicate those material instances for supported base colour,
base-colour texture, metallic, roughness and alpha-cutout settings. Export rejects
arbitrary Unreal shader graphs. The glTF exporter supplies geometry; portable
material parameters and embedded texture data are serialized separately.

Optional authoring metadata version 1 preserves supported names, hierarchy and
local transforms; legacy files remain readable. Collider/start metadata may use
canonical map-space frames to preserve their footprints across differing affine
transform composition. This is not an archive of original prefabs, Blueprints,
scripts, procedural history or engine-specific shader graphs. Editor preview
illumination remains outside the export root and is excluded.

The shared Workshop client exposes upload, verified download and subscribed
catalogue queries. Consumers own operation cancellation and current-context
callbacks. Tanuki's session lifecycle and participant resource preparation live
in the game integration, not this format plugin.
