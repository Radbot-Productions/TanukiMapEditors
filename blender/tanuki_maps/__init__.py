bl_info = {'name':'Tanuki Map Editor','author':'Radbot Productions','version':(0,2,0),
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
_preview_timers = []


def update_environment(self, context):
    if isinstance(self.id_data,bpy.types.Object) and self.kind=='LIGHTING' and not self.id_data.get('_tanuki_sun_sync'):
        from . import environment
        environment.apply(self.id_data)

def update_lighting_preset(self,context):
    if not self.id_data.get('_tanuki_sun_sync'):
        self.override_sun=False
        update_environment(self,context)


def update_light(self, context):
    if isinstance(self.id_data, bpy.types.Object):
        scene.sync_light(self.id_data)


def workshop_items(self,context):
    from . import workshop
    return workshop.items()


def workshop_selected(self,context):
    if self.tanuki_workshop_choice!='NONE':self.tanuki_workshop_item=self.tanuki_workshop_choice


class TanukiObject(bpy.types.PropertyGroup):
    export_enabled: BoolProperty(name='Include in Export',description='Export this object and its children; generated preview helpers are always excluded',default=True)
    kind: EnumProperty(items=[(v,v.title(),'') for v in ('NONE','ROOT','BOUNDARY','START','LIGHT','LIGHTING','SKYBOX','PREVIEW')],default='NONE')
    base_lighting: EnumProperty(name='Base Lighting',items=[('cartoon-street-day','Cartoon Street Day','Daytime base lighting'),('cartoon-street-night','Cartoon Street Night','Nighttime base lighting')],update=update_lighting_preset)
    override_sun: BoolProperty(name='Override Preset Sun',update=update_environment)
    sun_rotation: FloatVectorProperty(name='Sun Rotation',size=3,subtype='EULER',update=update_environment)
    sun_color: FloatVectorProperty(name='Sun Color',size=3,subtype='COLOR',min=0,max=1,default=(1,1,1),update=update_environment)
    sun_intensity: FloatProperty(name='Sun Intensity (lux)',min=0,max=10000000,default=6,update=update_environment)
    sun_shadows: BoolProperty(name='Sun Shadows',default=True,update=update_environment)
    usage: EnumProperty(name='Export As',items=[('SCENERY','Scenery','Visible without collision'),('BOTH','Scenery + Collision','Visible and blocking'),('COLLISION','Collision Only','Invisible blocking mesh'),('NONE','Exclude','Not exported')],default='SCENERY')
    map_id: StringProperty(name='Map ID')
    workshop_id: StringProperty(name='Workshop Item',description='Retained even if a publishing job fails after item creation')
    export_path: StringProperty(name='Map File',subtype='FILE_PATH')
    preview_png: StringProperty(name='Preview PNG',subtype='FILE_PATH')
    spawn_tag: StringProperty(name='Spawn Tag',description='Optional player-start identifier for targeted spawning; leave blank for ordinary maps',maxlen=128)
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
    for obj in scene.export_objects(owner):
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
    bl_description='Create a separate editable map scene from an empty, room, or courtyard sample'
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
    bl_description='Add a boundary, player start, or portable light to the selected map'
    bl_idname='tanuki.add_element'
    bl_label='Add Element'
    bl_options={'REGISTER','UNDO'}
    element: StringProperty()
    def execute(self, context):
        try:
            owner=scene.root(context)
            existing=next((o for o in context.scene.objects if o.tanuki.kind==self.element),None) if self.element=='BOUNDARY' else None
            if self.element=='SKYBOX':
                from . import environment
                obj=environment.skybox(owner)
            elif self.element=='LIGHTING':
                from . import environment
                obj=environment.ensure(owner)
            elif existing:
                obj=existing
                self.report({'INFO'},'Selected the existing map boundary')
            elif self.element in ('point','spot','directional'):
                obj=scene.add_light(owner,self.element)
            else:
                obj=scene.empty('Player Start' if self.element=='START' else 'Play Boundary',owner,self.element)
                if self.element=='BOUNDARY':
                    obj.location,obj.scale=(0,0,3.875),(20,20,8.25)
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
    bl_description='Include the selected objects in this map for validation and export'
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
    bl_description='Create a Tanuki PBR material with portable texture and scalar inputs'
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
    bl_description="Load the native Tanuki material examples for use in Blender's material editor"
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
    bl_description='Show only blocking map geometry, or restore the previous viewport visibility'
    bl_idname='tanuki.collision_view'
    bl_label='Collision Only / Restore View'
    def execute(self, context):
        global _hidden
        if _hidden:
            restore_collision_view()
        else:
            try: owner=scene.root(context)
            except ValueError as error: self.report({'ERROR'},str(error));return {'CANCELLED'}
            for obj in scene.export_objects(owner):
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
    bl_description='Import a portable Tanuki map into a separate editable scene'
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
    bl_description='Validate and save this map as a portable .tanukimap file'
    bl_idname='tanuki.export_map'
    bl_label='Export Map File'
    filename_ext='.tanukimap'
    filter_glob:StringProperty(default='*.tanukimap',options={'HIDDEN'})
    def execute(self,context):
        try: scene.export_map(scene.root(context),self.filepath);self.report({'INFO'},'Map exported.');return {'FINISHED'}
        except Exception as error: self.report({'ERROR'},str(error));return {'CANCELLED'}


class TANUKI_OT_validate(bpy.types.Operator):
    """Check geometry, materials, lights, boundary and player starts before export"""
    bl_description='Check geometry, materials, lights, boundary and player starts before export'
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
        self.layout.label(text='Play in Tanuki installs and updates its preview player automatically.')


class TANUKI_OT_play(bpy.types.Operator):
    """Export a temporary snapshot and test it with production Tanuki movement"""
    bl_description='Launch a new preview from the current scene, including unsaved edits; relaunch after further changes'
    bl_idname='tanuki.play_map'
    bl_label='Play in Tanuki'
    def execute(self,context):
        job=None
        process=None
        try:
            prefs=context.preferences.addons[__package__].preferences
            exe=Path(bpy.path.abspath(prefs.preview_executable)) if prefs.preview_executable else Path(__file__).parent/'preview/TanukiMapPreview.exe'
            import json
            setup=Path(__file__).parent/'native/TanukiPreviewSetup.exe'
            scene.fmt.require(setup.is_file(),'The preview setup helper is missing. Reinstall the current Tanuki editor plugin package.')
            job=Path(bpy.utils.user_resource('CONFIG',path='tanuki_maps/preview',create=True))/uuid.uuid4().hex
            job.mkdir()
            target=job/'map.tanukimap'
            owner=scene.root(context)
            remembered=owner.tanuki.export_path
            try: scene.export_map(owner,str(target))
            finally: owner.tanuki.export_path=remembered
            request=job/'setup-request.json'
            request.write_text(json.dumps(dict(requiredSchema=5,candidateExecutable=str(exe),mapFile=str(target),logFile=str(job/'preview.log'))),encoding='utf-8')
            process=subprocess.Popen([str(setup),str(request)],cwd=str(setup.parent),creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            context.window_manager.tanuki_status='Preparing preview. The player installs automatically on first use.'
            context.window_manager.tanuki_preview_log=''
            _preview_processes.append((process,job))
            def cleanup():
                if process.poll() is None:
                    progress=job/'setup-progress.json'
                    if (job/'preview.log').exists() and 'TANUKI_PREVIEW_READY' in (job/'preview.log').read_text(errors='replace'):
                        context.window_manager.tanuki_status='Tanuki preview is running. Escape opens its menu.'
                    elif progress.exists():
                        state=json.loads(progress.read_text(encoding='utf-8-sig'));message=state.get('phase','Preparing preview')
                        if state.get('total',0)>1:message+=f" ({state.get('bytes',0)//1048576} / {state['total']//1048576} MB)"
                        context.window_manager.tanuki_status=message
                    return 1
                log=job/'preview.log'
                if not log.exists() and (job/'setup-result.json').exists():log.write_text((job/'setup-result.json').read_text(encoding='utf-8-sig'),encoding='utf-8')
                text=log.read_text(errors='replace') if log.exists() else ''
                ready='TANUKI_PREVIEW_READY' in text
                failed=process.returncode or not log.exists() or 'TANUKI_PREVIEW_FAILED' in text or 'PORTABLE_MAP_FAILED' in text
                target.unlink(missing_ok=True)
                if failed:
                    context.window_manager.tanuki_status='Preview did not start. Open Preview Log for the reason.'
                    context.window_manager.tanuki_preview_log=str(log) if log.exists() else ''
                elif not ready:
                    context.window_manager.tanuki_status='Preview closed before the map was ready. Relaunch to finish loading.'
                    context.window_manager.tanuki_preview_log=str(log) if log.exists() else ''
                else:
                    context.window_manager.tanuki_status='Preview closed. Play in Tanuki includes current scene edits.'
                    log.unlink(missing_ok=True)
                    context.window_manager.tanuki_preview_log=''
                    for name in ('setup-request.json','setup-progress.json','setup-result.json','cancel'):(job/name).unlink(missing_ok=True)
                if not any(job.iterdir()):job.rmdir()
                _preview_processes.remove((process,job))
                _preview_timers.remove(cleanup)
                return None
            bpy.app.timers.register(cleanup,first_interval=1)
            _preview_timers.append(cleanup)
            return {'FINISHED'}
        except Exception as error:
            if job is not None and process is None:
                for name in ('map.tanukimap','preview.log','setup-request.json'):(job/name).unlink(missing_ok=True)
                job.rmdir()
            self.report({'ERROR'},str(error));return {'CANCELLED'}


class TANUKI_OT_open_preview_log(bpy.types.Operator):
    """Open the retained diagnostics for the last unsuccessful gameplay preview"""
    bl_description='Open the retained diagnostics for the last unsuccessful gameplay preview'
    bl_idname='tanuki.open_preview_log'
    bl_label='Open Preview Log'
    def execute(self,context):
        bpy.ops.wm.path_open(filepath=context.window_manager.tanuki_preview_log)
        return {'FINISHED'}


class TANUKI_OT_workshop(bpy.types.Operator):
    """Open the Tanuki Workshop browser and publishing tools"""
    bl_description='Open the Tanuki Workshop browser and publishing tools'
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
        from . import workshop
        workshop.auto_connect(context)
        try:owner=scene.root(context)
        except ValueError:owner=None
        header,files=layout.panel('tanuki_map_files',default_closed=False)
        header.label(text='Map Files',icon='FILE_FOLDER')
        if files:
            files.row(align=True).prop(context.scene,'tanuki_tab',expand=True)
            tab=context.scene.tanuki_tab
            if tab=='NEW':
                for kind,label in (('EMPTY','Create Empty Map'),('ROOM','Room Sample'),('COURTYARD','Courtyard Sample')):
                    files.operator('tanuki.new_map',text=label).sample=kind
            elif tab=='OPEN':
                files.operator('tanuki.open_map',icon='FILE_FOLDER')
                files.operator('tanuki.workshop',text='Refresh Workshop Maps').action='refresh'
                files.prop(context.scene,'tanuki_workshop_choice',text='Workshop Map')
                files.prop(context.scene,'tanuki_workshop_item')
                files.operator('tanuki.workshop',text='Open Workshop Map').action='download'
            elif owner:
                files.prop(owner,'name',text='Name')
                files.prop(owner.tanuki,'map_id')
                files.prop(owner.tanuki,'workshop_id')
                files.prop(owner.tanuki,'preview_png')
                files.separator()
                files.operator('tanuki.validate_map',icon='CHECKMARK')
                files.operator('tanuki.export_map',icon='EXPORT')
                files.operator('tanuki.workshop',text='Publish / Update Workshop').action='publish'
            else:files.label(text='Create or open a map first.')
        if not owner:return
        header,preview=layout.panel('tanuki_preview',default_closed=False)
        header.label(text='Preview',icon='PLAY')
        if preview:
            preview.operator('tanuki.play_map',icon='PLAY')
            if context.window_manager.tanuki_preview_log:
                preview.operator('tanuki.open_preview_log',icon='TEXT')
        message=context.window_manager.tanuki_status
        if message:
            import textwrap
            box=layout.box()
            for line in message.splitlines():
                for part in textwrap.wrap(line,width=max(22,int(context.region.width/7))):box.label(text=part)
        header,components=layout.panel('tanuki_components',default_closed=False)
        header.label(text='Components',icon='OUTLINER_OB_GROUP_INSTANCE')
        if components:
            for kind,label in (('LIGHTING','Lighting Settings'),('SKYBOX','Skybox'),('START','Player Start'),('BOUNDARY','Play Boundary'),('point','Point Light'),('spot','Spot Light'),('directional','Directional Light')):
                components.operator('tanuki.add_element',text=label).element=kind
        header,collision=layout.panel('tanuki_collision',default_closed=True)
        header.label(text='Collision',icon='MOD_PHYSICS')
        if collision:
            collision.prop(context.scene,'tanuki_collision_overlay',text='Show Through Geometry')
            collision.operator('tanuki.collision_view')
        obj=context.object
        if owner and obj and obj!=owner and obj.name in context.scene.objects:
            header,selection=layout.panel('tanuki_selected_element',default_closed=False)
            header.label(text='Selected Element')
            if not selection:return
            if obj.tanuki.kind not in ('PREVIEW','LIGHTING','SKYBOX'):selection.prop(obj.tanuki,'export_enabled')
            if obj.type=='MESH' and obj.tanuki.kind!='PREVIEW':
                selection.prop(obj.tanuki,'usage')
                selection.operator('tanuki.new_material',icon='MATERIAL')
                selection.operator('tanuki.material_presets',icon='ASSET_MANAGER')
            if obj.tanuki.kind=='LIGHTING':
                selection.prop(obj.tanuki,'base_lighting');selection.prop(obj.tanuki,'override_sun')
                sun=selection.column();sun.enabled=obj.tanuki.override_sun
                for key in ('sun_rotation','sun_color','sun_intensity','sun_shadows'):sun.prop(obj.tanuki,key)
            if obj.tanuki.kind=='SKYBOX':
                selection.prop(obj.parent.tanuki,'base_lighting',text='Sky Preset')
                selection.label(text='Linked to Base Lighting')
            if obj.tanuki.kind=='BOUNDARY':selection.prop(obj.tanuki,'boundary_shape')
            if obj.tanuki.kind=='START':selection.prop(obj.tanuki,'spawn_tag')
            if obj.tanuki.kind=='LIGHT':
                for prop in ('color','intensity','range_m','shadows'):selection.prop(obj.tanuki,prop)
                if obj.data.type=='SPOT':selection.prop(obj.tanuki,'inner_cone');selection.prop(obj.tanuki,'outer_cone')


CLASSES=(TanukiObject,TanukiPreferences,TANUKI_OT_new,TANUKI_OT_element,TANUKI_OT_add_selection,TANUKI_OT_material,TANUKI_OT_material_presets,TANUKI_OT_collision,TANUKI_OT_open,TANUKI_OT_export,TANUKI_OT_validate,TANUKI_OT_play,TANUKI_OT_open_preview_log,TANUKI_OT_workshop,TANUKI_PT_editor)


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
    bpy.types.WindowManager.tanuki_preview_log=StringProperty(subtype='FILE_PATH')
    _draw=bpy.types.SpaceView3D.draw_handler_add(draw_overlay,(),'WINDOW','POST_VIEW')


def unregister():
    global _draw
    from . import workshop
    workshop.close()
    for timer in _preview_timers:
        if bpy.app.timers.is_registered(timer):bpy.app.timers.unregister(timer)
    _preview_timers.clear()
    for process,job in _preview_processes:
        if process.poll() is None:
            (job/'cancel').write_text('',encoding='utf-8')
            try:process.wait(timeout=5)
            except subprocess.TimeoutExpired:process.kill();process.wait()
        (job/'map.tanukimap').unlink(missing_ok=True)
    _preview_processes.clear()
    if _draw: bpy.types.SpaceView3D.draw_handler_remove(_draw,'WINDOW');_draw=None
    restore_collision_view()
    for owner,key in ((bpy.types.Object,'tanuki'),(bpy.types.Scene,'tanuki_tab'),(bpy.types.Scene,'tanuki_collision_overlay'),(bpy.types.Scene,'tanuki_workshop_item'),(bpy.types.Scene,'tanuki_workshop_choice'),(bpy.types.WindowManager,'tanuki_status'),(bpy.types.WindowManager,'tanuki_preview_log')):
        delattr(owner,key)
    for cls in reversed(CLASSES): bpy.utils.unregister_class(cls)
    from . import materials
    materials.unregister()
