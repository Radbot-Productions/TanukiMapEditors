"""Tanuki material inputs projected onto a portable Principled BSDF graph."""
import bpy
import hashlib
import json
from bpy.props import BoolProperty,EnumProperty,FloatProperty,FloatVectorProperty,PointerProperty

_updating=False


def changed(self,context):
    if self.enabled:apply(self.id_data)


class TanukiMaterial(bpy.types.PropertyGroup):
    enabled:BoolProperty(default=False)
    profile:EnumProperty(name='Surface',items=[('PBR','Opaque PBR',''),('Emissive','Emissive',''),('Cutout','Cutout',''),('Transparent','Transparent','')],update=changed)
    tint:FloatVectorProperty(name='Base Color',size=3,subtype='COLOR',min=0,max=1,default=(1,1,1),update=changed)
    base_texture:PointerProperty(name='Base Color Texture',type=bpy.types.Image,update=changed)
    roughness:FloatProperty(name='Roughness',min=0,max=1,default=.7,update=changed)
    roughness_texture:PointerProperty(name='Roughness Texture',type=bpy.types.Image,update=changed)
    metallic:FloatProperty(name='Metallic',min=0,max=1,default=0,update=changed)
    metallic_texture:PointerProperty(name='Metallic Texture',type=bpy.types.Image,update=changed)
    normal_texture:PointerProperty(name='Normal Texture',type=bpy.types.Image,update=changed)
    normal_strength:FloatProperty(name='Normal Strength',min=0,max=10,default=1,update=changed)
    ao_texture:PointerProperty(name='AO Texture',type=bpy.types.Image,update=changed)
    emission:FloatVectorProperty(name='Emission Color',size=3,subtype='COLOR',min=0,max=1,default=(1,1,1),update=changed)
    emission_texture:PointerProperty(name='Emission Texture',type=bpy.types.Image,update=changed)
    emission_strength:FloatProperty(name='Emission Strength',min=0,max=100000,default=1,update=changed)
    opacity:FloatProperty(name='Opacity',min=0,max=1,default=1,update=changed)
    cutoff:FloatProperty(name='Alpha Cutoff',min=0,max=1,default=.5,update=changed)
    uv_scale:FloatVectorProperty(name='UV Tiling',size=2,default=(1,1),update=changed)
    uv_offset:FloatVectorProperty(name='UV Offset',size=2,default=(0,0),update=changed)


def create(name='Tanuki PBR'):
    material=bpy.data.materials.new(name)
    material.tanuki_material.enabled=True
    apply(material)
    return material


def apply(material):
    global _updating
    if _updating:return
    _updating=True
    try:
        p=material.tanuki_material
        material.use_nodes=True;material.use_backface_culling=True
        material.diffuse_color=(*p.tint[:3],p.opacity if p.profile=='Transparent' else 1)
        material.surface_render_method='BLENDED' if p.profile=='Transparent' else 'DITHERED'
        nodes=material.node_tree.nodes;links=material.node_tree.links
        old_images={node.image for node in nodes if node.type=='TEX_IMAGE' and node.image and node.image.get('tanuki_projection')}
        nodes.clear()
        for value in old_images:
            if value.users==0:bpy.data.images.remove(value)
        output=nodes.new('ShaderNodeOutputMaterial');output.location=(700,0)
        bsdf=nodes.new('ShaderNodeBsdfPrincipled');bsdf.name='Tanuki PBR';bsdf.location=(380,0)
        links.new(bsdf.outputs['BSDF'],output.inputs['Surface'])
        bsdf.inputs['Base Color'].default_value=(*p.tint[:3],1)
        bsdf.inputs['Roughness'].default_value=p.roughness
        bsdf.inputs['Metallic'].default_value=p.metallic
        uv=nodes.new('ShaderNodeTexCoord');uv.location=(-900,0)
        mapping=nodes.new('ShaderNodeMapping');mapping.vector_type='POINT';mapping.location=(-700,0)
        mapping.inputs['Scale'].default_value=(*p.uv_scale,1)
        mapping.inputs['Location'].default_value=(*p.uv_offset,0)
        links.new(uv.outputs['UV'],mapping.inputs['Vector'])
        row=0
        def image(value,data=False):
            nonlocal row
            if not value:return None
            space='Non-Color' if data else 'sRGB'
            if value.colorspace_settings.name!=space:
                value=value.copy();value['tanuki_projection']=True;value.colorspace_settings.name=space
            node=nodes.new('ShaderNodeTexImage');node.image=value;node.location=(-450,300-row*250);row+=1
            links.new(mapping.outputs['Vector'],node.inputs['Vector'])
            return node
        def multiply_color(socket,texture,color):
            if not texture:return
            mix=nodes.new('ShaderNodeMixRGB');mix.blend_type='MULTIPLY';mix.inputs[0].default_value=1
            mix.inputs[2].default_value=(*color[:3],1)
            links.new(texture.outputs['Color'],mix.inputs[1]);links.new(mix.outputs['Color'],socket)
        base=image(p.base_texture)
        multiply_color(bsdf.inputs['Base Color'],base,p.tint)
        for texture,name in ((p.roughness_texture,'Roughness'),(p.metallic_texture,'Metallic')):
            node=image(texture,True)
            if node:
                split=nodes.new('ShaderNodeSeparateColor');links.new(node.outputs['Color'],split.inputs['Color'])
                links.new(split.outputs['Red'],bsdf.inputs[name])
        normal=image(p.normal_texture,True)
        if normal:
            node=nodes.new('ShaderNodeNormalMap');node.inputs['Strength'].default_value=p.normal_strength
            links.new(normal.outputs['Color'],node.inputs['Color']);links.new(node.outputs['Normal'],bsdf.inputs['Normal'])
        ao=image(p.ao_texture,True)
        if ao:
            from io_scene_gltf2.blender.com.material_helpers import get_gltf_node_name,create_settings_group
            group=bpy.data.node_groups.get(get_gltf_node_name()) or create_settings_group(get_gltf_node_name())
            node=nodes.new('ShaderNodeGroup');node.node_tree=group
            split=nodes.new('ShaderNodeSeparateColor');links.new(ao.outputs['Color'],split.inputs['Color'])
            links.new(split.outputs['Red'],node.inputs['Occlusion'])
        if p.profile=='Emissive':
            bsdf.inputs['Emission Color'].default_value=(*p.emission,1)
            bsdf.inputs['Emission Strength'].default_value=p.emission_strength
            multiply_color(bsdf.inputs['Emission Color'],image(p.emission_texture),p.emission)
        else:bsdf.inputs['Emission Strength'].default_value=0
        if p.profile in ('Cutout','Transparent'):
            if base:
                alpha=nodes.new('ShaderNodeMath');alpha.operation='MULTIPLY';alpha.inputs[1].default_value=p.opacity
                links.new(base.outputs['Alpha'],alpha.inputs[0]);source=alpha.outputs[0]
                if p.profile=='Cutout':
                    clip=nodes.new('ShaderNodeMath');clip.operation='GREATER_THAN';clip.inputs[1].default_value=p.cutoff
                    links.new(source,clip.inputs[0]);source=clip.outputs[0]
                links.new(source,bsdf.inputs['Alpha'])
            else:bsdf.inputs['Alpha'].default_value=(1 if p.opacity>=p.cutoff else 0) if p.profile=='Cutout' else p.opacity
        material['tanuki_graph_signature']=signature(material)
    finally:_updating=False


