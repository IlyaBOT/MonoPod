"""MonoPod enclosure v0.4: protected jack, cable well and flat flush acrylic.

Assembly coordinates match Click Wheel: X right, Y up, front Z=0,
rear Z<0. Helpers use positive depth D=-Z. STL poses use rigid rotations
and translations only; original Click Wheel files are never regenerated.
"""
from __future__ import annotations
from pathlib import Path
from dataclasses import dataclass
import argparse
import copy
import csv
import hashlib
import json
import math
import sys
import time
import cadquery as cq
import numpy as np
import trimesh

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
EPS = 0.02


def box(w, h, d1, d2, x=0., y=0.):
    assert w > 0 and h > 0 and d2 > d1, (w, h, d1, d2)
    return cq.Workplane('XY').box(w, h, d2-d1, centered=(True, True, False)).val().translate((x, y, -d2))


def rrbox(w, h, d1, d2, r, x=0., y=0.):
    result = cq.Workplane(obj=box(w, h, d1, d2))
    if r > 0: result = result.edges('|Z').fillet(min(r, w/2-0.01, h/2-0.01))
    return result.val().translate((x, y, 0))


def cyl(r, d1, d2, x=0., y=0.):
    return cq.Solid.makeCylinder(r, d2-d1, cq.Vector(x, y, -d2))


def ycyl(r, y1, y2, x=0., d=0.):
    return cq.Solid.makeCylinder(r, y2-y1, cq.Vector(x, y1, -d), cq.Vector(0, 1, 0))


def ypill(w, h, y1, y2, x, d, r=0.):
    return rrbox(w, h, 0, y2-y1, r).rotate((0, 0, 0), (1, 0, 0), -90).translate((x, y2, -d))


def join(parts):
    result = parts[0]
    for part in parts[1:]: result = result.fuse(part)
    return result.clean()


def compound(parts):
    return cq.Compound.makeCompound(parts)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def bounds(shape):
    b = shape.BoundingBox()
    return np.array([[b.xmin, b.ymin, b.zmin], [b.xmax, b.ymax, b.zmax]])


def volume_overlap(a, b):
    # Bounding-box rejection avoids expensive unrelated boolean operations.
    ba, bb = bounds(a), bounds(b)
    if np.any(np.minimum(ba[1], bb[1])-np.maximum(ba[0], bb[0]) <= 1e-8): return 0.
    return a.intersect(b).Volume()


@dataclass
class Part:
    shape: cq.Shape
    pose: str
    group: str
    note: str = ''


