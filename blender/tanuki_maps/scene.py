"""Blender scene operations. Export staging never mutates the authored objects."""
import math
from pathlib import Path
import tempfile
import uuid
import bpy
from mathutils import Matrix, Quaternion, Vector
from . import format as fmt

# Blender (X right, -Y forward, Z up) -> Unreal centimetres.
C = Matrix(((0, -1, 0), (-1, 0, 0), (0, 0, 1)))
U = Matrix(((-1,0,0),(0,0,1),(0,-1,0)))


def root(context):
    obj = context.object
    while obj:
        if obj.tanuki.kind == 'ROOT':
            return obj
        obj = obj.parent
    roots = [o for o in context.scene.objects if o.tanuki.kind == 'ROOT']
    fmt.require(len(roots) == 1, "Select one Tanuki Map Root.")
    return roots[0]


def descendants(owner):
    return list(owner.children_recursive)


def record_frame(matrix):
    p = C @ matrix.translation * 100
    q = (C @ matrix.to_quaternion().to_matrix() @ C).to_quaternion()
    return {'position': list(p), 'rotation': [q.x, q.y, q.z, q.w]}


def read_frame(record):
    p = C @ Vector(record['position']) * .01
    x, y, z, w = record['rotation']
    return Matrix.LocRotScale(p, (C @ Quaternion((w, x, y, z)).to_matrix() @ C).to_quaternion(), Vector((1, 1, 1)))


def empty(name, owner=None, kind='NONE'):
    obj = bpy.data.objects.new(name, None)
    bpy.context.scene.collection.objects.link(obj)
    obj.parent = owner
    obj.tanuki.kind = kind
    obj.empty_display_type = 'ARROWS' if kind == 'START' else 'CUBE'
    obj.empty_display_size = .5
    return obj


def material(name, color):
    from . import presets
    mat = presets.create(name=name)
    mat.diffuse_color = (*color, 1)
    mat.use_nodes = True
    mat.use_backface_culling = True
    presets.inputs(mat)['Base Color'].default_value = (*color, 1)
    presets.inputs(mat)['Roughness'].default_value = .7
    return mat


def cube(name, owner, location, size, mat):
    # Data API keeps scene construction independent of active selection/edit mode.
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata([(x*.5, y*.5, z*.5) for x, y, z in
                      [(-1,-1,-1),(-1,-1,1),(-1,1,-1),(-1,1,1),(1,-1,-1),(1,-1,1),(1,1,-1),(1,1,1)]], [],
                     [(2,6,4,0),(5,7,3,1),(4,5,1,0),(3,7,6,2),(1,3,2,0),(6,7,5,4)])
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    obj.parent = owner
    obj.location, obj.scale = location, size
    obj.data.materials.append(mat)
    obj.tanuki.usage = 'BOTH'
    return obj


def new_map(sample='EMPTY'):
    # A separate scene preserves unsaved work in existing scenes.
    scene = bpy.data.scenes.new('Tanuki ' + sample.title())
    bpy.context.window.scene = scene
    scene.unit_settings.system = 'METRIC'
    owner = empty('New Map' if sample == 'EMPTY' else sample.title(), kind='ROOT')
    owner.tanuki.map_id = uuid.uuid4().hex
    floor = material('Tanuki Floor', (.32, .38, .44))
    block = material('Tanuki Block', (.5, .26, .12))
    cube('Floor', owner, (0, 0, -.1), (20, 20, .2), floor)
    boundary = empty('Play Boundary', owner, 'BOUNDARY')
    boundary.location, boundary.scale = (0, 0, 4), (20, 20, 8)
    for i in range(4):
        start = empty('Player Start', owner, 'START')
        start.location = ((i-1.5)*2, 3, 1)
    if sample != 'EMPTY':
        for i in range(3):
            cube('Block', owner, (-4+i*4, -2, .5+i*.3), (2, 2, 1+i*.6), block)
        ramp = cube('Ramp', owner, (5, -5, .6), (2, 4, .3), block)
        ramp.rotation_euler.x = math.radians(20)
        if sample == 'ROOM':
            for p, s in [((0,-9,2),(18,.3,4)),((-9,0,2),(.3,18,4)),((9,0,2),(.3,18,4))]:
                cube('Wall', owner, p, s, floor)
        else:
            cube('Platform', owner, (-5,-6,.25), (5,3,.5), floor)
    light = bpy.data.lights.new('Preview Sun', 'SUN')
    light.energy = 2
    sun = bpy.data.objects.new('Preview Sun', light)
    scene.collection.objects.link(sun)
    sun.rotation_euler = (.5, -.4, -.5)
    sun.tanuki.kind = 'PREVIEW'
    scene.world = bpy.data.worlds.new('Tanuki Preview World')
    scene.world.color = (.15, .15, .15)
    bpy.context.view_layer.objects.active = owner
    owner.select_set(True)
    return owner


