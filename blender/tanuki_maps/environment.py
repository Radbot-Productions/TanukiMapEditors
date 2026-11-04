"""One base-lighting construct and non-exported character references per map."""
import json
import math
from pathlib import Path
import bpy
from mathutils import Matrix, Vector, Euler

DATA=Path(__file__).parent/'references'
PROFILES=(('cartoon-street-day','Cartoon Street Day','The game\'s Cartoon Street daytime lighting'),
          ('cartoon-street-night','Cartoon Street Night','The game\'s Cartoon Street nighttime lighting'))


def ensure(owner):
    from . import scene
    found=[o for o in bpy.context.scene.objects if o.tanuki.kind=='LIGHTING']
    if len(found)>1:raise ValueError('A map must have exactly one Base Lighting settings object.')
    if found:return found[0]
    obj=scene.empty('Base Lighting',owner,'LIGHTING')
    apply(obj)
    return obj


def skybox(owner):
    from . import scene
    settings=ensure(owner)
    return ensure_skybox(settings)


def ensure_skybox(settings):
    from . import scene
    found=[o for o in settings.children if o.tanuki.kind=='SKYBOX']
    if len(found)>1:raise ValueError('A map has only one Skybox component.')
    return found[0] if found else scene.empty('Skybox',settings,'SKYBOX')


def apply(settings):
    from . import scene
    data=json.loads((DATA/'lighting-profiles.json').read_text(encoding='utf-8-sig'))[settings.tanuki.base_lighting]
    if not settings.tanuki.override_sun:
        sun=data['suns'][0];settings['_tanuki_sun_sync']=True
        try:
            settings.tanuki.sun_rotation=(scene.read_frame(dict(position=[0,0,0],rotation=sun['quaternion']))@Matrix.Rotation(-math.pi/2,4,'X')).to_euler()
            settings.tanuki.sun_color=sun['color'];settings.tanuki.sun_intensity=sun['intensity'];settings.tanuki.sun_shadows=True
        finally:del settings['_tanuki_sun_sync']
    # Rebuild only children owned by this construct. Authored map lights are separate.
    ensure_skybox(settings)
    for child in list(settings.children_recursive):
        if child.tanuki.kind=='PREVIEW':bpy.data.objects.remove(child,do_unlink=True)
    for index,sun in enumerate(data['suns']):
        light=bpy.data.lights.new('Base Sun','SUN');light.energy=sun['intensity']/683
        light.color=sun['color'];obj=bpy.data.objects.new('Base Sun '+str(index),light)
        bpy.context.scene.collection.objects.link(obj);obj.parent=settings;obj.tanuki.kind='PREVIEW'
        obj.matrix_local=scene.read_frame(dict(position=[0,0,0],rotation=sun['quaternion']))@Matrix.Rotation(-math.pi/2,4,'X')
        if settings.tanuki.override_sun:
            light.energy=settings.tanuki.sun_intensity/683;light.color=settings.tanuki.sun_color;light.use_shadow=settings.tanuki.sun_shadows;obj.rotation_euler=settings.tanuki.sun_rotation
    world=bpy.data.worlds.new('Tanuki Base Lighting') if not bpy.context.scene.world else bpy.context.scene.world
    bpy.context.scene.world=world;world.use_nodes=True
    background=world.node_tree.nodes.get('Background')
    if background:
        nodes=world.node_tree.nodes;links=world.node_tree.links
        for node in list(nodes):
            if node.get('tanuki_base_sky'):nodes.remove(node)
        def node(kind):
            item=nodes.new(kind);item['tanuki_base_sky']=True;return item
        coordinates=node('ShaderNodeTexCoord');split=node('ShaderNodeSeparateXYZ');combine=node('ShaderNodeCombineXYZ')
        links.new(coordinates.outputs['Normal'],split.inputs['Vector'])
        for output,input_name in (('Y','X'),('X','Y')):
            negate=node('ShaderNodeMath');negate.operation='MULTIPLY';negate.inputs[1].default_value=-1
            links.new(split.outputs[output],negate.inputs[0]);links.new(negate.outputs[0],combine.inputs[input_name])
        links.new(split.outputs['Z'],combine.inputs['Z'])
        texture=node('ShaderNodeTexEnvironment');texture.image=bpy.data.images.load(str(DATA/(settings.tanuki.base_lighting+'.hdr')),check_existing=True)
        links.new(combine.outputs['Vector'],texture.inputs['Vector'])
        tint=node('ShaderNodeMixRGB');tint.blend_type='MULTIPLY';tint.inputs[0].default_value=1;tint.inputs[2].default_value=(*data.get('skyColor',[1,1,1]),1)
        links.new(texture.outputs['Color'],tint.inputs[1]);links.new(tint.outputs['Color'],background.inputs['Color'])
        background.inputs['Strength'].default_value=data.get('skyIntensity',0)/683
        camera=node('ShaderNodeBackground');camera.inputs['Strength'].default_value=1/683
        links.new(texture.outputs['Color'],camera.inputs['Color'])
        path=node('ShaderNodeLightPath');mix=node('ShaderNodeMixShader')
        links.new(path.outputs['Is Camera Ray'],mix.inputs[0]);links.new(background.outputs[0],mix.inputs[1]);links.new(camera.outputs[0],mix.inputs[2])
        output=next(n for n in nodes if n.type=='OUTPUT_WORLD');links.new(mix.outputs[0],output.inputs['Surface'])
    view=bpy.context.scene.view_settings
    view.view_transform='AgX'
    # Match the profile's fixed EV when authored; otherwise use a stable reference
    # exposure instead of allowing each native editor to auto-expose independently.
    ev=data.get('minEV100',0) if data.get('minEV100')==data.get('maxEV100') else data.get('referenceEV100',0)
    view.exposure=math.log2(683/1.2)-ev+data.get('exposureBias',0)
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type=='VIEW_3D':
                area.spaces.active.shading.use_scene_lights=True
                area.spaces.active.shading.use_scene_world=True