class Model:
    def __init__(self, params, root=ROOT):
        self.p = params
        self.root = Path(root)
        self.w = json.loads((self.root/'reference/clickwheel/source/parameters.json').read_text(encoding='utf-8'))
        self.parts: dict[str, Part] = {}
        self.refs: dict[str, cq.Shape] = {}
        self.groups = {}

    def add(self, name, shape, pose='front_down', group='coupon', note=''):
        shape = shape.clean()
        if not shape.isValid() or len(shape.Solids()) != 1 or shape.Volume() <= 0:
            raise ValueError(f'{name}: invalid or disconnected printed part; solids={len(shape.Solids())}')
        self.parts[name] = Part(shape, pose, group, note)
        return shape

    def wheel_mounts(self):
        q = self.p['wheel']; half = self.w['case_hole_half_pitch']
        return [(q['x']+sx*half, q['y']+sy*half) for sx in [-1, 1] for sy in [-1, 1]]

    def display_mounts(self):
        q = self.p['display']
        angle = math.radians(q['rotation_degrees']); co, si = math.cos(angle), math.sin(angle)
        return [(q['x']+co*sx*q['hole_pitch_x']/2-si*sy*q['hole_pitch_y']/2,
                 q['y']+si*sx*q['hole_pitch_x']/2+co*sy*q['hole_pitch_y']/2)
                for sx in [-1, 1] for sy in [-1, 1]]

    def board_datums(self):
        # ONE parent transform. The protected jack positions the complete board;
        # connector offsets are never recomputed independently from the case.
        p=self.p; b=p['mainboard']; c=p['case']; j=p['jack']
        bottom=-c['height']/2+b['jack_tip_recess']+j['tip_beyond_board']
        rear_solder=p['wheel']['face_recess']+b['stack_to_wheel_face']
        front=rear_solder-b['rear_solder_height']-b['thickness']
        return dict(left=b['x']-b['width']/2,right=b['x']+b['width']/2,
            bottom=bottom,top=bottom+b['height'],x=b['x'],y=bottom+b['height']/2,
            front=front,rear=front+b['thickness'],solder_rear=rear_solder)

    def mainboard_mounts(self):
        d=self.board_datums()
        return [(d['left']+x,d['bottom']+y) for x,y in self.p['mainboard']['mount_from_left_bottom']]

    def jack_datums(self):
        p=self.p; b=self.board_datums(); q=p['jack']; usb=self.charger_datums()
        x=usb['x']+p['tp4056']['usb_width']/2+q['gap_from_usb_right']+q['thread_diameter']/2
        tip=b['bottom']-q['tip_beyond_board']; shoulder=tip+q['thread_length']
        base=b['front']-q['mount_standoff']; axis=base-q['axis_above_body_bottom']
        return dict(x=x,tip_y=tip,shoulder_y=shoulder,base_depth=base,axis_depth=axis,
            panel_outer_y=shoulder-q['panel_thickness'])

    def insert_mounts(self):
        q=self.p['connector_insert']
        return [(q['x']-q['width']/2+offset,q['mount_depth']) for offset in q['mount_from_left']]

    def display_shape(self, width, height, d1, d2, radius=0, y_offset=0):
        q = self.p['display']
        return rrbox(width, height, d1, d2, radius, y=y_offset).rotate((0,0,0),(0,0,1),q['rotation_degrees']).translate((q['x'],q['y'],0))

    def acrylic_datums(self):
        q, a = self.p['display'], self.p['acrylic']
        front = a['face_recess'] if a['mount_style']=='flush_carrier' else a['front_lip_thickness']+a['adhesive_gap']
        result={'width':q['window_width']+2*a['overlap'], 'height':q['window_height']+2*a['overlap'],
            'front_depth':front, 'rear_depth':front+a['thickness'],
            'display_air_gap':q['pcb_front_depth']-q['glass_above_pcb']-front-a['thickness']}
        if a['mount_style']=='flush_carrier':
            r=self.p['lens_carrier']; result.update(
                carrier_front=front+a['thickness']+a['adhesive_gap'],
                carrier_rear=front+a['thickness']+a['adhesive_gap']+r['frame_thickness'],
                carrier_ear_front=q['pcb_front_depth']-r['ear_thickness'],
                clear_width=result['width']-2*r['bond_width'],clear_height=result['height']-2*r['bond_width'])
        return result

    def acrylic_pad_centres(self):
        q, a = self.p['display'], self.p['acrylic']
        if a['mount_style']=='flush_carrier': return []
        local = [(sx*(q['window_width']/2+a['overlap']/2), sy*q['window_height']/4)
                 for sx in [-1,1] for sy in [-1,1]]
        local += [(sx*q['window_width']/4, sy*(q['window_height']/2+a['overlap']/2))
                  for sx in [-1,1] for sy in [-1,1]]
        angle=math.radians(q['rotation_degrees']); co,si=math.cos(angle),math.sin(angle)
        return [(q['x']+co*x-si*(y+q['glass_y_offset']), q['y']+si*x+co*(y+q['glass_y_offset'])) for x,y in local]

    def acrylic_opening(self, shape, pocket_gap=None):
        p, q, a = self.p, self.p['display'], self.p['acrylic']; lens=self.acrylic_datums()
        gap=a['pocket_side_clearance'] if pocket_gap is None else pocket_gap
        if a['mount_style']=='flush_carrier':
            shape=shape.cut(self.display_shape(lens['width']+2*gap,lens['height']+2*gap,
                -EPS,p['case']['front_thickness']+EPS,a['corner_radius']+gap,q['glass_y_offset']))
            r=p['lens_carrier']; radius=q['boss_diameter']/2+r['boss_side_clearance']+r['sleeve_wall']+r['panel_pocket_clearance']
            start=lens['carrier_front']-r['panel_pocket_axial_clearance']
            for x,y in self.display_mounts():
                shape=shape.cut(cyl(radius,start,p['case']['front_thickness']+EPS,x,y))
            return shape.clean()
        shape=shape.cut(self.display_shape(q['window_width'],q['window_height'],-EPS,
            p['case']['front_thickness']+EPS,q['window_radius'],q['glass_y_offset']))
        # Open the larger pocket from the REAR. The continuous front bezel is
        # printed first, so this rebate introduces no unsupported inward shelf.
        shape=shape.cut(self.display_shape(lens['width']+2*gap,lens['height']+2*gap,
            a['front_lip_thickness'],p['case']['front_thickness']+EPS,
            a['corner_radius']+gap,q['glass_y_offset']))
        for x,y in self.acrylic_pad_centres():
            shape=shape.fuse(cyl(a['stop_pad_diameter']/2,a['front_lip_thickness']-EPS,lens['front_depth'],x,y))
        return shape.clean()

    def lens_carrier(self):
        p=self.p; q=p['display']; a=p['acrylic']; r=p['lens_carrier']; d=self.acrylic_datums()
        assert a['mount_style']=='flush_carrier'
        low,high=d['carrier_front'],d['carrier_rear']; ear=d['carrier_ear_front']
        inner=q['boss_diameter']/2+r['boss_side_clearance']; outer=inner+r['sleeve_wall']
        shape=self.display_shape(d['width'],d['height'],low,high,a['corner_radius'],q['glass_y_offset'])
        for x,y in self.display_mounts():
            shape=shape.fuse(cyl(outer,low,ear,x,y))
            shape=shape.cut(cyl(inner,low-EPS,ear+EPS,x,y))
            shape=shape.fuse(cyl(outer,ear, q['pcb_front_depth'],x,y))
            shape=shape.cut(cyl(p['fasteners']['m25_clearance']/2,ear-EPS,q['pcb_front_depth']+EPS,x,y))
        shape=shape.cut(self.display_shape(d['clear_width'],d['clear_height'],low-EPS,high+EPS,
            r['inner_corner_radius'],q['glass_y_offset']))
        # Keep the 0.8-mm bond ring in front of the glass, and notch the
        # mounting sleeves around the actual declared glass envelope.
        shape=shape.cut(self.display_shape(q['glass_width']+2*r['glass_side_clearance'],
            q['glass_height']+2*r['glass_side_clearance'],high, q['pcb_front_depth']+EPS,0,q['glass_y_offset']))
        return shape.clean()

    def lens_gluing_jig(self):
        p=self.p; a=p['acrylic']; r=p['lens_carrier']; d=self.acrylic_datums()
        shape=self.display_shape(d['width']+2*r['jig_border'],d['height']+2*r['jig_border'],
            -r['jig_base_thickness'],0,a['corner_radius']+r['jig_border'])
        raised=self.display_shape(d['width']-2*r['jig_lens_side_clearance'],d['height']-2*r['jig_lens_side_clearance'],
            -EPS,d['front_depth'],a['corner_radius'])
        return shape.fuse(raised).clean()

    def usb_cable_datums(self):
        p=self.p; u=self.charger_datums(); q=p['usb_cable']; outer=-p['case']['height']/2
        body_front=u['mouth_y']+q['engagement_depth']-q['metal_tip_length']
        floor=body_front+q['axial_clearance']; rear=floor+q['well_wall']
        return dict(body_front_y=body_front,body_rear_y=body_front-q['body_length'],
            metal_tip_y=u['mouth_y']+q['engagement_depth'],well_floor_y=floor,well_rear_y=rear,
            well_depth=floor-outer,well_width=q['body_width']+2*q['side_clearance'],
            well_height=q['body_height']+2*q['side_clearance'],usb_recess=u['mouth_y']-outer,
            pcb_clearance=u['pcb_front_y']-rear,corner_radius=q['well_corner_radius'])

    def check_parameters(self):
        p = self.p; c, w, f, lcd, h, j = (p[n] for n in ['case','wheel','fasteners','display','hold','jack'])
        seat = w['face_recess']+self.w['case_lug_front_depth']
        engagement = f['wheel_screw']-self.w['case_lug_thickness']
        a=p['acrylic']; lens=self.acrylic_datums()
        checks = {
            'wheel_blind_floor': seat-w['pilot_depth'] >= f['minimum_blind_floor']-1e-8,
            'wheel_screw_end_clearance': w['pilot_depth'] > engagement+0.1,
            'closure_screw_end_clearance': c['closure_pilot_depth'] > f['closure_screw']-c['rear_thickness']-c['closure_end_gap']+0.1,
            'hold_blind_floor': c['wall']+h['standoff_height']-h['pilot_depth'] >= f['minimum_blind_floor']-1e-8,
            'wheel_ring_front_skin': w['face_recess']+self.w['frame_front_depth']-w['ring_axial_clearance'] >= 0.6,
            'display_glass_behind_panel': lcd['pcb_front_depth']-lcd['glass_above_pcb'] >= c['front_thickness']+0.1-1e-8,
            'display_window_inside_glass': lcd['window_width'] < lcd['glass_width'] and lcd['window_height'] < lcd['glass_height'],
            'case_wall_at_least_four_lines': c['wall'] >= 1.6,
            'hold_neck_side_wall': (h['neck_width_z']-h['lever_width_z']-2*h['lever_gap'])/2 >= 0.8,
            'hold_neck_end_wall': (h['neck_length_x']-h['lever_width_x']-2*h['lever_gap'])/2 >= 0.8,
            'jack_full_nut_on_thread': j['thread_length']-j['panel_thickness'] >= j['nut_thickness'],
            'mainboard_boss_solder_clearance': c['depth']-c['rear_thickness']-self.board_datums()['solder_rear'] >= 1,
            'board_mounts_inside_board': all(2<=x<=p['mainboard']['width']-2 and 2<=y<=p['mainboard']['height']-2 for x,y in p['mainboard']['mount_from_left_bottom']),
            'insert_pilot_tip_reserve': p['connector_insert']['pilot_depth'] >= f['connector_insert_screw']-p['connector_insert']['wall_thickness']+0.2,
            'insert_pilot_blind_end': p['connector_insert']['boss_length']-p['connector_insert']['pilot_depth'] >= 0.5-1e-8,
            'insert_boss_minimum_wall': (p['connector_insert']['boss_diameter']-f['m2_pilot'])/2 >= 1.2,
            'mainboard_screw_engagement': p['mainboard']['pilot_depth'] > f['mainboard_screw']-p['mainboard']['thickness']+0.1,
            'insert_jack_seat_inside_wall': self.jack_datums()['panel_outer_y'] >= -c['height']/2-1e-8,
            'insert_seat_annulus': (j['seat_outer_diameter']-j['nut_access_diameter'])/2 >= 0.8,
            'acrylic_front_lip_printable': 0.6 <= a['front_lip_thickness'] < c['front_thickness'],
            'acrylic_nonnegative_glue_and_side_gap': a['adhesive_gap'] >= 0 and a['pocket_side_clearance'] >= 0,
            'acrylic_positive_sheet_thickness': a['thickness'] > 0,
            'acrylic_air_gap_to_display': lens['display_air_gap'] >= a['minimum_display_air_gap']-1e-8,
            'acrylic_stop_pads_on_ledge': a['overlap']/2-a['stop_pad_diameter']/2 >= a['adhesive_inner_margin'],
            'acrylic_lens_corner_radius': 0 < a['corner_radius'] < min(lens['width'],lens['height'])/2,
            'acrylic_lens_fits_stock': max(lens['width'],lens['height'])+2*a['sheet_margin'] <= a['stock_width'] and min(lens['width'],lens['height'])+2*a['sheet_margin'] <= a['stock_height'],
        }
        cable=self.usb_cable_datums(); v=p['usb_cable']
        checks.update({
            'protected_jack_tip': p['mainboard']['jack_tip_recess'] >= .5,
            'usb_hood_well_has_depth': cable['well_depth'] > .2,
            'usb_well_web_printable': v['well_wall'] >= 1.6,
            'usb_well_clear_of_charger_pcb': cable['pcb_clearance'] >= .2-1e-8,
            'usb_hood_side_allowance': v['side_clearance'] >= v['measurement_size_allowance']/2+.15,
            'usb_metal_engagement_within_tip': 0 < v['engagement_depth'] <= v['metal_tip_length'],
            'usb_axial_measurement_reserve': v['axial_clearance'] >= v['tip_length_allowance']+.1,
        })
        if a['mount_style']=='flush_carrier':
            r=p['lens_carrier']; engage=f['display_screw']-lcd['pcb_thickness']-r['ear_thickness']
            checks.update({
                'flush_lens_behind_face': a['face_recess'] >= 0,
                'carrier_thickness_printable': r['frame_thickness'] >= .8 and r['ear_thickness'] >= .8,
                'carrier_glass_air_gap': lcd['pcb_front_depth']-lcd['glass_above_pcb']-lens['carrier_rear'] >= .3-1e-8,
                'carrier_optical_opening': lens['clear_width']>lcd['active_width'] and lens['clear_height']>lcd['active_height'],
                'carrier_bond_land': 1.6<=r['bond_width']<=a['overlap'],
                'carrier_screw_engagement': engage>=2 and r['display_pilot_depth']>engage+.1,
                'carrier_pilot_blind_floor': lens['carrier_ear_front']-r['display_pilot_depth']>=f['minimum_blind_floor'],
                'carrier_sleeve_height': lens['carrier_ear_front']>lens['carrier_rear']+.8,
            })
        bad = [key for key, passed in checks.items() if not passed]
        if bad: raise ValueError('Parameter conflict: '+', '.join(bad))
        return checks

    def wheel_interface(self, shape, gap=None, step_extra=None):
        p, q, w = self.p, self.p['wheel'], self.w
        gap = q['radial_gap'] if gap is None else gap
        extra = q['ring_axial_clearance'] if step_extra is None else step_extra
        seat = q['face_recess']+w['case_lug_front_depth']
        depth = max(p['case']['front_thickness']+p['case']['lip_height'], seat)+EPS
        # Clear the ring and lugs before adding the four precise datum pads.
        shape = shape.cut(cyl(w['frame_outer_radius']+q['ring_radial_clearance'],
            q['face_recess']+w['frame_front_depth']-extra, depth, q['x'], q['y']))
        for x, y in self.wheel_mounts():
            shape = shape.cut(cyl(q['lug_radius']+q['lug_radial_clearance'], seat, depth, x, y))
            shape = shape.fuse(cyl(q['boss_diameter']/2, 0, seat, x, y))
            shape = shape.cut(cyl(p['fasteners']['m25_pilot']/2, seat-q['pilot_depth'], seat+EPS, x, y))
        return shape.cut(cyl(w['wheel_diameter']/2+gap, -EPS, depth, q['x'], q['y'])).clean()

    def display_interface(self, shape, pocket_gap=None):
        p, q = self.p, self.p['display']; t = p['case']['front_thickness']
        if p['acrylic']['mount_style']=='flush_carrier':
            r=p['lens_carrier']; d=self.acrylic_datums()
            shape=self.acrylic_opening(shape,pocket_gap)
            seat=d['carrier_ear_front']; start=d['carrier_front']-r['panel_pocket_axial_clearance']-EPS
            for x,y in self.display_mounts():
                shape=shape.fuse(cyl(q['boss_diameter']/2,start,seat,x,y))
                shape=shape.cut(cyl(p['fasteners']['m25_pilot']/2,seat-r['display_pilot_depth'],seat+EPS,x,y))
            return shape.clean()
        for x, y in self.display_mounts():
            shape = shape.fuse(cyl(q['boss_diameter']/2, t-EPS, q['pcb_front_depth'], x, y))
            shape = shape.cut(cyl(p['fasteners']['m25_pilot']/2,
                q['pcb_front_depth']-q['pilot_depth'], q['pcb_front_depth']+EPS, x, y))
        return self.acrylic_opening(shape,pocket_gap)

    def front(self):
        p, c = self.p, self.p['case']; t = c['front_thickness']
        shape = rrbox(c['width'], c['height'], 0, t, c['corner_radius'])
        if c['edge_radius']:
            shape = cq.Workplane(obj=shape).edges('>Z').fillet(c['edge_radius']).val()
        iw, ih = c['width']-2*c['wall']-2*c['seam_clearance'], c['height']-2*c['wall']-2*c['seam_clearance']
        ir = c['corner_radius']-c['wall']-c['seam_clearance']
        lip = rrbox(iw, ih, t-EPS, t+c['lip_height'], ir).cut(rrbox(iw-2*c['lip_wall'], ih-2*c['lip_wall'], t-2*EPS, t+c['lip_height']+EPS, ir-c['lip_wall']))
        shape = shape.fuse(lip)
        end = c['depth']-c['rear_thickness']-c['closure_end_gap']
        for x, y in c['closure_xy']:
            root = cq.Solid.makeCone(c['closure_boss_diameter']/2, c['closure_root_diameter']/2,
                c['closure_root_height'], cq.Vector(x, y, -t-c['closure_root_height']))
            shape = shape.fuse(root).fuse(cyl(c['closure_boss_diameter']/2, t+c['closure_root_height']-EPS, end, x, y))
            shape = shape.cut(cyl(p['fasteners']['m25_pilot']/2, end-c['closure_pilot_depth'], end+EPS, x, y))
        return self.display_interface(self.wheel_interface(shape))

    def charger_datums(self):
        p, q = self.p, self.p['tp4056']; b=self.board_datums()
        mouth = b['bottom']-q['usb_tip_beyond_board']
        edge = mouth+q['usb_protrusion']
        end = edge+q['pcb_length']
        rear=b['front']-q['mount_standoff']
        return dict(x=b['left']+q['usb_left_from_board_left']+q['usb_width']/2,
            mouth_y=mouth,pcb_front_y=edge,pcb_end_y=end,pcb_rear_depth=rear,
            axis_depth=rear-q['usb_axis_above_pcb_rear'])

    def connector_insert(self, aperture_extra=0., gap=None):
        p=self.p; c=p['case']; q=p['connector_insert']; u=p['tp4056']; j=p['jack']
        usb=self.charger_datums(); jack=self.jack_datums(); outer=-c['height']/2
        gap=q['gap'] if gap is None else gap
        front=c['front_thickness']+gap; rear=c['depth']-c['rear_thickness']-gap
        shape=ypill(q['width'],rear-front,outer,outer+q['wall_thickness'],q['x'],(front+rear)/2,0.8)
        # The jack is already soldered. Its shoulder determines a recessed
        # nut seat; never pull the connector towards an unrelated wall datum.
        shape=shape.fuse(ycyl(j['seat_outer_diameter']/2,outer+q['wall_thickness']-EPS,
            jack['shoulder_y'],jack['x'],jack['axis_depth']))
        shape=shape.cut(ycyl(j['nut_access_diameter']/2,outer-EPS,jack['panel_outer_y'],jack['x'],jack['axis_depth']))
        shape=shape.cut(ycyl((j['panel_hole_diameter']+aperture_extra)/2,outer-EPS,
            jack['shoulder_y']+EPS,jack['x'],jack['axis_depth']))
        cable=self.usb_cable_datums(); v=p['usb_cable']
        # Two-stage recess: a large blind well accepts the cable hood, then
        # the small aperture surrounds the recessed device receptacle.
        shape=shape.fuse(ypill(cable['well_width']+2*v['well_wall'],cable['well_height']+2*v['well_wall'],
            outer+q['wall_thickness']-EPS,cable['well_rear_y'],usb['x'],usb['axis_depth'],v['well_outer_radius']))
        shape=shape.cut(ypill(cable['well_width']+aperture_extra,cable['well_height']+aperture_extra,
            outer-EPS,cable['well_floor_y'],usb['x'],usb['axis_depth'],v['well_corner_radius']))
        shape=shape.cut(ypill(u['aperture_width']+aperture_extra,u['aperture_height']+aperture_extra,
            outer-EPS,cable['well_rear_y']+EPS,usb['x'],usb['axis_depth'],u['aperture_height']/2))
        for x,d in self.insert_mounts():
            shape=shape.cut(ycyl(p['fasteners']['m2_clearance']/2,outer-EPS,outer+q['wall_thickness']+EPS,x,d))
        return shape.clean()

    def bottom_opening(self,shape):
        p=self.p; c=p['case']; q=p['connector_insert']; outer=-c['height']/2
        floor=c['depth']-c['rear_thickness']; inner=outer+q['wall_thickness']
        shape=shape.cut(box(q['width']+2*q['gap'],c['wall']+q['wall_thickness']+2*EPS,
            c['front_thickness']-EPS,floor,q['x'],outer+(q['wall_thickness']-c['wall'])/2))
        for x,d in self.insert_mounts():
            shape=shape.fuse(box(q['root_width'],q['boss_length'],q['root_min_depth'],floor+EPS,
                x,inner+q['boss_length']/2))
            shape=shape.fuse(ycyl(q['boss_diameter']/2,inner,inner+q['boss_length'],x,d))
            shape=shape.cut(ycyl(p['fasteners']['m2_pilot']/2,inner-EPS,inner+q['pilot_depth'],x,d))
        return shape.clean()

    def hold_interface(self, shape, clearance=None, pitch=None):
        p, q, c = self.p, self.p['hold'], self.p['case']
        clearance = q['window_clearance'] if clearance is None else clearance
        pitch = q['hole_pitch'] if pitch is None else pitch
        outer = c['height']/2; inner = outer-c['wall']; seat = inner-q['standoff_height']
        for sign in [-1, 1]:
            x = q['x']+sign*pitch/2
            shape = shape.fuse(ycyl(q['boss_diameter']/2, seat, inner+EPS, x, q['center_depth']))
            shape = shape.cut(ycyl(p['fasteners']['m2_pilot']/2, seat-EPS, seat+q['pilot_depth'], x, q['center_depth']))
        return shape.cut(ypill(q['neck_length_x']+q['travel']+2*clearance, q['neck_width_z']+2*clearance,
            seat-EPS, outer+EPS, q['x'], q['center_depth'], 0.35)).clean()

    def hold_cap(self, lever_gap=None):
        p, q, c = self.p, self.p['hold'], self.p['case']
        lever_gap = q['lever_gap'] if lever_gap is None else lever_gap
        outer = c['height']/2; inner = outer-c['wall']
        back = inner-q['neck_inner_protrusion']; face = outer+q['cap_wall_gap']+q['cap_thickness_y']
        cap = ypill(q['cap_length_x'], q['cap_width_z'], outer+q['cap_wall_gap'], face,
            q['x'], q['center_depth'], q['cap_corner_radius'])
        cap = cap.fuse(ypill(q['neck_length_x'], q['neck_width_z'], back, outer+q['cap_wall_gap']+EPS,
            q['x'], q['center_depth'], 0.25))
        lever_tip = inner-q['standoff_height']+q['lever_length_y']
        cap = cap.cut(ypill(q['lever_width_x']+2*lever_gap, q['lever_width_z']+2*lever_gap, back-EPS,
            lever_tip+q['pocket_end_gap'], q['x'], q['center_depth'], 0))
        for index in range(q['grip_rib_count']):
            x = q['x']+(index-(q['grip_rib_count']-1)/2)*q['grip_rib_pitch']
            cap = cap.cut(ypill(q['grip_rib_width'], q['cap_width_z']-2*q['cap_corner_radius'],
                face-q['grip_rib_height'], face+EPS, x, q['center_depth'], 0))
        return cap.clean()

    def back(self):
        p, c = self.p, self.p['case']; t = c['front_thickness']; floor = c['depth']-c['rear_thickness']
        shape = rrbox(c['width'], c['height'], t, c['depth'], c['corner_radius'])
        if c['edge_radius']:
            shape = cq.Workplane(obj=shape).edges('<Z').fillet(c['edge_radius']).val()
        shape = shape.cut(rrbox(c['width']-2*c['wall'], c['height']-2*c['wall'], t-EPS, floor,
            c['corner_radius']-c['wall']))
        q = p['mainboard']; seat = self.board_datums()['rear']
        for x, y in self.mainboard_mounts():
            shape = shape.fuse(cyl(q['boss_diameter']/2, seat, floor+EPS, x, y))
            shape = shape.cut(cyl(p['fasteners']['m2_pilot']/2, seat-EPS, seat+q['pilot_depth'], x, y))
        for x, y in c['closure_xy']:
            shape = shape.cut(cyl(p['fasteners']['m25_clearance']/2, floor-EPS, c['depth']+EPS, x, y))
        return self.hold_interface(self.bottom_opening(shape))

    def references(self):
        p = self.p; w = p['wheel']; src = self.root/'reference/clickwheel'
        for name in ['frame','wheel','select']:
            self.refs['clickwheel_'+name] = cq.importers.importStep(str(src/f'cad/{name}.step')).val().translate((w['x'], w['y'], -w['face_recess']))
        self.refs['clickwheel_pcb'] = cq.importers.importStep(str(src/'reference/pcb_with_component_envelopes.step')).val().translate((w['x'], w['y'], -w['face_recess']))
        self.refs['clickwheel_assembly'] = cq.importers.importStep(str(src/'cad/assembly.step')).val().translate((w['x'], w['y'], -w['face_recess']))
        q = p['display']; front = q['pcb_front_depth']; rear = front+q['pcb_thickness']
        pcb = self.display_shape(q['pcb_width'], q['pcb_height'], front, rear)
        for x, y in self.display_mounts(): pcb = pcb.cut(cyl(q['hole_diameter']/2, front-EPS, rear+EPS, x, y))
        self.refs['display_pcb'] = pcb
        self.refs['display_glass'] = self.display_shape(q['glass_width'], q['glass_height'], front-q['glass_above_pcb'], front, y_offset=q['glass_y_offset'])
        self.refs['display_active'] = self.display_shape(q['active_width'], q['active_height'], front-q['glass_above_pcb']-0.01, front-q['glass_above_pcb']+0.02, y_offset=q['glass_y_offset'])
        self.refs['display_rear_keepout'] = self.display_shape(q['pcb_width'], q['pcb_height'], rear, rear+q['rear_component_height'])
        a=p['acrylic']; lens=self.acrylic_datums()
        self.refs['screen_lens']=self.display_shape(lens['width'],lens['height'],lens['front_depth'],lens['rear_depth'],a['corner_radius'],q['glass_y_offset'])
        if a['adhesive_gap']>0:
            flush=a['mount_style']=='flush_carrier'
            start=lens['rear_depth'] if flush else a['front_lip_thickness']
            end=lens['carrier_front'] if flush else lens['front_depth']
            inner_w=lens['clear_width'] if flush else q['window_width']; inner_h=lens['clear_height'] if flush else q['window_height']
            glue=self.display_shape(lens['width'],lens['height'],start,end,a['corner_radius'],q['glass_y_offset'])
            glue=glue.cut(self.display_shape(inner_w+2*a['adhesive_inner_margin'],inner_h+2*a['adhesive_inner_margin'],
                start-EPS,end+EPS,q['window_radius']+a['adhesive_inner_margin'],q['glass_y_offset']))
            for x,y in self.acrylic_pad_centres():
                glue=glue.cut(cyl(a['stop_pad_diameter']/2,a['front_lip_thickness']-EPS,lens['front_depth']+EPS,x,y))
            self.refs['lens_adhesive_envelope']=glue.clean()
        q = p['mainboard']; board=self.board_datums(); front=board['front']; rear=board['rear']
        pcb = box(q['width'], q['height'], front, rear, board['x'], board['y'])
        for x, y in self.mainboard_mounts(): pcb = pcb.cut(cyl(q['hole_diameter']/2, front-EPS, rear+EPS, x, y))
        self.refs['mainboard'] = pcb
        components=box(q['width'],q['height'],front-q['component_height'],front,board['x'],board['y'])
        solder=box(q['width'],q['height'],rear,board['solder_rear'],board['x'],board['y'])
        for x,y in self.mainboard_mounts():
            components=components.cut(cyl(p['fasteners']['m2_head_diameter']/2+.3,front-q['component_height']-EPS,front+EPS,x,y))
            solder=solder.cut(cyl(q['boss_diameter']/2+.3,rear-EPS,board['solder_rear']+EPS,x,y))
        q = p['tp4056']; dat = self.charger_datums(); y = (dat['pcb_front_y']+dat['pcb_end_y'])/2
        r=dat['pcb_rear_depth']; x=dat['x']
        self.refs['tp4056_pcb'] = box(q['pcb_width'], q['pcb_length'], r-q['pcb_thickness'],r,x,y)
        self.refs['tp4056_components'] = box(q['pcb_width'],q['pcb_length'],r-q['module_height'],r-q['pcb_thickness'],x,y)
        self.refs['usb_c_shell'] = ypill(q['usb_width'], q['usb_height'], dat['mouth_y'], dat['mouth_y']+q['usb_metal_length'],x,dat['axis_depth'],q['usb_height']/2)
        components=components.cut(box(q['pcb_width']+2*q['body_clearance'],q['pcb_length']+2*q['body_clearance'],
            front-p['mainboard']['component_height']-EPS,front+EPS,x,y))
        q=p['jack']; dat=self.jack_datums(); x=dat['x']; shoulder=dat['shoulder_y']; axis=dat['axis_depth']
        body=box(q['body_width'],q['body_length'],dat['base_depth']-q['body_height'],dat['base_depth'],x+q['body_x_offset'],shoulder+q['body_length']/2)
        body=body.fuse(ycyl(q['thread_diameter']/2,dat['tip_y'],shoulder,x,axis))
        self.refs['jack_body']=body.cut(ycyl(q['bore_diameter']/2,dat['tip_y']-EPS,shoulder+q['body_length']+EPS,x,axis)).clean()
        nut_face=dat['panel_outer_y']
        self.refs['jack_nut']=ycyl(q['nut_diameter']/2,nut_face-q['nut_thickness'],nut_face,x,axis).cut(ycyl(q['thread_diameter']/2,nut_face-q['nut_thickness']-EPS,nut_face+EPS,x,axis))
        components=components.cut(box(q['body_width']+2*q['body_clearance'],q['body_length']+2*q['body_clearance'],
            front-p['mainboard']['component_height']-EPS,front+EPS,x+q['body_x_offset'],shoulder+q['body_length']/2))
        self.refs['mainboard_component_keepout']=components.clean()
        self.refs['mainboard_rear_solder_keepout']=solder.clean()
        self.groups['rigid_electronics']=['mainboard','mainboard_component_keepout','mainboard_rear_solder_keepout','tp4056_pcb','tp4056_components','usb_c_shell','jack_body']
        u=self.charger_datums(); cable=self.usb_cable_datums(); q=p['usb_cable']
        self.refs['usb_cable_hood']=ypill(q['body_width'],q['body_height'],cable['body_rear_y'],cable['body_front_y'],u['x'],u['axis_depth'],0)
        self.refs['usb_cable_metal']=ypill(q['metal_width'],q['metal_height'],cable['body_front_y'],cable['metal_tip_y'],u['x'],u['axis_depth'],q['metal_height']/2)
        q = p['hold']; seat = p['case']['height']/2-p['case']['wall']-q['standoff_height']
        flange = ypill(q['flange_length'], q['flange_width_z'], seat-q['flange_thickness'], seat, q['x'], q['center_depth'])
        for sign in [-1, 1]: flange = flange.cut(ycyl(q['hole_diameter']/2, seat-q['flange_thickness']-EPS, seat+EPS, q['x']+sign*q['hole_pitch']/2, q['center_depth']))
        self.refs['hold_switch_body'] = flange.fuse(ypill(q['body_length_x'], q['body_width_z'], seat-q['body_depth_y'], seat-q['flange_thickness'], q['x'], q['center_depth'])).clean()
        self.refs['hold_lever'] = ypill(q['lever_width_x'], q['lever_width_z'], seat, seat+q['lever_length_y'], q['x'], q['center_depth'])
        self.refs['hold_terminal_keepout'] = ypill(q['body_length_x'], q['body_width_z'], seat-q['body_depth_y']-q['terminal_length_y'], seat-q['body_depth_y'], q['x'], q['center_depth'])

    def build(self, coupons=True):
        self.check_parameters()
        self.add('front_panel', self.front(), group='main')
        self.add('back_chassis', self.back(), 'rear_down', 'main')
        self.add('hold_slider_cap', self.hold_cap(), 'hold_outside_down', 'main')
        self.add('bottom_connector_insert',self.connector_insert(),'bottom_outside_down','main',
            'Надевается снаружи после установки всей припаянной сборки. 2xM2x4, гайка джека снаружи в углублении.')
        if self.p['acrylic']['mount_style']=='flush_carrier':
            self.add('display_lens_carrier',self.lens_carrier(),group='main',
                note='Плоский акрил приклеить сзади по полосе 2 мм; общие с TFT четыре M2.5x5. Печать клеевой плоскостью на столе.')
            alt=copy.deepcopy(self.p);alt['acrylic']['mount_style']='rear_lip';alt['fasteners']['display_screw']=4.0
            self.add('front_panel_rear_lip_alternative',Model(alt,self.root).front(),group='alternative',
                note='Альтернатива: прежний передний бортик и акрил на глубине 1 мм. Без display_lens_carrier, TFT крепить M2.5x4.')
        self.references()
        if coupons: self.build_coupons()
        return self

    def build_coupons(self):
        # Implemented below, using the exact same interface builders as the case.
        build_coupons(self)


