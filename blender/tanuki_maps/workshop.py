"""One owned SDK helper process per operation. Durable receipts retain created IDs."""
import json
from pathlib import Path
import subprocess
import uuid
import bpy
from . import scene

_active=None
_timer=None
_catalog=[('NONE','Choose a Workshop map','')]


def items():
    return _catalog


def start(context,action):
    global _active,_timer
    scene.fmt.require(_active is None,'A Workshop operation is already pending.')
    bridge=Path(__file__).parent/'native/TanukiWorkshop.exe'
    scene.fmt.require(bridge.is_file(),'Install the complete Blender kit; its Workshop helper is missing.')
    owner=None
    request={'action':action}
    job=Path(bpy.utils.user_resource('CONFIG',path='tanuki_maps/workshop',create=True))/uuid.uuid4().hex
    job.mkdir()
    if action=='download':
        request['id']=context.scene.tanuki_workshop_item
        scene.fmt.require(request['id'].isdigit() and int(request['id'])>0,'Choose a Workshop map or enter its ID.')
    elif action=='publish':
        owner=scene.root(context)
        content=job/'content';content.mkdir()
        target=content/'map.tanukimap'
        remembered=owner.tanuki.export_path
        try: scene.export_map(owner,str(target))
        finally: owner.tanuki.export_path=remembered
        preview=Path(bpy.path.abspath(owner.tanuki.preview_png)) if owner.tanuki.preview_png else thumbnail(owner,job/'preview.png')
        request.update(file=str(target),preview=str(preview),title=owner.name,id=owner.tanuki.workshop_id or '0')
    (job/'request.json').write_text(json.dumps(request))
    process=subprocess.Popen([str(bridge),str(job/'request.json')],cwd=str(bridge.parent),
                             creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0),stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    wm=context.window_manager
    wm.tanuki_status='Workshop operation pending…'
    _active=(process,job,owner)
    def poll():
        global _active,_timer,_catalog
        result_path=job/'result.json'
        result=json.loads(result_path.read_text()) if result_path.exists() else {}
        if owner and result.get('id') and result['id']!='0':
            try:owner.tanuki.workshop_id=result['id']
            except ReferenceError:pass
        if process.poll() is None:
            return .2
        try:
            if not result.get('success'):
                wm.tanuki_status=result.get('error','Workshop helper stopped; inspect '+str(job))
            elif action=='refresh':
                items=result.get('items',[])
                _catalog=[('NONE','Choose a Workshop map','')]+[(str(i['Id']),i['Title']+(' · Your map' if i['Owned'] else ''),'') for i in items]
                wm.tanuki_status=result.get('account','Steam')+' · '+str(len(items))+' maps'
            elif action=='download':
                imported=scene.import_map(result['file'],fork=not result['owned'])
                from . import restore_collision_view
                restore_collision_view()
                imported.name=result['title']
                if result['owned']: imported.tanuki.workshop_id=result['id']
                wm.tanuki_status='Workshop map opened.'
            else:
                wm.tanuki_status='Published Workshop item '+result['id']+'. Save the .blend file to retain its association.'
        except Exception as error: wm.tanuki_status=str(error)
        finally:
            # Preserve the small request/result receipt, retire the map snapshot.
            for map_file in job.rglob('*.tanukimap'): map_file.unlink()
            _active=None;_timer=None
        return None
    _timer=poll
    bpy.app.timers.register(poll,first_interval=.2)


def thumbnail(owner,target):
    # A temporary scene contains only copies of exported scenery plus a camera/light.
    from mathutils import Vector
    source=bpy.context.window.scene
    preview=bpy.data.scenes.new('_TanukiThumbnail')
    owned=[]
    try:
        points=[]
        for obj in scene.descendants(owner):
            if obj.type!='MESH' or obj.tanuki.usage not in ('SCENERY','BOTH'):continue
            copy=obj.copy();copy.data=obj.data.copy();copy.parent=None;copy.matrix_world=obj.matrix_world.copy()
            preview.collection.objects.link(copy);owned.append(copy)
            points.extend(copy.matrix_world@Vector(v) for v in copy.bound_box)
        scene.fmt.require(points,'Add scenery for the preview image.')
        center=sum(points,Vector())/len(points)
        radius=max((p-center).length for p in points)
        camera=bpy.data.objects.new('Thumbnail Camera',bpy.data.cameras.new('Thumbnail Camera'))
        preview.collection.objects.link(camera);owned.append(camera)
        camera.location=center+Vector((1,-1,.8)).normalized()*max(3,radius*2.8)
        camera.rotation_euler=(center-camera.location).to_track_quat('-Z','Y').to_euler()
        preview.camera=camera
        sun=bpy.data.objects.new('Thumbnail Sun',bpy.data.lights.new('Thumbnail Sun','SUN'))
        preview.collection.objects.link(sun);owned.append(sun);sun.rotation_euler=(.4,-.5,-.6);sun.data.energy=3
        preview.world=bpy.data.worlds.new('Thumbnail World');preview.world.color=(.2,.2,.2)
        preview.render.engine='BLENDER_EEVEE_NEXT'
        preview.render.resolution_x=512;preview.render.resolution_y=512;preview.render.resolution_percentage=100
        preview.render.image_settings.file_format='PNG';preview.render.filepath=str(target)
        bpy.ops.render.render(write_still=True,scene=preview.name)
        return target
    finally:
        for obj in owned:
            data=obj.data;bpy.data.objects.remove(obj,do_unlink=True)
            if isinstance(data,bpy.types.Mesh):bpy.data.meshes.remove(data)
            elif isinstance(data,bpy.types.Light):bpy.data.lights.remove(data)
            elif isinstance(data,bpy.types.Camera):bpy.data.cameras.remove(data)
        world=preview.world
        bpy.data.scenes.remove(preview)
        if world:bpy.data.worlds.remove(world)
        bpy.context.window.scene=source


def close():
    global _active,_timer
    if _timer and bpy.app.timers.is_registered(_timer):bpy.app.timers.unregister(_timer)
    _timer=None
    if _active:
        process,job,owner=_active
        # Retain the receipt and identity; helper owns its callbacks until exit.
        bpy.context.window_manager.tanuki_status='Workshop outcome pending; inspect '+str(job)
    _active=None
