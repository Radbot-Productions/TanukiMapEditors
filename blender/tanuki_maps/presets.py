"""Tanuki shader groups and material assets for Blender's native shader editor."""
import bpy
import hashlib,json

PROFILES=('PBR','Cutout','Transparent')


def signature(group):
    nodes=[]
    for node in group.nodes:
        values=[]
        for socket in node.inputs:
            value=getattr(socket,'default_value',None)
            try:value=list(value)
            except TypeError:pass
            values.append((socket.name,value))
        nodes.append((node.name,node.bl_idname,getattr(node,'operation',''),getattr(node,'space',''),values))
    links=sorted((l.from_node.name,l.from_socket.name,l.to_node.name,l.to_socket.name) for l in group.links)
    return hashlib.sha256(json.dumps((nodes,links),sort_keys=True,default=str).encode()).hexdigest()


def shader_group(profile):
    existing=next((g for g in bpy.data.node_groups if g.get('tanuki_shader')==profile),None)
    if existing:return existing
    group=bpy.data.node_groups.new('Tanuki '+profile,'ShaderNodeTree');group['tanuki_shader']=profile
    def socket(name,kind,default=None,minimum=None,maximum=None):
        value=group.interface.new_socket(name=name,in_out='INPUT',socket_type=kind)
        if default is not None:value.default_value=default
        if minimum is not None:value.min_value=minimum
        if maximum is not None:value.max_value=maximum
        return value
    socket('Base Color','NodeSocketColor',(1,1,1,1))
    socket('Roughness','NodeSocketFloat',.7,0,1);socket('Metallic','NodeSocketFloat',0,0,1)
    socket('Normal Texture','NodeSocketColor',(.5,.5,1,1));socket('Normal Strength','NodeSocketFloat',1,0,10)
    ao=socket('AO Texture','NodeSocketFloat',1,0,1);ao.hide_value=True
    if profile=='PBR':
        socket('Emission Color','NodeSocketColor',(0,0,0,1));socket('Emission Strength','NodeSocketFloat',1,0,1000000)
    if profile in ('Cutout','Transparent'):socket('Alpha','NodeSocketFloat',1,0,1)
    if profile=='Cutout':socket('Alpha Cutoff','NodeSocketFloat',.5,0,1)
    group.interface.new_socket(name='BSDF',in_out='OUTPUT',socket_type='NodeSocketShader')
    group.interface.new_socket(name='Occlusion',in_out='OUTPUT',socket_type='NodeSocketFloat')
    nodes=group.nodes;links=group.links;source=nodes.new('NodeGroupInput');output=nodes.new('NodeGroupOutput');bsdf=nodes.new('ShaderNodeBsdfPrincipled')
    source.location=(-500,0);bsdf.location=(0,0);output.location=(350,0)
    for key in ('Base Color','Roughness','Metallic','Emission Color','Emission Strength'):
        if key in source.outputs:links.new(source.outputs[key],bsdf.inputs[key])
    normal=nodes.new('ShaderNodeNormalMap');normal.location=(-250,-200)
    links.new(source.outputs['Normal Texture'],normal.inputs['Color']);links.new(source.outputs['Normal Strength'],normal.inputs['Strength']);links.new(normal.outputs['Normal'],bsdf.inputs['Normal'])
    if profile=='Cutout':
        clip=nodes.new('ShaderNodeMath');clip.operation='GREATER_THAN';clip.location=(-250,-400)
        links.new(source.outputs['Alpha'],clip.inputs[0]);links.new(source.outputs['Alpha Cutoff'],clip.inputs[1]);links.new(clip.outputs[0],bsdf.inputs['Alpha'])
    elif profile=='Transparent':links.new(source.outputs['Alpha'],bsdf.inputs['Alpha'])
    links.new(bsdf.outputs['BSDF'],output.inputs['BSDF']);links.new(source.outputs['AO Texture'],output.inputs['Occlusion'])
    group['tanuki_shader_signature']=signature(group)
    group.asset_mark()
    return group