def printable(part):
    shape = part.shape
    angle = 180 if part.pose == 'front_down' else -90 if part.pose == 'hold_outside_down' else 90 if part.pose == 'bottom_outside_down' else 0
    if angle: shape = shape.rotate((0, 0, 0), (1, 0, 0), angle)
    low = bounds(shape)[0]
    shift = -low
    shape = shape.translate(tuple(shift))
    return shape, {'rotation_x_degrees': angle, 'translation_xyz': shift.tolist(), 'mirrored': False}


def tag(number):
    return f'{number:.2f}'.replace('-', 'm').replace('.', 'p')


def dots(shape, count, x=0., y=0., face_depth=0., diameter=.8, depth=.35):
    for index in range(count):
        xx = x+(index-(count-1)/2)*diameter*1.5
        shape = shape.cut(cyl(diameter/2, face_depth-EPS, face_depth+depth, xx, y))
    return shape.clean()


def hold_patch(model, clearance=None, pitch=None):
    p = model.p; q, c, v = p['hold'], p['case'], p['coupons']
    shape = ypill(v['hold_patch_width'], v['hold_patch_height'], c['height']/2-c['wall'], c['height']/2,
        q['x'], q['center_depth'], 0)
    return model.hold_interface(shape, clearance, pitch)


def bottom_patch(model):
    # Preserve full board width and both insert seats. A small full-width end
    # of the tray accepts the soldered assembly; no charger removal required.
    p=model.p; c=p['case']; v=p['coupons']
    return model.back().intersect(box(c['width']+EPS,v['bottom_patch_length'],c['front_thickness'],c['depth']+EPS,
        0,-c['height']/2+v['bottom_patch_length']/2))


