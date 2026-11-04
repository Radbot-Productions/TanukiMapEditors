bl_info = {'name':'Tanuki Map Editor','author':'Radbot Productions','version':(0,1,0),
           'blender':(4,5,0),'location':'3D View > Sidebar > Tanuki','category':'Import-Export',
           'description':'Create, open, inspect collision, light and export portable Tanuki maps'}

import math
from pathlib import Path
import subprocess
import tempfile
import uuid
import bpy
from bpy.props import BoolProperty, EnumProperty, FloatProperty, FloatVectorProperty, PointerProperty, StringProperty
from bpy_extras.io_utils import ImportHelper, ExportHelper
from mathutils import Vector
from . import scene

_draw = None
_hidden = {}
_preview_processes = []


def update_light(self, context):
    if isinstance(self.id_data, bpy.types.Object):
        scene.sync_light(self.id_data)


def workshop_items(self,context):
    from . import workshop
    return workshop.items()


def workshop_selected(self,context):
    if self.tanuki_workshop_choice!='NONE':self.tanuki_workshop_item=self.tanuki_workshop_choice


class TanukiObject(bpy.types.PropertyGroup):
    kind: EnumProperty(items=[(v,v.title(),'') for v in ('NONE','ROOT','BOUNDARY','START','LIGHT','PREVIEW')],default='NONE')
    usage: EnumProperty(name='Export As',items=[('SCENERY','Scenery','Visible without collision'),('BOTH','Scenery + Collision','Visible and blocking'),('COLLISION','Collision Only','Invisible blocking mesh'),('NONE','Exclude','Not exported')],default='SCENERY')
    map_id: StringProperty(name='Map ID')
    workshop_id: StringProperty(name='Workshop Item',description='Retained even if a publishing job fails after item creation')
    export_path: StringProperty(name='Map File',subtype='FILE_PATH')
    preview_png: StringProperty(name='Preview PNG',subtype='FILE_PATH')
    spawn_tag: StringProperty(name='Spawn Tag',maxlen=128)
    boundary_shape: EnumProperty(name='Shape',items=[('box','Box',''),('cylinder','Cylinder','')])
    color: FloatVectorProperty(name='Color',subtype='COLOR',size=3,min=0,max=1,default=(1,1,1),update=update_light)
    intensity: FloatProperty(name='Intensity (cd / lux)',min=0,max=10000000,default=1000,update=update_light)
    range_m: FloatProperty(name='Range (m)',min=.01,max=100000,default=10,update=update_light)
    inner_cone: FloatProperty(name='Inner Half Angle',min=0,max=89,default=20,update=update_light)
    outer_cone: FloatProperty(name='Outer Half Angle',min=.01,max=89,default=35,update=update_light)
    shadows: BoolProperty(name='Cast Shadows',default=True,update=update_light)


def redraw():
    for window in bpy.context.window_manager.windows:
        for area in window.screen.areas:
            if area.type == 'VIEW_3D':
                area.tag_redraw()


def restore_collision_view():
    for obj,(hidden,layer) in list(_hidden.items()):
        try:obj.hide_set(hidden,view_layer=layer)
        except (ReferenceError,RuntimeError):pass
    _hidden.clear()


