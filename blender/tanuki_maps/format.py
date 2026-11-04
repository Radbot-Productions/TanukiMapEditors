"""Bounded portable-map container; no Blender dependency or executable payloads."""
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import tempfile

MAX_JSON = 8 * 1024 * 1024
MAX_GLB = 256 * 1024 * 1024


def require(condition, message):
    if not condition:
        raise ValueError(message)


def vector(value, count=3):
    require(isinstance(value, list) and len(value) == count and
            all(isinstance(v, (int, float)) and not isinstance(v, bool) and
                math.isfinite(v) and abs(v) <= 10000000 for v in value), "Invalid numeric vector.")
    return value


def frame(record):
    vector(record.get('position'))
    q = vector(record.get('rotation'), 4)
    require(abs(sum(v*v for v in q)-1) < .001, "Rotation must be normalized.")


def _unique(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, "Duplicate JSON field: " + key)
        result[key] = value
    return result


def glb_read(data):
    require(28 <= len(data) <= MAX_GLB, "Invalid GLB length.")
    magic, version, length, count, kind = struct.unpack_from('<4sIIII', data)
    require(magic == b'glTF' and version == 2 and length == len(data) and
            kind == 0x4E4F534A and 0 < count <= MAX_JSON and 20+count+8 <= length,
            "Unsupported GLB container.")
    doc = json.loads(data[20:20+count], object_pairs_hook=_unique)
    size, kind = struct.unpack_from('<II', data, 20+count)
    require(kind == 0x004E4942 and 28+count+size == length, "Invalid GLB binary chunk.")
    require(len(doc.get('buffers', [])) == 1 and not doc['buffers'][0].get('uri'), "External buffers are forbidden.")
    require(all(not i.get('uri') and i.get('mimeType') == 'image/png' for i in doc.get('images', [])),
            "Use embedded PNG textures.")
    allowed = {'KHR_texture_transform', 'KHR_materials_emissive_strength'}
    require(set(doc.get('extensionsUsed', [])) <= allowed and
            set(doc.get('extensionsRequired', [])) <= allowed, "Unsupported glTF extension; use portable PBR materials.")
    binary=data[28+count:]
    require(0 <= doc['buffers'][0].get('byteLength',-1) <= len(binary), 'Invalid GLB buffer length.')
    views=doc.get('bufferViews',[])
    for view in views:
        offset,size=view.get('byteOffset',0),view.get('byteLength',-1)
        require(type(offset) is int and type(size) is int and view.get('buffer',0)==0 and offset>=0 and size>=0 and offset+size<=len(binary),'Invalid GLB buffer view.')
    for image in doc.get('images',[]):
        index=image.get('bufferView',-1)
        require(type(index) is int and 0<=index<len(views),'Invalid image buffer view.')
        view=views[index];offset=view.get('byteOffset',0)
        require(view['byteLength']>=24 and binary[offset:offset+8]==b'\x89PNG\r\n\x1a\n','Invalid embedded PNG.')
        width,height=struct.unpack_from('>II',binary,offset+16)
        require(0<width<=8192 and 0<height<=8192,'Embedded image exceeds 8192 pixels.')
    return doc, binary


def glb_write(doc, binary):
    content = json.dumps(doc, separators=(',', ':'), allow_nan=False).encode()
    content += b' ' * (-len(content) % 4)
    binary += b'\0' * (-len(binary) % 4)
    return (struct.pack('<4sII', b'glTF', 2, 28+len(content)+len(binary)) +
            struct.pack('<II', len(content), 0x4E4F534A) + content +
            struct.pack('<II', len(binary), 0x004E4942) + binary)