def build_coupons(m):
    p = m.p; c, q, v = p['case'], p['wheel'], p['coupons']
    lcd = p['display']; border = v['window_border']
    patch = m.display_shape(lcd['pcb_width']+2*border, lcd['pcb_height']+2*border, 0, c['front_thickness'], 2)
    m.add('display_window_coupon', m.display_interface(patch), note='Полное окно, четыре стойки и отверстия дисплея; M2.5x4.')
    a=p['acrylic']; lens=m.acrylic_datums()
    lens_patch=m.display_shape(lcd['pcb_width']+2*v['window_border'],lcd['pcb_height']+2*v['window_border'],0,c['front_thickness'],2,lcd['glass_y_offset'])
    m.add('acrylic_seat_coupon',m.display_interface(lens_patch),note='Примерка плоского акрила заподлицо с display_lens_carrier и TFT; номинальный зазор '+str(a['pocket_side_clearance'])+' на сторону.')
    for gap in a['fit_clearances']:
        if abs(gap-a['pocket_side_clearance'])<1e-7: continue
        m.add('acrylic_seat_gap_'+tag(gap),m.display_interface(lens_patch,pocket_gap=gap),
            note=f'Тот же лазерный акрил; карман с зазором {gap:.2f} на сторону. Вариант 0.20 представлен основным acrylic_seat_coupon.')
    side = 2*(m.w['frame_outer_radius']+v['wheel_border'])
    patch = rrbox(side, side, 0, c['front_thickness'], 2, q['x'], q['y'])
    m.add('clickwheel_mount_coupon', m.wheel_interface(patch), note='Полный интерфейс каркаса 42x42, выборка и глухие пилоты; M2.5x4.')
    for index, extra in enumerate(v['wheel_step_extras'], 1):
        shape = dots(m.wheel_interface(patch, step_extra=extra), index, q['x'], q['y']-side/2+2)
        m.add('clickwheel_depth_extra_'+tag(extra), shape,
            note=f'{index} точек: ступень 1.15 + осевой запас {extra:.2f}; другие размеры как у панели.')
    for index, gap in enumerate(v['clearances'], 1):
        shape = dots(m.wheel_interface(patch, gap=gap), index, q['x'], q['y']-side/2+2)
        m.add('clickwheel_window_gap_'+tag(gap), shape, note=f'{index} точек: радиальный зазор вокруг колеса {gap:.2f} на сторону.')
    m.add('bottom_edge_coupon',bottom_patch(m),'rear_down',note='Поддон для нижней части всей припаянной сборки; использовать с bottom_connector_insert. Для примерки верх платы временно поддержать на той же высоте.')
    for gap in v['insert_clearances']:
        if abs(gap-p['connector_insert']['gap'])<1e-7: continue
        # The mating tray opening is fixed. Increase/decrease insert edges.
        delta=p['connector_insert']['gap']-gap
        params=copy.deepcopy(p); params['connector_insert']['width']+=2*delta
        params['connector_insert']['mount_from_left']=[offset+delta for offset in p['connector_insert']['mount_from_left']]
        variant=Model(params,m.root)
        m.add('bottom_insert_gap_'+tag(gap),variant.connector_insert(gap=gap),'bottom_outside_down',
            note=f'Зазор вставки {gap:.2f} к неизменённому bottom_edge_coupon; оси разъёмов и винтов сохранены.')
    for extra in v['connector_aperture_extras']:
        if extra==0: continue
        m.add('bottom_aperture_extra_'+tag(extra),m.connector_insert(aperture_extra=extra),'bottom_outside_down',
            note=f'Окна USB и джека расширены на {extra:.2f} по полному размеру; плата и оси НЕ смещены.')
    for recess in v['usb_recesses']:
        nominal=m.usb_cable_datums()['usb_recess']
        if abs(recess-nominal)<1e-7: continue
        params=copy.deepcopy(p);params['mainboard']['jack_tip_recess']+=recess-nominal
        m.add('bottom_insert_recess_'+tag(recess),Model(params,m.root).connector_insert(),'bottom_outside_down',
            note=f'Для утопления USB {recess:.2f} СДВИНУТЬ ВСЮ ПЛАТУ по Y на {recess-nominal:+.2f}; отдельно зарядник не перемещается. Шаблон отверстий основной версии тогда не подходит.')
    for gap in v['usb_cable_side_clearances']:
        if abs(gap-p['usb_cable']['side_clearance'])<1e-7: continue
        params=copy.deepcopy(p);params['usb_cable']['side_clearance']=gap
        m.add('usb_cable_well_gap_'+tag(gap),Model(params,m.root).connector_insert(),'bottom_outside_down',
            note=f'Корпус кабеля 9.7x5.6, зазор {gap:.2f} на сторону. Джек, оси, положение платы и креплений неизменны.')
    if a['mount_style']=='flush_carrier':
        m.add('lens_gluing_jig',m.lens_gluing_jig(),note='Опора при вклейке: наружная панель лежит на плоском бортике, акрил — на выступе 0.1 мм. Покрыть тонкой разделительной плёнкой; её толщину учесть при примерке. Не приклеивать рамку к кондуктору.')
    j = p['jack']
    for index, thickness in enumerate(v['jack_panel_thicknesses'], 1):
        shape = rrbox(v['jack_plate_width'], v['jack_plate_height'], 0, thickness, 2)
        shape = shape.cut(cyl(j['panel_hole_diameter']/2, -EPS, thickness+EPS))
        shape = dots(shape, index, y=-v['jack_plate_height']/2+2)
        m.add('jack_panel_thickness_'+tag(thickness), shape,
            note=f'{index} точек: толщина {thickness:.2f}, отверстие {j["panel_hole_diameter"]:.2f}; гайка снаружи.')
    shape = rrbox(20*len(v['jack_holes']), v['jack_plate_height'], 0, j['panel_thickness'], 2)
    for index, diameter in enumerate(v['jack_holes'], 1):
        x = (index-(len(v['jack_holes'])+1)/2)*20
        shape = dots(shape.cut(cyl(diameter/2, -EPS, j['panel_thickness']+EPS, x)), index, x, -9)
    m.add('jack_hole_diameter_coupon', shape, note='1/2/3 точки: отверстия '+', '.join(str(n) for n in v['jack_holes']))
    m.add('hold_switch_coupon', hold_patch(m), 'hold_outside_down', note='Окно, две бобышки и пилоты: актуальные размеры hold из JSON; M2x4 изнутри.')
    for clearance in v['clearances']:
        m.add('hold_window_gap_'+tag(clearance), hold_patch(m, clearance), 'hold_outside_down',
            note=f'Боковой зазор окна {clearance:.2f}; применяется с основным колпачком.')
    for pitch in v['hold_hole_pitches']:
        if abs(pitch-p['hold']['hole_pitch']) < 1e-7: continue
        m.add('hold_mount_pitch_'+tag(pitch), hold_patch(m, pitch=pitch), 'hold_outside_down',
            note=f'Пробная компактная версия: шаг отверстий {pitch:.2f}; габарит рычага проверить.')
    for gap in v['lever_gaps']:
        if abs(gap-p['hold']['lever_gap']) < 1e-7: continue
        m.add('hold_cap_lever_gap_'+tag(gap), m.hold_cap(gap), 'hold_outside_down',
            note=f'Посадка кармана рычага: {gap:+.2f} на сторону; основной hold_slider_cap {p["hold"]["lever_gap"]:+.2f}.')
    shape = rrbox(v['screw_bar_width'], v['screw_bar_height'], 0, v['screw_bar_base'], 2)
    pilots = [('M2', d, p['mainboard']['boss_diameter']) for d in v['m2_pilots']] + [('M2.5', d, c['closure_boss_diameter']) for d in v['m25_pilots']]
    for index, (name, diameter, outside) in enumerate(pilots):
        x = (index-(len(pilots)-1)/2)*12
        rear = v['screw_bar_base']+v['screw_boss_height']
        shape = shape.fuse(cyl(outside/2, v['screw_bar_base']-EPS, rear, x, 2))
        shape = shape.cut(cyl(diameter/2, rear-min(4.2, v['screw_boss_height']-0.6), rear+EPS, x, 2))
        shape = dots(shape, index+1, x, -8)
    m.add('screw_boss_coupon', shape, note='1..6 точек: M2 '+str(v['m2_pilots'])+'; M2.5 '+str(v['m25_pilots'])+'. Для подбора пилота использовать M2x3/4 и M2.5x3/4.')
    mounts = m.mainboard_mounts(); thickness = v['screw_bar_base']; radius = p['mainboard']['boss_diameter']/2
    templates = [cyl(radius, 0, thickness, x, y) for x, y in mounts]
    # Connect the actual user-editable points with bars; no rectangular-board
    # assumption is baked into this cheap three-hole alignment template.
    for (x1,y1),(x2,y2) in zip(mounts, mounts[1:]):
        length = math.hypot(x2-x1,y2-y1)
        templates.append(box(length,2*radius,0,thickness).rotate((0,0,0),(0,0,1),math.degrees(math.atan2(y2-y1,x2-x1))).translate(((x1+x2)/2,(y1+y2)/2,0)))
    template = join(templates)
    for x,y in mounts: template = template.cut(cyl(p['fasteners']['m2_clearance']/2,-EPS,thickness+EPS,x,y))
    m.add('mainboard_hole_template_coupon', template, note='Три M2-точки mainboard.mount_from_left_bottom от левого нижнего угла платы; отверстия оценочные, проверить до печати корпуса.')
    length=v['stack_gauge_jaw_length']; width=v['stack_gauge_width']; wall=v['stack_gauge_wall']
    stack=p['mainboard']['stack_to_wheel_face']
    gauge=join([box(length,width,-wall,0),box(length,width,stack,stack+wall),
        box(wall,width,-wall,stack+wall,-length/2+wall/2)])
    m.add('stack_height_gauge',gauge,'bottom_outside_down',note=f'Свободный просвет между губками {stack:g} мм. Проверить сборку от самой нижней точки пайки до лица колеса, не давить на колесо и детали. Печатается боком без поддержек.')
    stem_height = v['screw_boss_height']+v['tolerance_frame_thickness']
    end = c['front_thickness']+stem_height
    joint = rrbox(20,20,0,c['front_thickness'],2).fuse(cyl(c['closure_boss_diameter']/2,c['front_thickness']-EPS,end))
    joint = joint.cut(cyl(p['fasteners']['m25_pilot']/2,end-c['closure_pilot_depth'],end+EPS))
    m.add('closure_front_boss_coupon', joint, note='Передний пробник стыка: тот же пилот и глубина, короткая стойка для проверки M2.5x8.')
    joint_plate = rrbox(20,20,end,end+c['rear_thickness'],2).cut(cyl(p['fasteners']['m25_clearance']/2,end-EPS,end+c['rear_thickness']+EPS))
    m.add('closure_rear_plate_coupon', joint_plate, 'rear_down', note='Задняя пластина стыка. Соединить с closure_front_boss_coupon винтом M2.5x8; затягивать до контакта опор.')
    count = len(v['clearances']); width = v['tolerance_key_width']; t = v['tolerance_frame_thickness']
    shape = rrbox(count*18+4, 20, 0, t, 2)
    for index, gap in enumerate(v['clearances'], 1):
        x = (index-(count+1)/2)*18
        shape = shape.cut(rrbox(width+2*gap, width+2*gap, -EPS, t+EPS, 0.8+gap, x))
        shape = dots(shape, index, x, -7)
    m.add('tolerance_frame_coupon', shape, note='1..4 точек: зазор на сторону '+str(v['clearances'])+'; использовать tolerance_key, без масштабирования.')
    key = rrbox(width+4, width+4, 0, 2, 1)
    key = key.fuse(rrbox(width, width, 2-EPS, 2+v['tolerance_key_height'], .8))
    m.add('tolerance_key', key, note=f'Один ключ {width:.2f} x {width:.2f}; последовательно примерить во все четыре окна.')