def draw_overlay():
    import gpu
    from gpu_extras.batch import batch_for_shader
    context = bpy.context
    try:
        owner = scene.root(context)
    except ValueError:
        return
    lines = []
    bounds = []
    starts = []
    deps = context.evaluated_depsgraph_get()
    for obj in scene.descendants(owner):
        if obj.tanuki.kind=='START':
            center=obj.matrix_world.translation
            rotation=obj.matrix_world.to_quaternion()
            forward=rotation@Vector((0,-1,0));right=rotation@Vector((1,0,0))
            tip=center+forward
            starts.extend((center,tip,tip,tip-forward*.25+right*.15,tip,tip-forward*.25-right*.15))
        if context.scene.tanuki_collision_overlay and obj.type == 'MESH' and obj.tanuki.usage in ('BOTH','COLLISION'):
            evaluated = obj.evaluated_get(deps)
            mesh = evaluated.to_mesh()
            try:
                points = [obj.matrix_world @ v.co for v in mesh.vertices]
                lines.extend(points[i] for edge in mesh.edges for i in edge.vertices)
            finally:
                evaluated.to_mesh_clear()
        if obj.tanuki.kind == 'BOUNDARY':
            if obj.tanuki.boundary_shape == 'box':
                points = [obj.matrix_world @ Vector((x*.5,y*.5,z*.5)) for x,y,z in [(-1,-1,-1),(-1,-1,1),(-1,1,-1),(-1,1,1),(1,-1,-1),(1,-1,1),(1,1,-1),(1,1,1)]]
                for i in range(8):
                    for bit in (1,2,4):
                        j = i ^ bit
                        if i < j:
                            bounds.extend((points[i],points[j]))
            else:
                for i in range(48):
                    a,b = i*math.tau/48,(i+1)*math.tau/48
                    for z in (-.5,.5):
                        bounds.extend(obj.matrix_world @ Vector(v) for v in ((math.cos(a)*.5,math.sin(a)*.5,z),(math.cos(b)*.5,math.sin(b)*.5,z)))
                    if i%12 == 0:
                        bounds.extend(obj.matrix_world @ Vector((math.cos(a)*.5,math.sin(a)*.5,z)) for z in (-.5,.5))
    shader = gpu.shader.from_builtin('UNIFORM_COLOR')
    gpu.state.depth_test_set('NONE')
    gpu.state.blend_set('ALPHA')
    gpu.state.line_width_set(1.5)
    try:
        for points,color in ((lines,(.15,1,.25,.75)),(bounds,(.15,.5,1,.8)),(starts,(.1,1,1,.9))):
            if points:
                shader.bind()
                shader.uniform_float('color',color)
                batch_for_shader(shader,'LINES',{'pos':points}).draw(shader)
    finally:
        gpu.state.depth_test_set('NONE')
        gpu.state.blend_set('NONE')
        gpu.state.line_width_set(1)


class TANUKI_OT_new(bpy.types.Operator):
    """Create a separate editable map scene from an empty, room, or courtyard sample"""
    bl_idname='tanuki.new_map'
    bl_label='Create Map'
    bl_options={'REGISTER','UNDO'}
    sample: EnumProperty(items=[(v,v.title(),'') for v in ('EMPTY','ROOM','COURTYARD')])
    def execute(self, context):
        restore_collision_view()
        scene.new_map(self.sample)
        redraw()
        return {'FINISHED'}


class TANUKI_OT_element(bpy.types.Operator):
    """Add a boundary, player start, or portable light to the selected map"""
    bl_idname='tanuki.add_element'
    bl_label='Add Element'
    bl_options={'REGISTER','UNDO'}
    element: StringProperty()
    def execute(self, context):
        try:
            owner=scene.root(context)
            existing=next((o for o in scene.descendants(owner) if o.tanuki.kind==self.element),None) if self.element=='BOUNDARY' else None
            if existing:
                obj=existing
                self.report({'INFO'},'Selected the existing map boundary')
            elif self.element in ('point','spot','directional'):
                obj=scene.add_light(owner,self.element)
            else:
                obj=scene.empty('Player Start' if self.element=='START' else 'Play Boundary',owner,self.element)
                if self.element=='BOUNDARY':
                    obj.location,obj.scale=(0,0,4),(20,20,8)
                else:
                    obj.location=context.scene.cursor.location
            bpy.ops.object.select_all(action='DESELECT')
            obj.select_set(True)
            context.view_layer.objects.active=obj
            redraw()
            return {'FINISHED'}
        except Exception as error:
            self.report({'ERROR'},str(error));return {'CANCELLED'}


class TANUKI_OT_add_selection(bpy.types.Operator):
    """Include the selected objects in this map for validation and export"""
    bl_idname='tanuki.add_selection'
    bl_label='Add Selection to Map'
    bl_options={'REGISTER','UNDO'}
    def execute(self,context):
        try:
            owner=scene.root(context)
            selected=set(context.selected_objects)-{owner}
            for obj in selected:
                if obj.parent in selected or obj in scene.descendants(owner):continue
                scene.fmt.require(obj.tanuki.kind!='ROOT','Select scenery rather than another Map Root.')
                world=obj.matrix_world.copy();obj.parent=owner;obj.matrix_world=world
            return {'FINISHED'}
        except Exception as error:self.report({'ERROR'},str(error));return {'CANCELLED'}


