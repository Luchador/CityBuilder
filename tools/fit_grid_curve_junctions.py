"""Fit the supplied inner/outer grid-corner junctions (Python 3 stdlib only).

Keeps the authored curves and UVs, combines Blender material primitives, and
snaps tiny seam errors to the 16-meter grid. Runtime uses one texture per model.
Rebuild CityGenerateRoadCollision after geometry changes.
"""
import argparse
import math
from pathlib import Path

from fit_slip_junctions import read_template, facearea, write


def fit(source, inner):
    vertices = []
    for (x, y, z), uv in zip(source['vertices'],source['uv']):
        x, z = (round(v/4)*4. if abs(v-round(v/4)*4) < .001 else v for v in (x,z))
        cap = -48. if inner else 16.
        if abs(x+z-cap) < .001:
            delta = (cap-x-z)/2
            x += delta; z += delta
        position = (x,0.,z)
        # Weld coincident positions across material/UV seams without merging UVs.
        position = next((v[:3] for v in vertices if math.dist(position,v[:3]) < .00001),position)
        vertices.append((*position,*uv))
    triangles = [tuple(vertices[i] for i in face) for face in source['indices']]
    return [t if facearea(t)>0 else tuple(reversed(t)) for t in triangles if abs(facearea(t))>1e-8]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('inner',type=Path)
    parser.add_argument('outer',type=Path)
    parser.add_argument('--output',type=Path,default=Path(__file__).resolve().parents[1]/'assets/models/transportation/roads')
    args = parser.parse_args()
    models = []
    for kind,path in [('inner',args.inner),('outer',args.outer)]:
        name = f'road_grid_curve_{kind}_3way'
        models.append((name,fit(read_template(path,name),kind=='inner')))
    args.output.mkdir(parents=True,exist_ok=True)
    for name,triangles in models:
        write(args.output/(name+'.glb'),triangles)


if __name__ == '__main__':
    main()