COLORS = {
    'front_panel': (.91, .93, .95), 'back_chassis': (.24, .31, .38),
    'hold_slider_cap': (.96, .45, .14), 'bottom_connector_insert': (.36, .48, .57),
    'clickwheel_frame': (.29, .36, .42), 'clickwheel_wheel': (.91, .91, .88),
    'clickwheel_select': (.67, .75, .81), 'clickwheel_pcb': (.12, .43, .29),
    'clickwheel_assembly': (.72, .76, .77), 'display_pcb': (.47, .13, .17),
    'display_glass': (.10, .13, .16), 'display_active': (.23, .43, .48),
    'mainboard': (.14, .43, .31), 'tp4056_pcb': (.12, .32, .66),
    'tp4056_components': (.22, .26, .30), 'usb_c_shell': (.71, .75, .80),
    'jack_body': (.12, .14, .18), 'jack_nut': (.71, .74, .77),
    'hold_switch_body': (.62, .67, .69), 'hold_lever': (.15, .16, .18),
    'screen_lens':(.61,.83,.88),'lens_adhesive_envelope':(.86,.62,.23)
}
COLORS.update(display_lens_carrier=(.60,.67,.72),usb_cable_hood=(.19,.20,.22),usb_cable_metal=(.73,.76,.79))


