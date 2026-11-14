#!/usr/bin/env python3
"""Parametric click-wheel caps, millimetres. CadQuery 2.7 / Python 3.

Front face: XY, Z=0; back of caps: negative Z. STL exports are rigidly
rotated so the front face lies on the build plate (Z=0), pushers upwards.
This is a prototype of the two moving caps, not a complete mechanism.
"""
from __future__ import annotations
import json
import math
import struct
from collections import Counter
from dataclasses import asdict, dataclass
from pathlib import Path

import cadquery as cq
import numpy as np
from matplotlib.font_manager import FontProperties
from matplotlib.textpath import TextPath

ROOT = Path(__file__).resolve().parents[1]


@dataclass
class Parameters:
    wheel_diameter: float = 44.0       # Visible cylindrical face, not hidden lip.
    select_diameter: float = 15.0      # Visible cylindrical face.
    radial_gap: float = 0.20          # EACH SIDE; ring opening is 15.4.
    face_thickness: float = 1.00
    engraving_depth: float = 0.20
    front_chamfer: float = 0.15
    button_radius: float = 16.0       # ASSUMPTION: 4 switch actuator centres.
    pusher_height: float = 2.40       # From back of face to contact tip.
    pusher_tip_diameter: float = 2.40
    pusher_root_diameter: float = 3.60
    ring_lip_overhang: float = 0.80    # Radial, hidden OD = 45.6.
    select_lip_overhang: float = 2.50  # Radial, flat hidden flange OD = 20.0.
    select_guide_depth: float = 2.00   # Total bearing length from front plane.
    select_guide_wall: float = 1.20
    select_axial_clearance: float = .30
    select_flange_thickness: float = .80
    lip_land: float = 0.30
    ring_lip_wall: float = 1.00
    pcb_thickness: float = 1.60        # Reference only; no PCB fabrication file.
    switch_height: float = 0.80        # Unverified user-supplied total height.
    switch_width: float = 4.00
    trial_travel: float = 0.20         # Geometric check only, NOT switch spec.
    copper_gap: float = 1.20
    copper_inner_radius: float = 9.50
    copper_outer_radius: float = 20.80
    copper_pusher_clearance_radius: float = 2.40

    @property
    def opening_diameter(self):
        return self.select_diameter + 2 * self.radial_gap

    @property
    def cap_depth(self):
        return self.face_thickness + self.pusher_height

    @property
    def select_flange_start(self):
        return self.select_guide_depth + self.select_axial_clearance

    @property
    def select_flange_end(self):
        return self.select_flange_start + self.select_flange_thickness

    @property
    def pcb_front_depth(self):
        return self.cap_depth + self.switch_height

    @property
    def reference_stack(self):
        return self.pcb_front_depth + self.pcb_thickness


P = Parameters()


def cylinder(radius, height, x=0.0, y=0.0, z=0.0):
    return cq.Solid.makeCylinder(radius, height, cq.Vector(x, y, z))


def cone(r1, r2, height, x=0.0, y=0.0, z=0.0):
    return cq.Solid.makeCone(r1, r2, height, cq.Vector(x, y, z))


def polygon_prism(points, height, z=0.0):
    return cq.Workplane("XY").workplane(offset=z).polyline(points).close().extrude(height).val()


