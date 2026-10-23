"""Fit the supplied 16 m grid/8 m road template and derive six mixed junctions.

Python 3, standard library only. This is an offline fitting tool for the
three-cell Blender template, not a general-purpose intersection generator.
It extracts the named mesh even when the GLB also contains other scene objects.
Re-run CityGenerateRoadCollision after replacing game meshes.
"""
import argparse
import json
import math
import struct
from pathlib import Path

def read_template(path, node_name='road_mixed_3way_grid_nw'):
    data = path.read_bytes()
    magic, version, total = struct.unpack_from('<III', data)
    if magic != 0x46546C67 or version != 2 or total != len(data):
        raise ValueError('Expected a glTF 2.0 binary file')
    chunks = {}
    offset = 12
    while offset < len(data):
        length, kind = struct.unpack_from('<II', data, offset)
        chunks[kind] = data[offset+8:offset+8+length]
        offset += 8 + length
    doc = json.loads(chunks[0x4E4F534A]); blob = chunks[0x004E4942]
    nodes = [n for n in doc['nodes'] if n.get('name') == node_name]
    if len(nodes) != 1:
        raise ValueError(f'Expected exactly one node named {node_name}')
    node = nodes[0]
    if node.get('translation', [0,0,0]) != [0,0,0] or node.get('scale', [1,1,1]) != [1,1,1] or \
       node.get('rotation', [0,0,0,1]) != [0,0,0,1] or 'matrix' in node:
        raise ValueError('Apply the template object transforms before exporting')
    def accessor(index):
        a = doc['accessors'][index]; v = doc['bufferViews'][a['bufferView']]
        if 'sparse' in a or v['buffer'] != 0:
            raise ValueError('Sparse/external accessors are not supported by this fitting script')
        count = {'SCALAR':1, 'VEC2':2, 'VEC3':3}[a['type']]
        fmt = '<' + {5126:'f', 5123:'H', 5125:'I'}[a['componentType']] * count
        offset = v.get('byteOffset',0) + a.get('byteOffset',0)
        stride = v.get('byteStride',struct.calcsize(fmt))
        return [struct.unpack_from(fmt,blob,offset+i*stride) for i in range(a['count'])]
    primitives = doc['meshes'][node['mesh']]['primitives']
    if len(primitives) != 1 or primitives[0].get('mode',4) != 4:
        raise ValueError('Expected one indexed triangle primitive')
    primitive = primitives[0]; ids = [row[0] for row in accessor(primitive['indices'])]
    return {'vertices':accessor(primitive['attributes']['POSITION']),
            'uv':accessor(primitive['attributes']['TEXCOORD_0']),
            'indices':[ids[i:i+3] for i in range(0,len(ids),3)]}

def clip(poly, distance):
    result = []
    for a,b in zip(poly, poly[1:]+poly[:1]):
        da, db = distance(a), distance(b)
        if da >= 0: result.append(a)
        if (da >= 0) != (db >= 0):
            t = da/(da-db)
            result.append(tuple(x+t*(y-x) for x,y in zip(a,b)))
    return result