def assembly_items(model, kind='all'):
    names = ['back_chassis', 'hold_slider_cap', 'bottom_connector_insert'] if kind == 'back' else ['front_panel'] if kind == 'front' else ['front_panel', 'back_chassis', 'hold_slider_cap', 'bottom_connector_insert']
    items = [(name, model.parts[name].shape) for name in names]
    if kind!='back' and 'display_lens_carrier' in model.parts:items.append(('display_lens_carrier',model.parts['display_lens_carrier'].shape))
    if kind != 'back': items.append(('clickwheel_original_assembly', model.refs['clickwheel_assembly']))
    if kind != 'front':
        items += [(name, model.refs[name]) for name in ['mainboard', 'tp4056_pcb', 'tp4056_components', 'usb_c_shell', 'jack_body', 'jack_nut', 'hold_switch_body', 'hold_lever']]
    if kind == 'all': items += [(name, model.refs[name]) for name in ['display_pcb', 'display_glass', 'display_active']]
    if kind != 'back': items += [(name,model.refs[name]) for name in ['screen_lens','lens_adhesive_envelope'] if name in model.refs]
    return items


def save_assembly(items, path):
    result = cq.Assembly(name=path.stem)
    for name, shape in items:
        color = COLORS.get(name, COLORS['clickwheel_assembly'] if name.startswith('clickwheel') else (.6, .7, .75))
        result.add(shape, name=name, color=cq.Color(*color, .25) if name=='screen_lens' else cq.Color(*color))
    result.save(str(path), exportType='STEP')