class TANUKI_OT_material(bpy.types.Operator):
    """Create a Tanuki PBR material with portable texture and scalar inputs"""
    bl_idname='tanuki.new_material'
    bl_label='New PBR Material'
    bl_options={'REGISTER','UNDO'}
    def execute(self,context):
        meshes=[obj for obj in context.selected_objects if obj.type=='MESH']
        if not meshes:self.report({'ERROR'},'Select one or more meshes.');return {'CANCELLED'}
        from . import presets
        mat=presets.create()
        for obj in meshes:
            if not obj.data.materials:obj.data.materials.append(mat)
            else:obj.active_material=mat
        return {'FINISHED'}


class TANUKI_OT_material_presets(bpy.types.Operator):
    """Load the native Tanuki material examples for use in Blender's material editor"""
    bl_idname='tanuki.material_presets'
    bl_label='Load Material Presets'
    bl_options={'REGISTER','UNDO'}
    def execute(self,context):
        from . import presets
        presets.collection()
        self.report({'INFO'},'PBR, Emissive, Cutout and Transparent materials are available in the native material selector.')
        return {'FINISHED'}


class TANUKI_OT_collision(bpy.types.Operator):
    """Show only blocking map geometry, or restore the previous viewport visibility"""
    bl_idname='tanuki.collision_view'
    bl_label='Collision Only / Restore View'
    def execute(self, context):
        global _hidden
        if _hidden:
            restore_collision_view()
        else:
            try: owner=scene.root(context)
            except ValueError as error: self.report({'ERROR'},str(error));return {'CANCELLED'}
            for obj in scene.descendants(owner):
                if obj.type=='MESH':
                    _hidden[obj]=(obj.hide_get(),context.view_layer)
                    obj.hide_set(obj.tanuki.usage not in ('BOTH','COLLISION'))
                    if obj.tanuki.usage in ('BOTH','COLLISION'):
                        obj.select_set(True)
            context.scene.tanuki_collision_overlay=True
        redraw()
        return {'FINISHED'}


class TANUKI_OT_open(bpy.types.Operator,ImportHelper):
    """Import a portable Tanuki map into a separate editable scene"""
    bl_idname='tanuki.open_map'
    bl_label='Open Map File'
    filename_ext='.tanukimap'
    filter_glob:StringProperty(default='*.tanukimap',options={'HIDDEN'})
    fork:BoolProperty(name='Create New Map Identity',default=False)
    def execute(self,context):
        try: scene.import_map(self.filepath,self.fork);restore_collision_view();return {'FINISHED'}
        except Exception as error: self.report({'ERROR'},str(error));return {'CANCELLED'}


class TANUKI_OT_export(bpy.types.Operator,ExportHelper):
    """Validate and save this map as a portable .tanukimap file"""
    bl_idname='tanuki.export_map'
    bl_label='Export Map File'
    filename_ext='.tanukimap'
    filter_glob:StringProperty(default='*.tanukimap',options={'HIDDEN'})
    def execute(self,context):
        try: scene.export_map(scene.root(context),self.filepath);self.report({'INFO'},'Map exported.');return {'FINISHED'}
        except Exception as error: self.report({'ERROR'},str(error));return {'CANCELLED'}


class TANUKI_OT_validate(bpy.types.Operator):
    """Check geometry, materials, lights, boundary and player starts before export"""
    bl_idname='tanuki.validate_map'
    bl_label='Validate Map'
    def execute(self,context):
        try:
            owner=scene.root(context)
            remembered=owner.tanuki.export_path
            try:
                with tempfile.TemporaryDirectory(prefix='tanuki-validation-') as tmp:
                    scene.export_map(owner,str(Path(tmp)/'map.tanukimap'))
            finally: owner.tanuki.export_path=remembered
            self.report({'INFO'},'Map is ready to export.');return {'FINISHED'}
        except Exception as error: self.report({'ERROR'},str(error));return {'CANCELLED'}