def icon_polygons(p=P):
    """Identical polygon outlines are embedded into the standalone SCAD file."""
    path = TextPath((0, 0), "MENU", size=2.20,
                    prop=FontProperties(family="DejaVu Sans", weight="bold"))
    bb = path.get_extents()
    menu = [np.asarray(poly) - [(bb.x0 + bb.x1) / 2, (bb.y0 + bb.y1) / 2]
            for poly in path.to_polygons()]
    icons = [(poly + [0, p.button_radius]).tolist() for poly in menu]
    # NEXT: two right-pointing triangles and a stop bar.
    # Positive gaps prevent non-manifold point contacts between engraved cavities.
    nxt = [np.array([[-1.70, -.90], [-1.70, .90], [-.35, 0]]),
           np.array([[-.10, -.90], [-.10, .90], [1.25, 0]]),
           np.array([[1.45, -.90], [1.85, -.90], [1.85, .90], [1.45, .90]])]
    icons += [(poly + [p.button_radius, 0]).tolist() for poly in nxt]
    icons += [(poly * [-1, 1] + [-p.button_radius, 0]).tolist() for poly in nxt]
    play = [np.array([[-1.70, -.95], [-1.70, .95], [-.20, 0]]),
            np.array([[.10, -.95], [.55, -.95], [.55, .95], [.10, .95]]),
            np.array([[.95, -.95], [1.40, -.95], [1.40, .95], [.95, .95]])]
    icons += [(poly + [0, -p.button_radius]).tolist() for poly in play]
    return icons, [poly.tolist() for poly in menu], [poly.tolist() for poly in nxt], [poly.tolist() for poly in play]


def validate_parameters(p):
    assert p.face_thickness - p.engraving_depth >= .79
    assert 0 < p.radial_gap < p.select_lip_overhang
    assert p.ring_lip_overhang + p.lip_land < p.pusher_height
    assert p.select_guide_depth > p.face_thickness
    assert p.select_flange_end < p.cap_depth
    assert p.select_axial_clearance > p.trial_travel
    assert p.select_lip_overhang > p.radial_gap + p.select_guide_wall
    assert p.copper_inner_radius > p.opening_diameter/2 + p.select_guide_wall
    assert p.button_radius - p.pusher_root_diameter / 2 > p.opening_diameter / 2
    assert p.button_radius + p.pusher_root_diameter / 2 < p.wheel_diameter / 2 - p.ring_lip_wall


def build_caps(p=P, retain=True, engraved=True):
    """Build in positive depth, then reflect once to the front-up CAD frame."""
    validate_parameters(p)
    ro = p.wheel_diameter / 2
    ri = p.opening_diameter / 2
    rs = p.select_diameter / 2
    t = p.face_thickness
    ring = cq.Workplane("XY").circle(ro).circle(ri).extrude(t)
    ring = ring.faces("<Z").edges().chamfer(p.front_chamfer).val()
    select = cq.Workplane("XY").circle(rs).extrude(t)
    select = select.faces("<Z").edges().chamfer(p.front_chamfer).val()

    if retain:
        h = p.ring_lip_overhang
        rim = cone(ro, ro + h, h, z=t).fuse(cylinder(ro + h, p.lip_land, z=t + h))
        rim = rim.cut(cylinder(ro - p.ring_lip_wall, h + p.lip_land + .02, z=t - .01))
        ring = ring.fuse(rim)
        # Straight bearing sleeve on the moving ring, with a flat stop below it.
        sleeve = cylinder(ri+p.select_guide_wall, p.select_guide_depth-t, z=t)
        sleeve = sleeve.cut(cylinder(ri, p.select_guide_depth-t+.02, z=t-.01))
        ring = ring.fuse(sleeve)
        select = select.fuse(cylinder(rs, p.select_flange_start-t, z=t))
        select = select.fuse(cylinder(rs+p.select_lip_overhang,
                                     p.select_flange_thickness,z=p.select_flange_start))
        select = select.fuse(cylinder(p.pusher_tip_diameter / 2,
                                      p.cap_depth-p.select_flange_end,
                                      z=p.select_flange_end))
    else:
        select = select.fuse(make_pusher(0, 0, p))

    for x, y in [(0, p.button_radius), (p.button_radius, 0), (0, -p.button_radius), (-p.button_radius, 0)]:
        ring = ring.fuse(make_pusher(x, y, p))

    if engraved:
        for poly in icon_polygons(p)[0]:
            ring = ring.cut(polygon_prism(poly, p.engraving_depth + .01, -.01))

    return ring.clean().mirror("XY"), select.clean().mirror("XY")


