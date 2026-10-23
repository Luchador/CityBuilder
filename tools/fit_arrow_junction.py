"""Fit the supplied arrow junction to a diagonal through route and two grid arms.

Run with the ORIGINAL Blender export, not the already fitted game asset.
Python 3 standard library only. Rebuild collision profiles after geometry edits.
"""
import argparse
import math
from pathlib import Path

from fit_slip_junctions import read_template, transform, clip, extend, facearea, write


def fit(source):
    vertices = []
    for (x, y, z), uv in zip(source['vertices'], source['uv']):
        # The shared center vertex of the W mouth is about 0.6 mm off-plane.
        # Snap it before extending; otherwise only the outer lane edges extend.
        if abs(x+24) < .001 and abs(z) <= 4.001:
            x = -24.
        vertices.append((x, 0., z, *uv))
    triangles = [tuple(vertices[i] for i in face) for face in source['indices']]
    triangles = transform(triangles, lambda x, z: ((x-z)/math.sqrt(2), (x+z)/math.sqrt(2)))

    # The original has W/E through and NW/SW branches. Rotating it 45 degrees
    # makes NW/SE through and N/W branches, preserving all pavement widths.
    # The N/W straights are now longer than their owned cells: trim only their
    # outer tails to -24 m, retaining the authored curves and UV interpolation.
    fitted = []
    for triangle in triangles:
        polygon = clip(list(triangle), lambda v: v[0]+24)
        polygon = clip(polygon, lambda v: v[2]+24)
        for i in range(1, len(polygon)-1):
            piece = (polygon[0], polygon[i], polygon[i+1])
            if facearea(piece) > 1e-8:
                fitted.append(piece)

    # Extend the two diagonal mouths to the far seam of the owned NW cell and
    # the ordinary SE seam of the junction cell, respectively.
    fitted = extend(fitted, -1, -1, 24, 24*math.sqrt(2))
    fitted = extend(fitted, 1, 1, 8, 8*math.sqrt(2))
    return fitted


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('template', type=Path)
    parser.add_argument('--node-name', default='road_mixed_4way_grid_sw_se',
                        help='Object name inside the original uploaded GLB')
    parser.add_argument('--output', type=Path,
                        default=Path(__file__).resolve().parents[1]/'assets/models/transportation/roads/road_mixed_4way_arrow.glb')
    args = parser.parse_args()
    triangles = fit(read_template(args.template, args.node_name))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    write(args.output, triangles)


if __name__ == '__main__':
    main()