class TanukiPreferences(bpy.types.AddonPreferences):
    bl_idname=__package__
    preview_executable:StringProperty(name='Tanuki Preview Executable',subtype='FILE_PATH')
    def draw(self,context):
        self.layout.prop(self,'preview_executable')


class TANUKI_OT_play(bpy.types.Operator):
    """Export a temporary snapshot and test it with production Tanuki movement"""
    bl_idname='tanuki.play_map'
    bl_label='Play in Tanuki'
    def execute(self,context):
        job=None
        process=None
        try:
            prefs=context.preferences.addons[__package__].preferences
            exe=Path(bpy.path.abspath(prefs.preview_executable)) if prefs.preview_executable else Path(__file__).parent/'preview/TanukiMapPreview.exe'
            scene.fmt.require(exe.is_file() and exe.name=='TanukiMapPreview.exe','Choose TanukiMapPreview.exe from the gameplay preview package in Add-on Preferences.')
            native=exe.parent/'Tanuki/Binaries/Win64'/exe.name
            if native.is_file():exe=native
            job=Path(bpy.utils.user_resource('CONFIG',path='tanuki_maps/preview',create=True))/uuid.uuid4().hex
            job.mkdir()
            target=job/'map.tanukimap'
            owner=scene.root(context)
            remembered=owner.tanuki.export_path
            try: scene.export_map(owner,str(target))
            finally: owner.tanuki.export_path=remembered
            process=subprocess.Popen([str(exe),'-CustomConfig=MapPreview','-TanukiPreviewMap='+str(target),'-abslog='+str(job/'preview.log'),'-windowed','-ResX=1280','-ResY=720'],cwd=str(exe.parent))
            _preview_processes.append((process,job))
            def cleanup():
                if process.poll() is None: return 1
                log=job/'preview.log'
                if hasattr(context.window_manager,'tanuki_status') and (process.returncode or not log.exists() or 'TANUKI_PREVIEW_READY' not in log.read_text(errors='replace')):
                    context.window_manager.tanuki_status='Preview failed. Check the configured executable and runtime compatibility.'
                target.unlink(missing_ok=True)
                log.unlink(missing_ok=True)
                job.rmdir()
                _preview_processes.remove((process,job))
                return None
            bpy.app.timers.register(cleanup,first_interval=1)
            return {'FINISHED'}
        except Exception as error:
            if job is not None and process is None:
                (job/'map.tanukimap').unlink(missing_ok=True)
                (job/'preview.log').unlink(missing_ok=True)
                job.rmdir()
            self.report({'ERROR'},str(error));return {'CANCELLED'}


class TANUKI_OT_workshop(bpy.types.Operator):
    """Open the Tanuki Workshop browser and publishing tools"""
    bl_idname='tanuki.workshop'
    bl_label='Workshop'
    action:StringProperty(default='refresh')
    def execute(self,context):
        try:
            from . import workshop
            workshop.start(context,self.action)
            return {'FINISHED'}
        except Exception as error: self.report({'ERROR'},str(error));return {'CANCELLED'}