def add_light(owner, kind):
    data = bpy.data.lights.new('Map ' + kind.title(), {'point':'POINT','spot':'SPOT','directional':'SUN'}[kind])
    obj = bpy.data.objects.new(data.name, data)
    bpy.context.scene.collection.objects.link(obj)
    obj.parent = owner
    obj.location.z = 3
    obj.tanuki.kind = 'LIGHT'
    sync_light(obj)
    return obj


def sync_light(obj):
    if obj.type != 'LIGHT' or obj.tanuki.kind != 'LIGHT':
        return
    p, data = obj.tanuki, obj.data
    data.color = p.color
    # Blender uses radiant watts; portable intensity is photometric cd / lux.
    # glTF's standard 683 lm/W conversion is used for the local approximation.
    data.energy = p.intensity / 683 if data.type == 'SUN' else p.intensity * 4*math.pi / 683
    data.use_shadow = p.shadows
    if data.type != 'SUN':
        data.use_custom_distance = True
        data.cutoff_distance = p.range_m
    if data.type == 'SPOT':
        data.spot_size = math.radians(p.outer_cone*2)
        data.spot_blend = 1 - p.inner_cone / max(.001, p.outer_cone)


def validate_material(mat, name):
    if mat is None:
        return
    fmt.require(mat.use_nodes, name + ': use a Principled BSDF material.')
    canonical=hasattr(mat,'tanuki_material') and mat.tanuki_material.enabled
    fmt.require(mat.use_backface_culling, name + ': enable Backface Culling for portable materials.')
    nodes=list(mat.node_tree.nodes)
    for node in list(nodes):
        if node.type=='GROUP' and node.node_tree and node.node_tree.get('tanuki_shader'):
            from . import presets
            fmt.require(node.node_tree.get('tanuki_shader_signature')==presets.signature(node.node_tree),name+': use Tanuki shader inputs; bake changes to the shader definition.')
            nodes.extend(node.node_tree.nodes)
    allowed = {'BSDF_PRINCIPLED','OUTPUT_MATERIAL','TEX_IMAGE','NORMAL_MAP','SEPRGB','SEPXYZ','SEPARATE_COLOR','UVMAP','TEX_COORD','MAPPING','REROUTE','GROUP'}
    allowed|={'MIX_RGB','MATH','GROUP_INPUT','GROUP_OUTPUT'}
    fmt.require(all(n.type in allowed for n in nodes), name + ': bake procedural or layered materials to portable PBR textures.')
    fmt.require(all(n.type!='MATH' or n.operation in ('MULTIPLY','GREATER_THAN') for n in nodes),name+': bake unsupported material math to textures.')
    fmt.require(all(n.type!='MIX_RGB' or n.blend_type=='MULTIPLY' for n in nodes),name+': use a simple multiply for portable color tint.')
    bsdf = [n for n in nodes if n.type == 'BSDF_PRINCIPLED']
    fmt.require(len(bsdf) == 1, name + ': use one Principled BSDF.')
    for key in ('Transmission Weight', 'Coat Weight', 'Sheen Weight', 'Anisotropic IOR Level', 'Subsurface Weight'):
        socket = bsdf[0].inputs.get(key)
        fmt.require(socket is None or (not socket.is_linked and socket.default_value == 0), name + ': unsupported ' + key)
    for node in nodes:
        if node.type == 'TEX_IMAGE':
            fmt.require(node.image and node.image.size[0] <= 8192 and node.image.size[1] <= 8192,
                        name + ': textures must be 2D images up to 8192 pixels.')
        if node.type == 'GROUP':
            fmt.require(node.node_tree and (node.node_tree.get('tanuki_shader') in ('PBR','Cutout','Transparent') or
                        (node.node_tree.name in ('glTF Material Output', 'glTF Settings') and not any(link.from_node==node for link in mat.node_tree.links))), name + ': unsupported shader group.')