def export_sun(settings):
    from . import scene
    p=settings.tanuki
    if not p.override_sun:return None
    frame=Euler(p.sun_rotation).to_matrix().to_4x4()@Matrix.Rotation(math.pi/2,4,'X')
    rotation=scene.record_frame(frame)['rotation']
    return dict(rotation=rotation,color=list(p.sun_color),intensity=p.sun_intensity,shadows=p.sun_shadows)

def import_sun(settings,sun):
    from . import scene
    if sun is None:return
    settings['_tanuki_sun_sync']=True
    try:
        p=settings.tanuki;p.override_sun=True;p.sun_rotation=(scene.read_frame(dict(position=[0,0,0],rotation=sun['rotation']))@Matrix.Rotation(-math.pi/2,4,'X')).to_euler()
        p.sun_color=sun['color'];p.sun_intensity=sun['intensity'];p.sun_shadows=sun['shadows']
    finally:del settings['_tanuki_sun_sync']
    apply(settings)


def references(owner):
    from . import scene
    existing=next((o for o in owner.children if o.name.startswith('Character Scale References')),None)
    if existing and existing.get('tanuki_reference_version')==3:return existing
    if existing:
        for obj in list(existing.children_recursive):bpy.data.objects.remove(obj,do_unlink=True)
        bpy.data.objects.remove(existing,do_unlink=True)
    group=scene.empty('Character Scale References (not exported)',owner,'PREVIEW')
    group['tanuki_reference_version']=3
    bpy.context.view_layer.update();floor=None;area=0;inverse=owner.matrix_world.inverted()
    for obj in scene.export_objects(owner):
        if obj.type!='MESH':continue
        points=[inverse@obj.matrix_world@Vector(v) for v in obj.bound_box]
        low=Vector(tuple(min(p[i] for p in points) for i in range(3)));high=Vector(tuple(max(p[i] for p in points) for i in range(3)));size=high-low
        if size.z<min(size.x,size.y)*.3 and size.x*size.y>area:floor=(low,high);area=size.x*size.y
    low,high=floor if floor else (Vector((-10,-10,-.2)),Vector((10,10,0)))
    center=(low+high)*.5;spacing=min(1.5,(high.x-low.x)*.2);front=high.y-min(1.5,(high.y-low.y)*.15)
    for index,name in enumerate(('Tanuki','Tengu')):
        before=set(bpy.context.scene.objects)
        bpy.ops.import_scene.gltf(filepath=str(DATA/(name+'.glb')))
        imported=set(bpy.context.scene.objects)-before
        for obj in imported:
            obj.tanuki.kind='PREVIEW';obj.tanuki.usage='NONE'
            if obj.parent not in imported:
                obj.parent=group;obj.location+=Vector((center.x+(-spacing if index==0 else spacing),front,high.z))
        label=bpy.data.curves.new(name+' Scale Label','FONT');label.body=name+' (scale reference)';label.size=.2
        obj=bpy.data.objects.new(label.name,label);bpy.context.scene.collection.objects.link(obj)
        obj.parent=group;obj.tanuki.kind='PREVIEW';obj.location=(center.x+(-spacing if index==0 else spacing),front+.5,high.z+.02)
    return group