class TANUKI_PT_editor(bpy.types.Panel):
    bl_label='Tanuki Map Editor'
    bl_idname='TANUKI_PT_editor'
    bl_space_type='VIEW_3D'
    bl_region_type='UI'
    bl_category='Tanuki'
    def draw(self,context):
        layout=self.layout
        layout.prop(context.scene,'tanuki_tab',expand=True)
        tab=context.scene.tanuki_tab
        if tab=='NEW':
            for kind,label in (('EMPTY','Create Empty Map'),('ROOM','Room Sample'),('COURTYARD','Courtyard Sample')):
                layout.operator('tanuki.new_map',text=label).sample=kind
        elif tab=='OPEN':
            layout.operator('tanuki.open_map',icon='FILE_FOLDER')
            layout.operator('tanuki.workshop',text='Refresh Workshop Maps').action='refresh'
            layout.prop(context.scene,'tanuki_workshop_choice',text='Workshop Map')
            layout.prop(context.scene,'tanuki_workshop_item')
            layout.operator('tanuki.workshop',text='Open Workshop Map').action='download'
        else:
            layout.operator('tanuki.validate_map',icon='CHECKMARK')
            layout.operator('tanuki.export_map',icon='EXPORT')
            layout.operator('tanuki.workshop',text='Publish / Update Workshop').action='publish'
        message=context.window_manager.tanuki_status
        if message:
            box=layout.box()
            for line in message.splitlines(): box.label(text=line[:120])
        try: owner=scene.root(context)
        except ValueError: return
        layout.separator()
        layout.prop(owner,'name',text='Name')
        layout.prop(owner.tanuki,'map_id')
        layout.prop(owner.tanuki,'workshop_id')
        layout.prop(owner.tanuki,'preview_png')
        layout.operator('tanuki.play_map',icon='PLAY')
        box=layout.box()
        box.label(text='Elements')
        box.operator('tanuki.add_selection',icon='OUTLINER_OB_GROUP_INSTANCE')
        for kind,label in (('START','Player Start'),('BOUNDARY','Play Boundary'),('point','Point Light'),('spot','Spot Light'),('directional','Directional Light')):
            box.operator('tanuki.add_element',text=label).element=kind
        box=layout.box()
        box.label(text='Collision')
        box.prop(context.scene,'tanuki_collision_overlay',text='Show Collision Through Geometry')
        box.operator('tanuki.collision_view')
        obj=context.object
        if obj and obj in scene.descendants(owner):
            if obj.type=='MESH':
                box.prop(obj.tanuki,'usage')
                box.operator('tanuki.new_material',icon='MATERIAL')
                box.operator('tanuki.material_presets',icon='ASSET_MANAGER')
            if obj.tanuki.kind=='BOUNDARY': layout.prop(obj.tanuki,'boundary_shape')
            if obj.tanuki.kind=='START': layout.prop(obj.tanuki,'spawn_tag')
            if obj.tanuki.kind=='LIGHT':
                for prop in ('color','intensity','range_m','shadows'):
                    layout.prop(obj.tanuki,prop)
                if obj.data.type=='SPOT':
                    layout.prop(obj.tanuki,'inner_cone');layout.prop(obj.tanuki,'outer_cone')


CLASSES=(TanukiObject,TanukiPreferences,TANUKI_OT_new,TANUKI_OT_element,TANUKI_OT_add_selection,TANUKI_OT_material,TANUKI_OT_material_presets,TANUKI_OT_collision,TANUKI_OT_open,TANUKI_OT_export,TANUKI_OT_validate,TANUKI_OT_play,TANUKI_OT_workshop,TANUKI_PT_editor)


def register():
    global _draw
    from . import materials
    materials.register()
    for cls in CLASSES: bpy.utils.register_class(cls)
    bpy.types.Object.tanuki=PointerProperty(type=TanukiObject)
    bpy.types.Scene.tanuki_tab=EnumProperty(items=[(v,v.title(),'') for v in ('NEW','OPEN','EXPORT')])
    bpy.types.Scene.tanuki_collision_overlay=BoolProperty(default=True,update=lambda s,c:redraw())
    bpy.types.Scene.tanuki_workshop_item=StringProperty(name='Workshop Item ID')
    bpy.types.Scene.tanuki_workshop_choice=EnumProperty(items=workshop_items,update=workshop_selected)
    bpy.types.WindowManager.tanuki_status=StringProperty()
    _draw=bpy.types.SpaceView3D.draw_handler_add(draw_overlay,(),'WINDOW','POST_VIEW')


def unregister():
    global _draw
    from . import workshop
    workshop.close()
    if _draw: bpy.types.SpaceView3D.draw_handler_remove(_draw,'WINDOW');_draw=None
    restore_collision_view()
    for owner,key in ((bpy.types.Object,'tanuki'),(bpy.types.Scene,'tanuki_tab'),(bpy.types.Scene,'tanuki_collision_overlay'),(bpy.types.Scene,'tanuki_workshop_item'),(bpy.types.Scene,'tanuki_workshop_choice'),(bpy.types.WindowManager,'tanuki_status')):
        delattr(owner,key)
    for cls in reversed(CLASSES): bpy.utils.unregister_class(cls)
    from . import materials
    materials.unregister()