def inputs(material):
    return next(n for n in material.node_tree.nodes if n.type=='GROUP' and n.node_tree.get('tanuki_shader')).inputs


def export_material(material):
    """Project current native group inputs into a disposable glTF-compatible graph."""
    if not any(n.type=='GROUP' and n.node_tree and n.node_tree.get('tanuki_shader') for n in material.node_tree.nodes):return material
    result=material.copy();result.use_fake_user=False;nodes=result.node_tree.nodes;links=result.node_tree.links
    shader=next(n for n in nodes if n.type=='GROUP' and n.node_tree and n.node_tree.get('tanuki_shader'));profile=shader.node_tree['tanuki_shader']
    bsdf=nodes.new('ShaderNodeBsdfPrincipled')
    def bind(source,target):
        if source.is_linked:links.new(source.links[0].from_socket,target)
        else:target.default_value=source.default_value
    for key in ('Base Color','Roughness','Metallic','Emission Color','Emission Strength'):
        if key in shader.inputs:bind(shader.inputs[key],bsdf.inputs[key])
    normal=nodes.new('ShaderNodeNormalMap');bind(shader.inputs['Normal Texture'],normal.inputs['Color']);bind(shader.inputs['Normal Strength'],normal.inputs['Strength']);links.new(normal.outputs['Normal'],bsdf.inputs['Normal'])
    if profile=='Cutout':
        clip=nodes.new('ShaderNodeMath');clip.operation='GREATER_THAN';bind(shader.inputs['Alpha'],clip.inputs[0]);bind(shader.inputs['Alpha Cutoff'],clip.inputs[1]);links.new(clip.outputs[0],bsdf.inputs['Alpha'])
    elif profile=='Transparent':bind(shader.inputs['Alpha'],bsdf.inputs['Alpha'])
    settings=next(n for n in nodes if n.type=='GROUP' and n.node_tree.name in ('glTF Material Output','glTF Settings'))
    bind(shader.inputs['AO Texture'],settings.inputs['Occlusion'])
    output=next(n for n in nodes if n.type=='OUTPUT_MATERIAL' and n.is_active_output);links.new(bsdf.outputs['BSDF'],output.inputs['Surface']);nodes.remove(shader)
    return result

def create(profile='PBR',name=None):
    emission=profile in ('Emissive','PBR Emission');profile='PBR' if emission else profile
    mat=bpy.data.materials.new(name or 'Tanuki '+('PBR Emission' if emission else profile))
    mat.use_nodes=True;mat.use_backface_culling=True;mat.use_fake_user=True
    mat['tanuki_native_preset']='PBR Emission' if emission else profile
    nodes=mat.node_tree.nodes;links=mat.node_tree.links;nodes.remove(nodes.get('Principled BSDF'))
    shader=nodes.new('ShaderNodeGroup');shader.node_tree=shader_group(profile);shader.name='Tanuki Shader';shader.label='Tanuki '+profile
    links.new(shader.outputs['BSDF'],nodes.get('Material Output').inputs['Surface'])
    from io_scene_gltf2.blender.com.material_helpers import get_gltf_node_name,create_settings_group
    settings=nodes.new('ShaderNodeGroup');settings.node_tree=bpy.data.node_groups.get(get_gltf_node_name()) or create_settings_group(get_gltf_node_name());settings.location=(0,-400)
    links.new(shader.outputs['Occlusion'],settings.inputs['Occlusion'])
    if emission:shader.inputs['Emission Color'].default_value=(1,1,1,1)
    if profile=='Transparent':
        mat.surface_render_method='BLENDED';shader.inputs['Alpha'].default_value=.4
    mat.asset_mark();return mat

def collection():
    result=[]
    for profile in (*PROFILES,'PBR Emission'):
        existing=next((m for m in bpy.data.materials if m.get('tanuki_native_preset')==profile),None)
        result.append(existing or create(profile))
    return result