def export_map(owner, path):
    bpy.context.view_layer.update()
    fmt.require(owner.tanuki.map_id and len(owner.tanuki.map_id) <= 128, 'Set a Map ID.')
    fmt.require(all(abs(v-1) < .00001 for v in owner.matrix_world.to_scale()), 'Map root must have unit scale.')
    objects = []
    def visit(parent):
        for obj in parent.children:
            if obj.tanuki.kind != 'PREVIEW':
                objects.append(obj);visit(obj)
    visit(owner)
    outside=[o.name for o in bpy.context.scene.objects if o.type=='MESH' and o not in objects and o.tanuki.kind!='PREVIEW']
    fmt.require(not outside,'Move scenery under the Map Root with Add Selection to Map: '+', '.join(outside[:5]))
    fmt.require(not any(o.tanuki.kind == 'ROOT' for o in objects), 'Nested map roots are unsupported.')
    boundaries = [o for o in objects if o.tanuki.kind == 'BOUNDARY']
    starts = [o for o in objects if o.tanuki.kind == 'START']
    fmt.require(len(boundaries) == 1 and len(starts) >= 4, 'A map needs exactly one boundary and at least four player starts.')
    for obj in objects:
        fmt.require(obj.type in ('MESH','EMPTY','LIGHT'), obj.name + ': bake curves, text, armatures and other content to static meshes.')
        fmt.require(not obj.animation_data and not obj.constraints and not obj.rigid_body, obj.name + ': bake animated, constrained or dynamic content first.')
        if obj.type == 'LIGHT':
            fmt.require(obj.tanuki.kind == 'LIGHT' and obj.data.type in ('POINT','SPOT','SUN'), obj.name + ': use a portable Point, Spot or Directional light.')
    inv = owner.matrix_world.inverted()
    b = boundaries[0]
    bt = inv @ b.matrix_world
    size = bt.to_scale()
    fmt.require(min(size) >= .01 and bt.determinant() > 0 and
                all(abs(bt.to_3x3().col[i].normalized().dot(bt.to_3x3().col[j].normalized())) < .0001
                    for i,j in ((0,1),(1,2),(0,2))), 'Boundary needs positive dimensions without shear.')
    fmt.require(b.tanuki.boundary_shape != 'cylinder' or abs(size.x-size.y) < .001, 'Cylinder boundary X/Y dimensions must match.')
    boundary = dict(id='boundary', shape=b.tanuki.boundary_shape, size=[size.y*100,size.x*100,size.z*100], **record_frame(bt))
    markers = [dict(id='start-'+str(i), type='playerStart', spawnTag=o.tanuki.spawn_tag, **record_frame(inv @ o.matrix_world)) for i,o in enumerate(starts)]
    author_nodes=[]
    indices={}
    bindings=[]
    for obj in [owner]+objects:
        indices[obj]=len(author_nodes)
        local=Matrix.Identity(4) if obj==owner else obj.parent.matrix_world.inverted() @ obj.matrix_world
        position,rotation,s=local.decompose()
        p=U @ position
        q=(U @ rotation.to_matrix() @ U.inverted()).to_quaternion()
        author_nodes.append(dict(name=obj.name,parent=-1 if obj==owner else indices[obj.parent],
            position=dict(x=p.x,y=p.y,z=p.z),rotation=dict(x=q.x,y=q.y,z=q.z,w=q.w),scale=dict(x=s.x,y=s.z,z=s.y)))
    def bind(identity,obj): bindings.append(dict(id=identity,node=indices[obj]))
    bind('boundary',b)
    for marker,obj in zip(markers,starts):bind(marker['id'],obj)
    lights = []
    for obj in objects:
        if obj.tanuki.kind != 'LIGHT':
            continue
        p = obj.tanuki
        # Native Blender lights shine along -Z; portable lights shine along +X.
        frame = inv @ obj.matrix_world @ Matrix.Rotation(math.pi/2, 4, 'X')
        lights.append(dict(id='light-'+str(len(lights)),type={'POINT':'point','SPOT':'spot','SUN':'directional'}[obj.data.type],
                           color=list(p.color), intensity=p.intensity, range=p.range_m*100, innerCone=p.inner_cone,
                           outerCone=p.outer_cone, shadows=p.shadows, **record_frame(frame)))
        # Lights have a different native optical axis; metadata must store the
        # canonical +forward frame used by every other editor.
        frame=inv @ obj.matrix_world @ Matrix.Rotation(math.pi/2,4,'X')
        p=U@frame.translation;q=(U@frame.to_quaternion().to_matrix()@U.inverted()).to_quaternion()
        node=len(author_nodes)
        author_nodes.append(dict(name=obj.name+' Frame',parent=0,position=dict(x=p.x,y=p.y,z=p.z),rotation=dict(x=q.x,y=q.y,z=q.z,w=q.w),scale=dict(x=1,y=1,z=1)))
        bindings.append(dict(id=lights[-1]['id'],node=node))
    staging = bpy.data.scenes.new('_TanukiExport')
    meshes = []
    copies = []
    export_materials={};export_names={}
    selected = list(bpy.context.selected_objects)
    active = bpy.context.view_layer.objects.active
    source_scene = bpy.context.window.scene
    try:
        deps = bpy.context.evaluated_depsgraph_get()
        for obj in objects:
            if obj.type != 'MESH' or obj.tanuki.usage == 'NONE':
                continue
            if obj.tanuki.usage != 'COLLISION':
                fmt.require(len(obj.data.materials)>0 and all(m is not None for m in obj.data.materials),obj.name+': assign a portable material with New PBR Material.')
                for mat in obj.data.materials:
                    validate_material(mat, obj.name)
            fmt.require(not obj.data.shape_keys, obj.name + ': bake shape keys before export.')
            mesh = bpy.data.meshes.new_from_object(obj.evaluated_get(deps), depsgraph=deps)
            if obj.tanuki.usage!='COLLISION':
                from . import presets
                for index,shader in enumerate(mesh.materials):
                    if not shader:continue
                    source=shader.original
                    if source not in export_materials:
                        export_materials[source]=presets.export_material(source)
                        export_names[export_materials[source].name]=source.name
                    mesh.materials[index]=export_materials[source]
            frame=inv @ obj.matrix_world
            mesh.transform(frame)
            if frame.determinant()<0:mesh.flip_normals()
            mesh.update()
            copy = bpy.data.objects.new('entry-'+str(len(copies)), mesh)
            staging.collection.objects.link(copy)
            copies.append(copy)
            # Remove materials from invisible collision meshes.
            if obj.tanuki.usage == 'COLLISION':
                mesh.materials.clear()
            meshes.append({'id':copy.name, 'visible':obj.tanuki.usage != 'COLLISION', 'collision':obj.tanuki.usage in ('BOTH','COLLISION')})
            bind(copy.name,obj)
        fmt.require(any(e['visible'] for e in meshes), 'Add static scenery before exporting.')
        fmt.require(len(meshes) <= 10000 and len(markers) <= 10000, 'Map exceeds the element limit.')
        bpy.context.window.scene = staging
        with tempfile.TemporaryDirectory(prefix='tanuki-export-') as tmp:
            glb_path = str(Path(tmp)/'scene.glb')
            result = bpy.ops.export_scene.gltf(filepath=glb_path, export_format='GLB', export_animations=False,
                      export_cameras=False, export_lights=False, export_apply=False, export_yup=True,
                      export_image_format='AUTO', export_materials='EXPORT', export_extras=False,
                      export_tangents=False, export_attributes=False, export_vertex_color='NONE',use_active_scene=True)
            fmt.require('FINISHED' in result, 'Blender GLB export failed.')
            data = Path(glb_path).read_bytes()
        doc, binary = fmt.glb_read(data)
        for record in doc.get('materials',[]):record['name']=export_names.get(record.get('name'),record.get('name'))
        from .materials import canonicalize
        canonicalize(doc,{mat.name:mat for obj in objects if obj.type=='MESH' for mat in obj.data.materials if mat})
        # Runtime consumes baked mesh arrays, not node transforms. Staging vertices
        # are already in map space; every node must therefore remain identity.
        for entry in meshes:
            node = next(n for n in doc['nodes'] if n.get('name') == entry['id'])
            fmt.require(not any(k in node for k in ('matrix','translation','rotation','scale')), 'Unexpected non-identity staging transform.')
            entry['mesh'] = node['mesh']
        fmt.write(path, dict(schemaVersion=3 if lights else 2, mapId=owner.tanuki.map_id,
                  meshes=meshes,colliders=[],markers=markers,boundary=boundary,lights=lights,
                  authoring=dict(version=1,nodes=author_nodes,bindings=bindings)), fmt.glb_write(doc,binary))
        owner.tanuki.export_path = str(Path(path).resolve())
    finally:
        bpy.context.window.scene = source_scene
        for copy in copies:
            mesh = copy.data
            bpy.data.objects.remove(copy, do_unlink=True)
            bpy.data.meshes.remove(mesh)
        bpy.data.scenes.remove(staging)
        for source,material in export_materials.items():
            if material!=source:bpy.data.materials.remove(material)
        for obj in selected:
            if obj.name in bpy.context.view_layer.objects:
                obj.select_set(True)
        bpy.context.view_layer.objects.active = active