def installation_checks(model):
    """Conservative continuous Z-access envelopes; sampled insert translation.

    Lower the board without front/wheel, bottom insert or nut. Each reference
    envelope's bounding rectangle sweeps to D=-8, ahead of the open chassis.
    The rear-solder keepout retains the explicit board mounting clearances.
    """
    p=model.p; board=model.board_datums(); back=model.parts['back_chassis'].shape
    access={}
    for name in model.groups['rigid_electronics']:
        ref=model.refs[name]; bb=bounds(ref); x,y=(bb[0,:2]+bb[1,:2])/2
        shape=box(bb[1,0]-bb[0,0],bb[1,1]-bb[0,1],-8.,-bb[0,2],x,y)
        if name=='mainboard_rear_solder_keepout':
            for xx,yy in model.mainboard_mounts():
                shape=shape.cut(cyl(p['mainboard']['boss_diameter']/2+.3,-8.1,board['solder_rear']+EPS,xx,yy))
        access[name]=volume_overlap(shape,back)
    insert=model.parts['bottom_connector_insert'].shape
    electronics=compound([model.refs[name] for name in model.groups['rigid_electronics']])
    slide=[]
    for offset in [-24.,-12.,-6.,-3.,-1.5,-.75,-.25,0.]:
        piece=insert.translate((0,offset,0))
        slide.append({'offset_y_mm':offset,'chassis_overlap_mm3':volume_overlap(piece,back),
            'soldered_assembly_overlap_mm3':volume_overlap(piece,electronics)})
    return {'passed':max(access.values())<1e-5 and all(max(row[k] for k in row if k.endswith('_mm3'))<1e-5 for row in slide),
        'whole_board_lowering_continuous_conservative_envelopes_mm3':access,
        'insert_slide_samples':slide,'insert_slide_sampling_not_continuous_proof':True,
        'sequence':['remove jack nut and bottom insert','lower complete soldered board along -Z','secure board with three M2x5',
            'slide insert along +Y over both connectors','secure insert with two M2x4','fit jack nut without forcing soldered alignment','fit front panel'],
        'physical_assembly_verified':False}


def cable_fit_checks(model):
    p=model.p; u=model.charger_datums(); d=model.usb_cable_datums(); q=p['usb_cable']
    targets={name:model.parts[name].shape for name in ['front_panel','back_chassis','bottom_connector_insert']}
    variants={
        'nominal':model.refs['usb_cable_hood'],
        'size_allowance_and_shorter_tip':ypill(q['body_width']+q['measurement_size_allowance'],
            q['body_height']+q['measurement_size_allowance'],d['body_rear_y'],d['body_front_y']+q['tip_length_allowance'],u['x'],u['axis_depth'],0)}
    rows=[]
    for name,hood in variants.items():
        for offset in [-15.,-5.,-1.,0.]:
            piece=hood.translate((0,offset,0))
            row={'variant':name,'offset_y_mm':offset}
            row.update({part+'_overlap_mm3':volume_overlap(piece,shape) for part,shape in targets.items()})
            for ref in model.groups['rigid_electronics']+['jack_nut']:
                row[ref+'_overlap_mm3']=volume_overlap(piece,model.refs[ref])
            rows.append(row)
    metal={name:volume_overlap(model.refs['usb_cable_metal'],shape) for name,shape in targets.items()}
    return {'passed':all(max(row[k] for k in row if k.endswith('_mm3'))<1e-5 for row in rows) and max(metal.values())<1e-5,
        'datums':d,'hood_insertion_samples':rows,'metal_printed_part_overlaps_mm3':metal,
        'measurement_allowance_on_total_size_mm':q['measurement_size_allowance'],'shorter_tip_allowance_mm':q['tip_length_allowance'],
        'assumed_engagement_mm':q['engagement_depth'],'physical_cable_fit_or_electrical_contact_verified':False,
        'note':'User measured hood 9.7x5.6 and exposed tip about 7 mm. Mating depth and metal section are provisional envelopes; no internal USB contact geometry or physical mating validation.'}


def validate_assembly(model):
    p = model.p; front = model.parts['front_panel'].shape; back = model.parts['back_chassis'].shape
    collisions = {}
    collisions['front_back'] = volume_overlap(front, back)
    for name in ['clickwheel_frame','clickwheel_wheel','clickwheel_select','clickwheel_pcb','display_pcb','display_glass','display_rear_keepout','screen_lens','lens_adhesive_envelope']:
        if name not in model.refs: continue
        collisions['front__'+name] = volume_overlap(front, model.refs[name])
    for name in ['mainboard','mainboard_component_keepout','mainboard_rear_solder_keepout','tp4056_pcb','tp4056_components','usb_c_shell','jack_body','jack_nut','hold_switch_body','hold_lever','hold_terminal_keepout','display_rear_keepout','clickwheel_assembly']:
        collisions['back__'+name] = volume_overlap(back, model.refs[name])
    insert=model.parts['bottom_connector_insert'].shape
    collisions['insert_back']=volume_overlap(insert,back)
    collisions['insert_front']=volume_overlap(insert,front)
    for name in model.groups['rigid_electronics']+['jack_nut']:
        collisions['insert__'+name]=volume_overlap(insert,model.refs[name])
    if 'display_lens_carrier' in model.parts:
        carrier=model.parts['display_lens_carrier'].shape
        for name,shape in [('front',front),('back',back)]+list(model.refs.items()):
            if name.startswith('usb_cable'):continue
            collisions['carrier__'+name]=volume_overlap(carrier,shape)
    sweeps = []
    for x in [-p['hold']['travel']/2, 0, p['hold']['travel']/2]:
        cap = model.parts['hold_slider_cap'].shape.translate((x, 0, 0))
        lever = model.refs['hold_lever'].translate((x, 0, 0))
        entry = {'offset_x': x, 'cap_back_overlap_mm3': volume_overlap(cap, back),
                 'cap_lever_overlap_mm3': volume_overlap(cap, lever),
                 'cap_body_overlap_mm3': volume_overlap(cap, model.refs['hold_switch_body'])}
        sweeps.append(entry)
    wheel_sweep = volume_overlap(front, model.refs['clickwheel_wheel'].translate((0, 0, -model.w['switch_travel_check'])))
    select_sweep = volume_overlap(front, model.refs['clickwheel_select'].translate((0, 0, -model.w['select_stop_gap'])))
    modules = {
        'wheel': model.refs['clickwheel_assembly'],
        'display': compound([model.refs[n] for n in ['display_pcb','display_glass','display_rear_keepout']]),
        'electronics':compound([model.refs[n] for n in model.groups['rigid_electronics']]),
        'jack_nut':model.refs['jack_nut'],
        'hold': compound([model.refs[n] for n in ['hold_switch_body','hold_lever','hold_terminal_keepout']]),
        'lens':model.refs['screen_lens'],
    }
    names = list(modules)
    module_pairs = {a+'__'+b: volume_overlap(modules[a],modules[b]) for i,a in enumerate(names) for b in names[i+1:]}
    # Rigid grouping must not conceal incompatible internal module envelopes.
    parent_parts={
        'mainboard':compound([model.refs[n] for n in ['mainboard','mainboard_component_keepout','mainboard_rear_solder_keepout']]),
        'charger':compound([model.refs[n] for n in ['tp4056_pcb','tp4056_components','usb_c_shell']]),
        'jack':model.refs['jack_body']}
    keys=list(parent_parts)
    rigid_pairs={a+'__'+b:volume_overlap(parent_parts[a],parent_parts[b]) for i,a in enumerate(keys) for b in keys[i+1:]}
    for name in model.groups['rigid_electronics']:
        collisions['front__'+name]=volume_overlap(front,model.refs[name])
    # The imported original assembly intentionally includes four Ø2.5 shanks
    # in our Ø2.1 thread-forming pilots. Classify this interference explicitly.
    raw = volume_overlap(front, model.refs['clickwheel_assembly'])
    expected = len(model.wheel_mounts())*math.pi*((2.5/2)**2-(p['fasteners']['m25_pilot']/2)**2)*(p['fasteners']['wheel_screw']-model.w['case_lug_thickness'])
    passed = max(collisions.values()) < 1e-5 and all(max(e[n] for n in e if n.endswith('_mm3')) < 1e-5 for e in sweeps)
    installation=installation_checks(model)
    cable=cable_fit_checks(model)
    passed = passed and max(module_pairs.values()) < 1e-5 and max(rigid_pairs.values())<1e-5 and wheel_sweep < 1e-5 and select_sweep < 1e-5 and abs(raw-expected) < 1e-4 and installation['passed'] and cable['passed']
    return {'passed': passed, 'unintended_overlaps_mm3': collisions, 'hold_sweep': sweeps,
        'wheel_down_clearance_overlap_mm3': wheel_sweep, 'select_down_clearance_overlap_mm3': select_sweep,
        'imported_screw_pilot_interference_mm3': raw, 'expected_thread_forming_interference_mm3': expected, 'module_pair_overlaps_mm3': module_pairs,
        'thread_note': 'Intentional Ø2.5 unthreaded reference shanks intersect Ø2.1 pilots. No modeled thread or physical pull-out test.',
        'acrylic':model.acrylic_datums(),
        'board_datums':model.board_datums(),'usb_datums':model.charger_datums(),'jack_datums':model.jack_datums(),
        'rigid_module_pair_overlaps_mm3':rigid_pairs,'installation':installation,
        'usb_cable_fit':cable,
        'limits': 'Rigid CAD positions and envelopes only. No FEA, measured fit, wiring/battery routing or real actuation validation.'}