def make_pusher(x, y, p=P):
    r0, r1 = p.pusher_root_diameter / 2, p.pusher_tip_diameter / 2
    taper = r0 - r1
    return cone(r0, r1, taper, x, y, p.face_thickness).fuse(
        cylinder(r1, p.pusher_height - taper, x, y, p.face_thickness + taper))


def build_fit_test(p=P):
    """1/2/3 dot marks indicate holes 15.2 / 15.4 / 15.6, depth 2.0."""
    part=cq.Workplane('XY').rect(64,23).extrude(p.select_guide_depth).val()
    for index,(x,diameter) in enumerate([(-21,15.2),(0,15.4),(21,15.6)],1):
        part=part.cut(cylinder(diameter/2,p.select_guide_depth+.2,x,0,-.1))
        for k in range(index):
            part=part.cut(cylinder(.5,.4,x+(k-(index-1)/2)*1.6,-9.5,p.select_guide_depth-.3))
    return part.clean().mirror('XY')


def print_pose(shape):
    return shape.rotate((0, 0, 0), (1, 0, 0), 180)


def stl_stats(path):
    """Check the exported mesh, not just the BREP that produced it."""
    b = path.read_bytes()
    n = struct.unpack_from("<I", b, 80)[0]
    assert len(b) == 84 + 50 * n, (path.name, "unexpected STL layout")
    typ = np.dtype([("normal", "<f4", (3,)), ("vertex", "<f4", (3, 3)), ("attr", "<u2")])
    tri = np.frombuffer(b, typ, n, 84)["vertex"].astype(float)
    verts, inverse = np.unique(np.round(tri.reshape(-1, 3), 5), axis=0, return_inverse=True)
    faces = inverse.reshape(-1, 3)
    edges = Counter()
    directed = Counter()
    for face in faces:
        assert len(set(face)) == 3, (path.name, "degenerate triangle")
        for a, z in zip(face, np.roll(face, -1)):
            edges[tuple(sorted((int(a), int(z))))] += 1
            directed[(int(a), int(z))] += 1
    assert all(v == 2 for v in edges.values()), (path.name, "not watertight")
    assert all(directed[(z, a)] == 1 for a, z in directed), (path.name, "inconsistent winding")
    volume = np.einsum("ij,ij->i", tri[:, 0], np.cross(tri[:, 1], tri[:, 2])).sum() / 6
    assert volume > 0, (path.name, "inward normals")
    assert abs(verts[:, 2].min()) < 1e-5
    return {"triangles": n, "watertight": True, "consistent_winding": True,
            "mesh_volume_mm3": round(float(volume), 3),
            "bounds_mm": np.round([verts.min(axis=0), verts.max(axis=0)], 4).tolist()}


def make_copper(p=P):
    """Six constant-width insulating gaps and clearance around pusher roots."""
    layer = cq.Workplane("XY").circle(p.copper_outer_radius).circle(p.copper_inner_radius).extrude(.10).val()
    for k in range(6):
        slit = cq.Workplane("XY").box(24, p.copper_gap, .30).val().translate((12, 0, .05))
        slit = slit.rotate((0, 0, 0), (0, 0, 1), k * 60)
        layer = layer.cut(slit)
    for x, y in [(0, p.button_radius), (p.button_radius, 0), (0, -p.button_radius), (-p.button_radius, 0)]:
        layer = layer.cut(cylinder(p.copper_pusher_clearance_radius, .30, x, y, -.10))
    assert len(layer.Solids()) == 6, "Electrode template must have six isolated sectors"
    return layer.clean().mirror("XY").translate((0, 0, -p.face_thickness))