def import_map(path, fork=False):
    manifest, data = fmt.read(path)
    doc, binary = fmt.glb_read(data)
    # Only instantiate the meshes explicitly admitted by the manifest. Ignore
    # source cameras, animations, lights and arbitrary glTF extras.
    prefix=uuid.uuid4().hex[:12]+'-'
    doc['nodes'] = [{'name':prefix+e['id'], 'mesh':e['mesh']} for e in manifest['meshes']]
    doc['scenes'] = [{'nodes':list(range(len(doc['nodes'])))}]
    doc['scene'] = 0
    for field in ('animations','skins','cameras','extensions'):
        doc.pop(field, None)
    previous = bpy.context.window.scene
    scene = bpy.data.scenes.new(Path(path).stem)
    bpy.context.window.scene = scene
    try:
        owner = empty(Path(path).stem, kind='ROOT')
        owner.tanuki.map_id = uuid.uuid4().hex if fork else manifest['mapId']
        nodes=[owner]
        bindings={}
        authored=manifest.get('authoring')
        if authored:
            fmt.require(authored.get('version')==1 and 0<len(authored.get('nodes',[]))<=30000,'Unsupported authoring metadata.')
            nodes=[]
            for i,n in enumerate(authored['nodes']):
                parent=n['parent']
                fmt.require((i==0 and parent==-1) or (i>0 and 0<=parent<i),'Invalid authoring hierarchy.')
                obj=owner if i==0 else empty(n['name'],nodes[parent])
                owner.name=n['name'] if i==0 else owner.name
                p=n['position'];r=n['rotation'];s=n['scale']
                position=U.inverted()@Vector((p['x'],p['y'],p['z']))
                rotation=(U.inverted()@Quaternion((r['w'],r['x'],r['y'],r['z'])).to_matrix()@U).to_quaternion()
                obj.matrix_local=Matrix.LocRotScale(position,rotation,Vector((s['x'],s['z'],s['y']))) if i else Matrix.Identity(4)
                nodes.append(obj)
            for binding in authored.get('bindings',[]):
                fmt.require(binding['id'] not in bindings and 0<=binding['node']<len(nodes),'Invalid authoring binding.')
                bindings[binding['id']]=nodes[binding['node']]
        bpy.context.view_layer.update()
        def attach(obj,identity):
            world=obj.matrix_world.copy()
            parent=bindings.get(identity,owner)
            if obj.type=='MESH':
                # glTF vertices are baked in map space. Put them back into the
                # authored owner frame instead of asking a Blender TRS object
                # to represent an inverse affine transform containing shear.
                if obj.data.users>1:obj.data=obj.data.copy()
                frame=parent.matrix_world.inverted()@world
                obj.data.transform(frame)
                if frame.determinant()<0:obj.data.flip_normals()
                obj.parent=parent
                obj.matrix_parent_inverse=Matrix.Identity(4)
                obj.matrix_basis=Matrix.Identity(4)
            else:
                obj.parent=parent
                obj.matrix_world=world
        with tempfile.TemporaryDirectory(prefix='tanuki-import-') as tmp:
            target = Path(tmp)/'scene.glb'
            target.write_bytes(fmt.glb_write(doc,binary))
            fmt.require('FINISHED' in bpy.ops.import_scene.gltf(filepath=str(target), import_pack_images=True), 'Blender GLB import failed.')
        for entry in manifest['meshes']:
            obj = next(o for o in scene.objects if o.name == prefix+entry['id'])
            obj.name=entry['id']
            attach(obj,entry['id'])
            obj.tanuki.usage = 'BOTH' if entry['visible'] and entry['collision'] else 'SCENERY' if entry['visible'] else 'COLLISION'
            obj.display_type = 'WIRE' if obj.tanuki.usage == 'COLLISION' else 'TEXTURED'
        for c in manifest['colliders']:
            # Convert primitives to explicit editable collision meshes.
            if c['type'] == 'box':
                size = C @ Vector(c['size']) * .01
                obj = cube(c['id'], owner, (0,0,0), tuple(abs(v) for v in size), material('Collision',(.1,.7,.2)))
            else:
                bpy.ops.mesh.primitive_uv_sphere_add(segments=24, ring_count=16, radius=c['radius']*.01)
                obj = bpy.context.object
                obj.parent = owner
                if c['type'] == 'capsule':
                    offset = (c['halfHeight']-c['radius'])*.01
                    for vertex in obj.data.vertices:
                        vertex.co.z += offset if vertex.co.z > 0 else -offset
            local = Matrix.Diagonal((*obj.scale,1))
            obj.matrix_world = read_frame(c) @ local
            attach(obj,c['id'])
            obj.tanuki.usage, obj.display_type = 'COLLISION','WIRE'
        for marker in manifest['markers']:
            obj = empty(marker['id'], owner, 'START')
            obj.matrix_world = read_frame(marker)
            attach(obj,marker['id'])
            obj.tanuki.spawn_tag = marker['spawnTag']
        b = manifest.get('boundary')
        if b:
            obj = empty('Play Boundary', owner, 'BOUNDARY')
            obj.tanuki.boundary_shape = b['shape']
            size = b['size']
            obj.matrix_world = read_frame(b) @ Matrix.Diagonal((size[1]*.01,size[0]*.01,size[2]*.01,1))
            attach(obj,b['id'])
        else:
            obj = empty('Play Boundary', owner, 'BOUNDARY')
            obj.location,obj.scale = (0,0,4),(20,20,8)
        for light in manifest.get('lights',[]):
            obj = add_light(owner,light['type'])
            obj.matrix_world = read_frame(light) @ Matrix.Rotation(-math.pi/2,4,'X')
            attach(obj,light['id'])
            p = obj.tanuki
            p.color,p.intensity,p.range_m,p.inner_cone,p.outer_cone,p.shadows = (light['color'],light['intensity'],light['range']*.01,light['innerCone'],light['outerCone'],light['shadows'])
            sync_light(obj)
        bpy.context.view_layer.objects.active = owner
        owner.select_set(True)
        return owner
    except Exception:
        bpy.context.window.scene = previous
        for obj in list(scene.objects):
            bpy.data.objects.remove(obj,do_unlink=True)
        bpy.data.scenes.remove(scene)
        raise