def validate(m, glb):
    require(m.get('schemaVersion') in (1, 2, 3, 4, 5), "Unsupported map schema.")
    require(isinstance(m.get('mapId'), str) and 0 < len(m['mapId']) <= 128, "Invalid Map ID.")
    require(hashlib.sha1(glb).hexdigest() == m.get('sceneSha1', '').lower(), "Geometry checksum mismatch.")
    if m['schemaVersion']>=4:
        require(m.get('baseLighting') in ('cartoon-street-day','cartoon-street-night'), 'Unsupported base lighting profile.')
    if 'baseSun' in m:
        sun=m['baseSun'];require(m['schemaVersion']>=5 and isinstance(sun,dict),'Unsupported base sun override.')
        frame(dict(position=[0,0,0],rotation=sun.get('rotation')))
        require(all(0<=v<=1 for v in vector(sun.get('color'))),'Invalid base sun color.')
        intensity=sun.get('intensity')
        require(type(intensity) in (int,float) and math.isfinite(intensity) and 0<=intensity<=10000000,'Invalid base sun intensity.')
        require(type(sun.get('shadows')) is bool,'Invalid base sun shadows.')
    doc, _ = glb_read(glb)
    ids = set()
    for group in ('meshes', 'colliders', 'markers', 'lights'):
        entries = m.get(group, [] if group == 'lights' else None)
        require(isinstance(entries, list) and len(entries) <= (256 if group == 'lights' else 10000), "Invalid " + group + " count.")
        for e in entries:
            identity = e.get('id')
            require(isinstance(identity, str) and 0 < len(identity) <= 128 and identity not in ids, "Invalid or duplicate element ID.")
            ids.add(identity)
            if group == 'meshes':
                require(type(e.get('mesh')) is int and 0 <= e['mesh'] < len(doc.get('meshes', [])), "Missing GLB mesh.")
                require(type(e.get('visible')) is bool and type(e.get('collision')) is bool and (e['visible'] or e['collision']), "Invalid mesh usage.")
            else:
                frame(e)
            if group == 'markers':
                require(e.get('type') == 'playerStart' and isinstance(e.get('spawnTag'), str) and len(e['spawnTag']) <= 128, "Unsupported player marker.")
            if group == 'colliders':
                require(e.get('type') in ('box', 'sphere', 'capsule'), "Unsupported collider.")
                if e['type'] == 'box':
                    require(min(vector(e.get('size'))) > 0, "Invalid box size.")
                else:
                    radius = e.get('radius', 0)
                    require(isinstance(radius, (int, float)) and math.isfinite(radius) and 0 < radius <= 10000000, "Invalid radius.")
                    if e['type'] == 'capsule':
                        require(radius <= e.get('halfHeight', 0) <= 10000000, "Invalid capsule height.")
            if group == 'lights':
                require(m['schemaVersion'] >= 3 and e.get('type') in ('point', 'spot', 'directional'), "Unsupported portable light.")
                require(all(0 <= v <= 1 for v in vector(e.get('color'))), "Invalid light color.")
                require(type(e.get('shadows')) is bool, "Missing light shadow setting.")
                intensity = e.get('intensity')
                require(isinstance(intensity, (int, float)) and math.isfinite(intensity) and 0 <= intensity <= 10000000, "Invalid light intensity.")
                reach = e.get('range')
                require(isinstance(reach, (int, float)) and math.isfinite(reach) and 0 < reach <= 10000000, "Invalid light range.")
                inner, outer = e.get('innerCone'), e.get('outerCone')
                require(isinstance(inner, (int, float)) and isinstance(outer, (int, float)) and 0 <= inner <= outer < 90, "Invalid light cone angles.")
    require(m['meshes'] and m['markers'], "A map needs geometry and player starts.")
    if m['schemaVersion'] >= 2:
        b = m.get('boundary')
        require(isinstance(b, dict) and b.get('id') == 'boundary' and 'boundary' not in ids and len(m['markers']) >= 4,
                "A playable map needs one boundary and four player starts.")
        frame(b)
        size = vector(b.get('size'))
        require(b.get('shape') in ('box', 'cylinder') and min(size) >= 1 and
                (b['shape'] != 'cylinder' or abs(size[0]-size[1]) <= .1), "Invalid map boundary.")
        ids.add('boundary')
    authored=m.get('authoring')
    if authored:
        nodes=authored.get('nodes',[])
        require(authored.get('version')==1 and 0<len(nodes)<=30000,'Unsupported authoring metadata.')
        depths=[]
        for i,node in enumerate(nodes):
            parent=node.get('parent')
            require(type(parent) is int and ((i==0 and parent==-1) or (i>0 and 0<=parent<i)),'Invalid authoring hierarchy.')
            depths.append(0 if parent<0 else depths[parent]+1)
            require(depths[-1]<=128 and isinstance(node.get('name'),str) and len(node['name'])<=1024,'Authoring hierarchy exceeds its limits.')
            for key,components in (('position','xyz'),('rotation','xyzw'),('scale','xyz')):
                value=node.get(key,{})
                vector([value.get(c) for c in components],len(components))
        bindings=authored.get('bindings',[])
        require(len(bindings)==len(ids) and {b.get('id') for b in bindings}==ids and
                all(type(b.get('node')) is int and 0<=b['node']<len(nodes) for b in bindings),'Authoring bindings do not match map entries.')
    return doc


def read(path):
    require(Path(path).stat().st_size <= 16+MAX_JSON+MAX_GLB, "Map exceeds the size limit.")
    with open(path, 'rb') as stream:
        header = stream.read(16)
        require(len(header) == 16, "Truncated map header.")
        magic, version, j, g = struct.unpack('<4sIII', header)
        require(magic == b'TMAP' and version == 1 and 0 < j <= MAX_JSON and 28 <= g <= MAX_GLB,
                "Unsupported map container or payload size.")
        manifest = stream.read(j)
        data = stream.read(g)
        require(len(manifest) == j and len(data) == g and not stream.read(1), "Invalid map lengths.")
    m = json.loads(manifest.decode('utf-8'), object_pairs_hook=_unique)
    validate(m, data)
    return m, data


def write(path, manifest, glb):
    require(Path(path).suffix.lower() == '.tanukimap', "Choose a .tanukimap filename.")
    manifest['sceneSha1'] = hashlib.sha1(glb).hexdigest()
    validate(manifest, glb)
    encoded = json.dumps(manifest, separators=(',', ':'), allow_nan=False).encode()
    require(len(encoded) <= MAX_JSON, "Manifest exceeds 8 MiB.")
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(dir=Path(path).parent, suffix='.tmp')
    try:
        with os.fdopen(fd, 'wb') as stream:
            stream.write(struct.pack('<4sIII', b'TMAP', 1, len(encoded), len(glb)))
            stream.write(encoded)
            stream.write(glb)
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)