def export_model(model, output, roundtrip=True, parameter_path=None):
    started = time.time()
    for folder in ['cad','stl','reference/envelopes','validation','images']: (output/folder).mkdir(parents=True, exist_ok=True)
    report = {'version': model.p['version'], 'units': 'mm', 'cadquery': cq.__version__,
        'parameters_sha256': sha(parameter_path or HERE/'parameters.json'), 'effective_parameters': model.p, 'full_roundtrip': roundtrip,
        'generator_sha256': sha(HERE/'build_enclosure.py'),
        'parameter_checks': model.check_parameters(), 'parts': {}}
    rows = []
    for name, part in model.parts.items():
        step_path = output/'cad'/f'{name}.step'; stl_path = output/'stl'/f'{name}.stl'
        cq.exporters.export(part.shape, str(step_path))
        printed, transform = printable(part)
        cq.exporters.export(printed, str(stl_path), tolerance=model.p['coupons']['stl_linear_tolerance'],
            angularTolerance=model.p['coupons']['stl_angular_tolerance'])
        mesh = trimesh.load(str(stl_path), force='mesh', process=True)
        mesh_ok = bool(mesh.is_watertight and mesh.is_winding_consistent and mesh.volume > 0)
        error = abs(mesh.volume-part.shape.Volume())/part.shape.Volume()
        bb_error = float(np.max(np.abs(mesh.bounds-bounds(printed))))
        imported = cq.importers.importStep(str(step_path)).val() if roundtrip else part.shape
        step_ok = imported.isValid() and len(imported.Solids()) == 1 and abs(imported.Volume()-part.shape.Volume())/part.shape.Volume() < 1e-6
        info = {'brep_valid': part.shape.isValid(), 'solids': len(part.shape.Solids()), 'volume_mm3': part.shape.Volume(),
            'stl_watertight': bool(mesh.is_watertight), 'stl_winding_consistent': bool(mesh.is_winding_consistent),
            'stl_triangles': len(mesh.faces), 'stl_volume_error_fraction': float(error), 'stl_bounds_error_mm': bb_error,
            'step_roundtrip_passed': bool(step_ok), 'print_transform': transform,
            'cad_bounds_mm': bounds(part.shape).tolist(), 'stl_bounds_mm': mesh.bounds.tolist(),
            'step_sha256': sha(step_path), 'stl_sha256': sha(stl_path), 'group': part.group, 'note': part.note}
        info['passed'] = bool(mesh_ok and error < .005 and bb_error < .08 and step_ok)
        report['parts'][name] = info
        if not info['passed']: raise ValueError(name+' export validation failed: '+str(info))
        size = mesh.extents
        rows.append([name, part.group, *[round(n,3) for n in size], round(mesh.volume,2), part.pose, part.note])
        print('OK', name, f'{size[0]:.1f} x {size[1]:.1f} x {size[2]:.1f}', flush=True)
    for name, shape in model.refs.items():
        cq.exporters.export(shape, str(output/'reference/envelopes'/f'{name}.step'))
    cq.exporters.export(model.refs['screen_lens'],str(output/'cad/acrylic_screen_lens.step'))
    save_assembly([(name,model.refs[name]) for name in model.groups['rigid_electronics'] if 'keepout' not in name],output/'cad/soldered_electronics_assembly.step')
    save_assembly([(name,model.parts[name].shape if name in model.parts else model.refs[name]) for name in
        ['front_panel','display_lens_carrier','screen_lens','lens_adhesive_envelope','display_pcb','display_glass'] if name in model.parts or name in model.refs],output/'cad/front_acrylic_display_assembly.step')
    if 'display_lens_carrier' in model.parts:
        save_assembly([(name,model.parts[name].shape if name in model.parts else model.refs[name]) for name in
            ['display_lens_carrier','screen_lens','lens_adhesive_envelope']],output/'cad/acrylic_carrier_subassembly.step')
    save_assembly(assembly_items(model,'back')+[(name,model.refs[name]) for name in ['usb_cable_hood','usb_cable_metal']],output/'cad/bottom_cable_fit_assembly.step')
    for kind, name in [('front','front_with_clickwheel'), ('back','back_with_connectors'), ('all','assembly_mockup')]:
        save_assembly(assembly_items(model, kind), output/'cad'/f'{name}.step')
    save_assembly([(name, shape) for name, shape in model.refs.items() if not name.startswith('clickwheel')], output/'reference/envelopes/module_envelopes.step')
    report['assembly'] = validate_assembly(model)
    report['input_hashes_preserved'] = all(sha(model.root/'reference'/name) == value['sha256'] for name, value in
        json.loads((model.root/'reference/input_manifest.json').read_text(encoding='utf-8')).items())
    report['physical_fit_verified'] = False
    report['seconds'] = round(time.time()-started, 2)
    report['passed'] = bool(all(row['passed'] for row in report['parts'].values()) and report['assembly']['passed'] and report['input_hashes_preserved'])
    (output/'validation/geometry_check.json').write_text(json.dumps(report, indent=2, ensure_ascii=False)+'\n', encoding='utf-8')
    with (output/'validation/parts_print_table.csv').open('w', encoding='utf-8-sig', newline='') as stream:
        writer = csv.writer(stream); writer.writerow(['part','group','print_x_mm','print_y_mm','print_z_mm','volume_mm3','pose','note']); writer.writerows(rows)
    if not report['passed']: raise ValueError('Assembly validation failed; inspect validation/geometry_check.json')
    print(f'PASS {len(model.parts)} parts; STEP/STL/assembly checks. Physical fit remains unverified.', flush=True)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--parameters', type=Path, default=HERE/'parameters.json')
    parser.add_argument('--output', type=Path, default=ROOT)
    parser.add_argument('--skip-coupons', action='store_true')
    parser.add_argument('--skip-roundtrip', action='store_true', help='Iteration only; final package uses full checks.')
    args = parser.parse_args()
    p = json.loads(args.parameters.read_text(encoding='utf-8-sig'))
    model = Model(p).build(not args.skip_coupons)
    export_model(model, args.output.resolve(), not args.skip_roundtrip, args.parameters)


if __name__ == '__main__':
    main()