def write(path, triangles):
    verts, ids, lookup = [],[],{}
    for triangle in triangles:
        for v in triangle:
            key = tuple(round(f,7) for f in v)
            if key not in lookup:
                lookup[key] = len(verts); verts.append(v)
            ids.append(lookup[key])
    blob = bytearray(); views=[]; access=[]
    def attr(rows, fmt, typ, component, target, bounds=False):
        while len(blob)%4: blob.append(0)
        start=len(blob)
        for row in rows: blob.extend(struct.pack('<'+fmt,*row))
        views.append({'buffer':0,'byteOffset':start,'byteLength':len(blob)-start,'target':target})
        a={'bufferView':len(views)-1,'componentType':component,'count':len(rows),'type':typ}
        if bounds:
            a.update(min=[min(r[k] for r in rows) for k in range(len(rows[0]))], max=[max(r[k] for r in rows) for k in range(len(rows[0]))])
        access.append(a); return len(access)-1
    pos=attr([v[:3] for v in verts],'fff','VEC3',5126,34962,True)
    norm=attr([(0,1,0)]*len(verts),'fff','VEC3',5126,34962)
    uv=attr([v[3:] for v in verts],'ff','VEC2',5126,34962)
    ind=attr([(i,) for i in ids],'I','SCALAR',5125,34963)
    while len(blob)%4: blob.append(0)
    doc={'asset':{'version':'2.0','generator':'Cities Unlimited: slip-lane template, fitted road seams'},'scene':0,'scenes':[{'nodes':[0]}],
         'nodes':[{'mesh':0,'name':path.stem}], 'meshes':[{'primitives':[{'attributes':{'POSITION':pos,'NORMAL':norm,'TEXCOORD_0':uv},'indices':ind}]}],
         'accessors':access,'bufferViews':views,'buffers':[{'byteLength':len(blob)}]}
    js=json.dumps(doc,separators=(',',':')).encode();js+=b' '*((-len(js))%4)
    path.write_bytes(struct.pack('<III',0x46546C67,2,28+len(js)+len(blob))+struct.pack('<II',len(js),0x4E4F534A)+js+struct.pack('<II',len(blob),0x004E4942)+blob)
    print(path.name, len(verts), 'vertices;',len(ids)//3,'triangles')

S=math.sqrt(2)
def snap(v,uv):
    x,y,z=v
    if abs(x+24)<.02:x=-24.
    if abs(abs(z)-4)<.005:z=math.copysign(4.,z)
    if abs(x+z+32)<.03:
        d=(-32-x-z)/2;x+=d;z+=d
    if abs(abs(x-z)-4*S)<.002:
        d=(math.copysign(4*S,x-z)-(x-z))/2;x+=d;z-=d
    return (x,0.,z,*uv)
def facearea(t):
    a,b,c=t;return ((b[2]-a[2])*(c[0]-a[0])-(b[0]-a[0])*(c[2]-a[2]))/2
def extend(tris,dx,dz,old,new):
    # Continue each mouth boundary edge, including its UV seams, along its axis.
    length=math.hypot(dx,dz);ux,uz=dx/length,dz/length
    result=list(tris);edges={}
    for t in tris:
        for a,b in zip(t,t[1:]+t[:1]):
            if abs(a[0]*ux+a[2]*uz-old)>1e-4 or abs(b[0]*ux+b[2]*uz-old)>1e-4:continue
            if abs(a[0]-b[0])+abs(a[2]-b[2])<1e-7:continue
            key=tuple(sorted((tuple(round(f,5) for f in a[:3]),tuple(round(f,5) for f in b[:3]))))
            if key in edges:del edges[key]
            else:edges[key]=(a,b)
    assert edges,(dx,dz,old)
    for a,b in edges.values():
        def move(v):return (v[0]+ux*(new-old),0.,v[2]+uz*(new-old),v[3],v[4]+(new-old)/32)
        A,B=move(a),move(b)
        for t in [(a,A,B),(a,B,b)]:result.append(t if facearea(t)>0 else tuple(reversed(t)))
    return result

def transform(tris,func):
    result=[]
    for t in tris:
        t=tuple((func(v[0],v[2])[0],0.,func(v[0],v[2])[1],*v[3:]) for v in t)
        result.append(t if facearea(t)>0 else tuple(reversed(t)))
    return result


def fourway(first):
    second=transform(first,lambda x,z:(-x,-z));result=list(first)
    # For NW, the original contains the whole overlapping horizontal strip.
    # Keep only the opposite branch and the extension beyond the grid mouth.
    for t in second:
        parts=[];rest=list(t)
        for distance in [lambda v:8-v[0],lambda v:4-v[2]]:
            parts.append(clip(rest,lambda v:-distance(v)))
            rest=clip(rest,distance)
        for poly in parts:
            for i in range(1,len(poly)-1):
                t=(poly[0],poly[i],poly[i+1])
                if facearea(t)>1e-8:result.append(t)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('template', type=Path, help='Original Blender GLB export containing road_mixed_3way_grid_nw')
    parser.add_argument('--output', type=Path, default=Path(__file__).resolve().parents[1]/'assets/models/transportation/roads')
    args = parser.parse_args()
    p = read_template(args.template)
    verts = [snap(v,uv) for v,uv in zip(p['vertices'],p['uv'])]
    # The export has two almost coincident vertices at the diagonal lane split.
    # Weld positions within 1 mm while retaining each vertex's own UVs, so
    # extending the two halves of that mouth cannot create a narrow overlap.
    welded = []
    for v in verts:
        position = next((p[:3] for p in welded if math.dist(p[:3], v[:3]) < .001), v[:3])
        welded.append((*position, *v[3:]))
    verts = welded
    base = [tuple(verts[i] for i in face) for face in p['indices']]
    base = [t if facearea(t)>0 else tuple(reversed(t)) for t in base if abs(facearea(t))>1e-8]
    grid_nw=extend(base,-1,-1,16*S,24*S)
    grid_ne=transform(grid_nw,lambda x,z:(-x,z))
    # A rigid 45-degree rotation preserves the road and turning-lane widths.
    # Extend each opening independently to its logical cell's far seam.
    diagonal_n=transform(base,lambda x,z:((x-z)/S,(x+z)/S))
    for dx,dz,old,new in [(-1,-1,24,24*S),(0,-1,16*S,24),(1,1,8,8*S)]:
        diagonal_n=extend(diagonal_n,dx,dz,old,new)
    diagonal_e=transform(diagonal_n,lambda x,z:(-z,-x))
    four_nw=fourway(grid_nw)
    specs={'3way_grid_nw':grid_nw,'3way_grid_ne':grid_ne,'3way_diagonal_n':diagonal_n,'3way_diagonal_e':diagonal_e,
           '4way_nw_se':four_nw,'4way_ne_sw':transform(four_nw,lambda x,z:(-x,z))}

    args.output.mkdir(parents=True, exist_ok=True)
    for name,triangles in specs.items():
        assert all(facearea(t)>0 for t in triangles)
        write(args.output/f'road_mixed_{name}.glb',triangles)

if __name__ == '__main__':
    main()