def write_electrode_svg(layer, p=P):
    # Extract exact OCCT edge approximations, preserving full-scale dimensions.
    from xml.sax.saxutils import escape
    paths = []
    solids = sorted(layer.Solids(), key=lambda s: math.atan2(s.Center().y, s.Center().x))
    for i, solid in enumerate(solids):
        face = max([f for f in solid.Faces() if abs(f.normalAt().z) > .9], key=lambda f: f.Center().z)
        commands = []
        for wire in face.Wires():
            pts, _ = wire.sample(.01)
            commands.append("M " + " L ".join(f"{50+v.x:.5f},{48-v.y:.5f}" for v in pts) + " Z")
        paths.append(f'<path d="{" ".join(commands)}" fill="#d99058" stroke="#3e291a" stroke-width="0.08" fill-rule="evenodd"/>')
        c = solid.Center()
        paths.append(f'<text x="{50+c.x:.4f}" y="{48-c.y:.4f}" text-anchor="middle" font-size="2.0" fill="#241d19">E{i+1}</text>')
    svg = f'''<svg xmlns="http://www.w3.org/2000/svg" width="100mm" height="92mm" viewBox="0 0 100 92">
<rect width="100" height="92" fill="white"/>
<g font-family="DejaVu Sans, sans-serif" fill="#222">
<text x="8" y="9" font-size="3.2" font-weight="bold">COPPER / 6 SECTORS / 1:1</text>
<text x="8" y="15" font-size="2.5">Gap {p.copper_gap:.1f} mm · print at 100%, no fit-to-page</text>
{''.join(paths)}
<path d="M 10 79 H 30 M 10 77 V 81 M 30 77 V 81" stroke="#222" stroke-width=".2" fill="none"/>
<text x="20" y="85" font-size="2.5" text-anchor="middle">20 mm check</text>
<text x="42" y="81" font-size="2.2">Cut copper only; no wiring supplied.</text>
<text x="42" y="86" font-size="2.2">Roots cleared by R{p.copper_pusher_clearance_radius:.1f} mm.</text>
</g></svg>'''
    (ROOT / "reference/electrodes_1to1.svg").write_text(svg, encoding="utf-8")