def signature(material):
    nodes=[]
    for node in material.node_tree.nodes:
        values=[]
        for socket in node.inputs:
            value=getattr(socket,'default_value',None)
            if value is not None:
                try:value=list(value)
                except TypeError:pass
            values.append((socket.identifier,value))
        nodes.append((node.name,node.bl_idname,getattr(node,'operation',''),getattr(node,'blend_type',''),
                      getattr(getattr(node,'image',None),'name',''),values))
    links=sorted((l.from_node.name,l.from_socket.identifier,l.to_node.name,l.to_socket.identifier) for l in material.node_tree.links)
    return hashlib.sha256(json.dumps((nodes,links),sort_keys=True,default=str).encode()).hexdigest()


def canonicalize(doc,materials):
    """Write explicit shared factors rather than relying on node-pattern defaults."""
    for record in doc.get('materials',[]):
        material=materials.get(record.get('name'))
        if not material or not material.tanuki_material.enabled:continue
        # Legacy recipes remain readable, but native node edits are authoritative.
        if material.get('tanuki_graph_signature')!=signature(material):continue
        p=material.tanuki_material
        pbr=record.setdefault('pbrMetallicRoughness',{})
        pbr['baseColorFactor']=[*p.tint,p.opacity if p.profile in ('Cutout','Transparent') else 1]
        pbr['roughnessFactor']=1 if p.roughness_texture else p.roughness
        pbr['metallicFactor']=1 if p.metallic_texture else p.metallic
        record['alphaMode']={'Cutout':'MASK','Transparent':'BLEND'}.get(p.profile,'OPAQUE')
        if p.profile=='Cutout':record['alphaCutoff']=p.cutoff
        else:record.pop('alphaCutoff',None)
        if p.normal_texture:record['normalTexture']['scale']=p.normal_strength
        if p.profile=='Emissive':
            record['emissiveFactor']=list(p.emission)
            record.setdefault('extensions',{})['KHR_materials_emissive_strength']={'emissiveStrength':p.emission_strength}
            doc['extensionsUsed']=list(set(doc.get('extensionsUsed',[]))|{'KHR_materials_emissive_strength'})
        record.setdefault('extras',{})['tanukiProfile']=p.profile


def draw(layout,context):
    obj=context.object
    if not obj or obj.type!='MESH':return
    layout.operator('tanuki.new_material',text='New Tanuki Material',icon='MATERIAL')
    material=obj.active_material
    if not material:return
    layout.prop(material,'name',text='Material')
    if not material.tanuki_material.enabled:
        layout.label(text='Existing native material. Create a Tanuki material to use shared controls.')
        return
    p=material.tanuki_material
    layout.operator('tanuki.rebuild_material',text='Rebuild Preview',icon='FILE_REFRESH')
    layout.prop(p,'profile')
    layout.prop(p,'tint')
    def texture(key):
        row=layout.row(align=True);row.prop(p,key)
        row.operator('tanuki.material_image',text='',icon='FILE_FOLDER').slot=key
    texture('base_texture')
    for scalar,slot in (('roughness','roughness_texture'),('metallic','metallic_texture')):
        row=layout.row();row.enabled=getattr(p,slot) is None;row.prop(p,scalar)
        texture(slot)
    texture('normal_texture');layout.prop(p,'normal_strength')
    texture('ao_texture')
    if p.profile=='Emissive':
        layout.prop(p,'emission');layout.prop(p,'emission_strength');texture('emission_texture')
    if p.profile in ('Cutout','Transparent'):layout.prop(p,'opacity')
    if p.profile=='Cutout':layout.prop(p,'cutoff')
    layout.prop(p,'uv_scale');layout.prop(p,'uv_offset')
    layout.label(text='Data textures use the red channel. Empty slots use scalar values.')


def register():
    bpy.utils.register_class(TanukiMaterial)
    bpy.types.Material.tanuki_material=PointerProperty(type=TanukiMaterial)


def unregister():
    del bpy.types.Material.tanuki_material
    bpy.utils.unregister_class(TanukiMaterial)