def scad_source(p=P):
    _, menu, nxt, play = icon_polygons(p)
    return f'''// Click-wheel prototype v0.2. Units: mm. 2 MOVING CAPS ONLY.
// Coordinates of PCB mounting holes are unknown; no M2 holes in moving caps.
// Main STL files include hidden lips: wheel 45.6, SELECT 20.0 overall OD.
// SELECT has a FLAT flange: supports under its overhang are required in face-down FDM printing.
// F6 -> Export STL. Modes: "print", "assembled", "ring", "select".
mode = "print";
retention_lips = true;
engraved_icons = true;

wheel_diameter = {p.wheel_diameter};
select_diameter = {p.select_diameter};
radial_gap = {p.radial_gap};  // PER SIDE
face_thickness = {p.face_thickness};
engraving_depth = {p.engraving_depth};
front_chamfer = {p.front_chamfer};
button_radius = {p.button_radius}; // ASSUMED; measure PCB!
pusher_height = {p.pusher_height}; // ASSUMED; measure vertical stack!
pusher_tip_diameter = {p.pusher_tip_diameter};
pusher_root_diameter = {p.pusher_root_diameter};
ring_lip_overhang = {p.ring_lip_overhang};
select_lip_overhang = {p.select_lip_overhang};
select_guide_depth = {p.select_guide_depth};
select_guide_wall = {p.select_guide_wall};
select_axial_clearance = {p.select_axial_clearance};
select_flange_thickness = {p.select_flange_thickness};
lip_land = {p.lip_land};
ring_lip_wall = {p.ring_lip_wall};
$fn = 360;
eps = 0.01;
opening_diameter = select_diameter + 2*radial_gap;
cap_depth = face_thickness + pusher_height;
select_flange_start = select_guide_depth + select_axial_clearance;
select_flange_end = select_flange_start + select_flange_thickness;
assert(face_thickness-engraving_depth >= 0.799);
assert(radial_gap < select_lip_overhang);
assert(pusher_height > ring_lip_overhang+lip_land);
assert(cap_depth > select_flange_end);
assert(select_axial_clearance > {p.trial_travel});

menu_paths = {json.dumps(menu, separators=(',', ':'))};
next_paths = {json.dumps(nxt, separators=(',', ':'))};
play_paths = {json.dumps(play, separators=(',', ':'))};

module polys(paths) {{ for(p=paths) polygon(p); }}
module icon_paths() {{
    translate([0,button_radius]) polys(menu_paths);
    translate([button_radius,0]) polys(next_paths);
    translate([-button_radius,0]) mirror([1,0]) polys(next_paths);
    translate([0,-button_radius]) polys(play_paths);
}}
module pusher() {{
    taper = (pusher_root_diameter-pusher_tip_diameter)/2;
    translate([0,0,face_thickness]) {{
        cylinder(h=taper,d1=pusher_root_diameter,d2=pusher_tip_diameter);
        translate([0,0,taper]) cylinder(h=pusher_height-taper,d=pusher_tip_diameter);
    }}
}}
module annular_face() {{
    r=wheel_diameter/2; ri=opening_diameter/2; c=front_chamfer;
    rotate_extrude() polygon([[ri+c,0],[r-c,0],[r,c],[r,face_thickness],
                              [ri,face_thickness],[ri,c]]);
}}
module ring_depth() {{
    difference() {{
        union() {{
            annular_face();
            if(retention_lips) translate([0,0,face_thickness]) difference() {{
                cylinder(h=select_guide_depth-face_thickness,d=opening_diameter+2*select_guide_wall);
                translate([0,0,-eps]) cylinder(h=select_guide_depth-face_thickness+2*eps,d=opening_diameter);
            }}
            if(retention_lips) translate([0,0,face_thickness]) difference() {{
                union() {{
                    cylinder(h=ring_lip_overhang,d1=wheel_diameter,d2=wheel_diameter+2*ring_lip_overhang);
                    translate([0,0,ring_lip_overhang]) cylinder(h=lip_land,d=wheel_diameter+2*ring_lip_overhang);
                }}
                translate([0,0,-eps]) cylinder(h=ring_lip_overhang+lip_land+2*eps,d=wheel_diameter-2*ring_lip_wall);
            }}
            for(pos=[[0,button_radius],[button_radius,0],[0,-button_radius],[-button_radius,0]])
                translate([pos[0],pos[1],0]) pusher();
        }}
        if(engraved_icons) translate([0,0,-eps]) linear_extrude(engraving_depth+eps) icon_paths();
    }}
}}
module select_depth() {{
    r=select_diameter/2; c=front_chamfer;
    rotate_extrude() polygon([[0,0],[r-c,0],[r,c],[r,face_thickness],[0,face_thickness]]);
    if(retention_lips) {{
        translate([0,0,face_thickness]) cylinder(h=select_flange_start-face_thickness,d=select_diameter);
        translate([0,0,select_flange_start]) cylinder(h=select_flange_thickness,d=select_diameter+2*select_lip_overhang);
        translate([0,0,select_flange_end]) cylinder(h=cap_depth-select_flange_end,d=pusher_tip_diameter);
    }}
    else pusher();
}}
// CAD front up. Use rigid rotations (not a mirrored part) for print orientation.
module ring() {{ mirror([0,0,1]) ring_depth(); }}
module select_cap() {{ mirror([0,0,1]) select_depth(); }}
if(mode=="print") {{
    translate([-15,0,0]) rotate([180,0,0]) ring();
    translate([22,0,0]) rotate([180,0,0]) select_cap();
}} else if(mode=="assembled") {{
    ring(); select_cap();
}} else if(mode=="ring") rotate([180,0,0]) ring();
else if(mode=="select") rotate([180,0,0]) select_cap();
'''


def main():
    for folder in ["stl", "cad", "reference"]:
        (ROOT / folder).mkdir(exist_ok=True)
    validate_parameters(P)
    ring, select = build_caps()
    plain, _ = build_caps(engraved=False)
    parts = {"wheel_44": ring, "select_15": select, "wheel_44_plain": plain,
             "select_fit_test": build_fit_test()}
    report = {"units": "mm", "prototype": "0.2", "parameters": asdict(P),
              "derived": {"opening_diameter": P.opening_diameter,
                          "cap_depth": P.cap_depth, "reference_stack": P.reference_stack,
                          "select_radial_retention_overlap": P.select_lip_overhang-P.radial_gap,
                          "select_flange_start":P.select_flange_start,
                          "select_flange_end":P.select_flange_end},
              "parts": {}, "collision_tests": {}}
    for name, part in parts.items():
        assert part.isValid() and len(part.Solids()) == 1, name
        path = ROOT / "stl" / f"{name}.stl"
        ok = print_pose(part).exportStl(str(path), tolerance=.01, angularTolerance=.07,
                                      relative=False, parallel=True)
        assert ok
        report["parts"][name] = {"brep_valid": True, "solids": 1,
                                  "brep_volume_mm3": round(part.Volume(), 3), **stl_stats(path)}
        if name in ("wheel_44", "select_15"):
            cq.exporters.export(part, str(ROOT / "cad" / f"{name}.step"))

    for tag, a, b in [
        ("rest", ring, select),
        ("select_down_0p2", ring, select.translate((0, 0, -P.trial_travel))),
        ("ring_down_0p2", ring.translate((0, 0, -P.trial_travel)), select),
        ("select_side_shift_0p15", ring, select.translate((.15,0,0))),
        ("select_up_to_stop", ring, select.translate((0,0,P.select_axial_clearance))),
    ]:
        v = a.intersect(b).Volume()
        assert v < 1e-6, (tag, v)
        report["collision_tests"][tag] = {"intersection_mm3": round(v, 9)}
    angle = math.degrees(math.atan(P.trial_travel / (2 * P.button_radius)))
    for k in range(4):
        tilt = ring.rotate((0, -P.button_radius, -P.cap_depth),
                           (1, -P.button_radius, -P.cap_depth), -angle)
        tilt = tilt.rotate((0, 0, 0), (0, 0, 1), k * 90)
        v = tilt.intersect(select).Volume()
        assert v < 1e-6, ("tilt", k, v)
        report["collision_tests"][f"ring_rock_{k*90}deg"] = {"tilt_angle_deg": round(angle, 5),
                                                               "intersection_mm3": round(v, 9)}

    # A flange crossing the sleeve is blocked by real material, not just a nominal OD.
    blocked=ring.intersect(select.translate((0,0,P.select_axial_clearance+.10))).Volume()
    assert blocked > 1
    report['retention_check']={'straight_withdrawal_blocked':True,
                              'intersection_after_stop_plus_0p1_mm3':round(blocked,6),
                              'nominal_upward_float_mm':P.select_axial_clearance}

    assembly = cq.Assembly(name="clickwheel_caps_v02_mm")
    assembly.add(ring, name="wheel_44_visible_45p6_hidden", color=cq.Color(.87, .88, .90))
    assembly.add(select, name="select_15_visible_20_hidden", color=cq.Color(.79, .81, .84))
    assembly.export(str(ROOT / "cad/caps_assembly.step"), unit="MM")

    copper = make_copper()
    write_electrode_svg(copper)
    (ROOT / "source/clickwheel.scad").write_text(scad_source(), encoding="utf-8")
    (ROOT / "source/parameters.json").write_text(json.dumps(asdict(P), indent=2)+"\n", encoding="utf-8")
    (ROOT / "reference/geometry_check.json").write_text(json.dumps(report, indent=2)+"\n", encoding="utf-8")
    print(json.dumps({"parts": len(parts), "valid_single_solids": True, "watertight_stls": True,
                      "collision_checks": len(report["collision_tests"]),
                      "cap_depth_mm": P.cap_depth, "reference_stack_mm": P.reference_stack}))


if __name__ == "__main__":
    main()
