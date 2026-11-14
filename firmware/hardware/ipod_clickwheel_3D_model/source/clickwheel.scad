// Click-wheel prototype v0.2. Units: mm. 2 MOVING CAPS ONLY.
// Coordinates of PCB mounting holes are unknown; no M2 holes in moving caps.
// Main STL files include hidden lips: wheel 45.6, SELECT 20.0 overall OD.
// SELECT has a FLAT flange: supports under its overhang are required in face-down FDM printing.
// F6 -> Export STL. Modes: "print", "assembled", "ring", "select".
mode = "print";
retention_lips = true;
engraved_icons = true;

wheel_diameter = 44.0;
select_diameter = 15.0;
radial_gap = 0.2;  // PER SIDE
face_thickness = 1.0;
engraving_depth = 0.2;
front_chamfer = 0.15;
button_radius = 16.0; // ASSUMED; measure PCB!
pusher_height = 2.4; // ASSUMED; measure vertical stack!
pusher_tip_diameter = 2.4;
pusher_root_diameter = 3.6;
ring_lip_overhang = 0.8;
select_lip_overhang = 2.5;
select_guide_depth = 2.0;
select_guide_wall = 1.2;
select_axial_clearance = 0.3;
select_flange_thickness = 0.8;
lip_land = 0.3;
ring_lip_wall = 1.0;
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
assert(select_axial_clearance > 0.2);

menu_paths = [[[-3.457759765625,0.8176093750000001],[-2.9314785156250003,0.8176093750000001],[-2.566416015625,-0.0407343750000001],[-2.1989472656249998,0.8176093750000001],[-1.673697265625,0.8176093750000001],[-1.673697265625,-0.7863281250000002],[-2.064541015625,-0.7863281250000002],[-2.064541015625,0.38689062500000004],[-2.434072265625,-0.47798437500000013],[-2.6963535156250003,-0.47798437500000013],[-3.0658847656250003,0.38689062500000004],[-3.0658847656250003,-0.7863281250000002],[-3.457759765625,-0.7863281250000002],[-3.457759765625,0.8176093750000001]],[[-1.268501953125,0.8176093750000001],[-0.15268945312499982,0.8176093750000001],[-0.15268945312499982,0.5047968749999999],[-0.8549707031249998,0.5047968749999999],[-0.8549707031249998,0.20642187499999998],[-0.19428320312499991,0.20642187499999998],[-0.19428320312499991,-0.10639062500000007],[-0.8549707031249998,-0.10639062500000007],[-0.8549707031249998,-0.47385937500000014],[-0.1289707031249998,-0.47385937500000014],[-0.1289707031249998,-0.7863281250000002],[-1.268501953125,-0.7863281250000002],[-1.268501953125,0.8176093750000001]],[[0.23433007812500017,0.8176093750000001],[0.695986328125,0.8176093750000001],[1.279330078125,-0.2823906250000001],[1.279330078125,0.8176093750000001],[1.6715488281250006,0.8176093750000001],[1.6715488281250006,-0.7863281250000002],[1.209548828125,-0.7863281250000002],[0.6262050781249999,0.31367187499999993],[0.6262050781249999,-0.7863281250000002],[0.23433007812500017,-0.7863281250000002],[0.23433007812500017,0.8176093750000001]],[[2.0755410156250007,0.8176093750000001],[2.489072265625001,0.8176093750000001],[2.489072265625001,-0.14385937500000012],[2.5053144531250005,-0.3142734375000001],[2.554041015625,-0.4281406250000001],[2.6395488281250006,-0.49207812500000014],[2.7661347656250004,-0.5133906250000002],[2.8934941406250005,-0.49207812500000014],[2.979259765625,-0.4281406250000001],[3.027986328125001,-0.3142734375000001],[3.0442285156250004,-0.14385937500000012],[3.0442285156250004,0.8176093750000001],[3.4577597656250005,0.8176093750000001],[3.4577597656250005,-0.14385937500000012],[3.4150488281250007,-0.4410312500000001],[3.286916015625,-0.6512343750000001],[3.071470703125001,-0.7760156250000002],[2.7661347656250004,-0.8176093750000002],[2.4614863281250012,-0.7760156250000002],[2.2460410156250004,-0.6512343750000001],[2.118166015625001,-0.4410312500000001],[2.0755410156250007,-0.14385937500000012],[2.0755410156250007,0.8176093750000001]]];
next_paths = [[[-1.7,-0.9],[-1.7,0.9],[-0.35,0.0]],[[-0.1,-0.9],[-0.1,0.9],[1.25,0.0]],[[1.45,-0.9],[1.85,-0.9],[1.85,0.9],[1.45,0.9]]];
play_paths = [[[-1.7,-0.95],[-1.7,0.95],[-0.2,0.0]],[[0.1,-0.95],[0.55,-0.95],[0.55,0.95],[0.1,0.95]],[[0.95,-0.95],[1.4,-0.95],[1.4,0.95],[0.95,0.95]]];

module polys(paths) { for(p=paths) polygon(p); }
module icon_paths() {
    translate([0,button_radius]) polys(menu_paths);
    translate([button_radius,0]) polys(next_paths);
    translate([-button_radius,0]) mirror([1,0]) polys(next_paths);
    translate([0,-button_radius]) polys(play_paths);
}
module pusher() {
    taper = (pusher_root_diameter-pusher_tip_diameter)/2;
    translate([0,0,face_thickness]) {
        cylinder(h=taper,d1=pusher_root_diameter,d2=pusher_tip_diameter);
        translate([0,0,taper]) cylinder(h=pusher_height-taper,d=pusher_tip_diameter);
    }
}
module annular_face() {
    r=wheel_diameter/2; ri=opening_diameter/2; c=front_chamfer;
    rotate_extrude() polygon([[ri+c,0],[r-c,0],[r,c],[r,face_thickness],
                              [ri,face_thickness],[ri,c]]);
}
module ring_depth() {
    difference() {
        union() {
            annular_face();
            if(retention_lips) translate([0,0,face_thickness]) difference() {
                cylinder(h=select_guide_depth-face_thickness,d=opening_diameter+2*select_guide_wall);
                translate([0,0,-eps]) cylinder(h=select_guide_depth-face_thickness+2*eps,d=opening_diameter);
            }
            if(retention_lips) translate([0,0,face_thickness]) difference() {
                union() {
                    cylinder(h=ring_lip_overhang,d1=wheel_diameter,d2=wheel_diameter+2*ring_lip_overhang);
                    translate([0,0,ring_lip_overhang]) cylinder(h=lip_land,d=wheel_diameter+2*ring_lip_overhang);
                }
                translate([0,0,-eps]) cylinder(h=ring_lip_overhang+lip_land+2*eps,d=wheel_diameter-2*ring_lip_wall);
            }
            for(pos=[[0,button_radius],[button_radius,0],[0,-button_radius],[-button_radius,0]])
                translate([pos[0],pos[1],0]) pusher();
        }
        if(engraved_icons) translate([0,0,-eps]) linear_extrude(engraving_depth+eps) icon_paths();
    }
}
module select_depth() {
    r=select_diameter/2; c=front_chamfer;
    rotate_extrude() polygon([[0,0],[r-c,0],[r,c],[r,face_thickness],[0,face_thickness]]);
    if(retention_lips) {
        translate([0,0,face_thickness]) cylinder(h=select_flange_start-face_thickness,d=select_diameter);
        translate([0,0,select_flange_start]) cylinder(h=select_flange_thickness,d=select_diameter+2*select_lip_overhang);
        translate([0,0,select_flange_end]) cylinder(h=cap_depth-select_flange_end,d=pusher_tip_diameter);
    }
    else pusher();
}
// CAD front up. Use rigid rotations (not a mirrored part) for print orientation.
module ring() { mirror([0,0,1]) ring_depth(); }
module select_cap() { mirror([0,0,1]) select_depth(); }
if(mode=="print") {
    translate([-15,0,0]) rotate([180,0,0]) ring();
    translate([22,0,0]) rotate([180,0,0]) select_cap();
} else if(mode=="assembled") {
    ring(); select_cap();
} else if(mode=="ring") rotate([180,0,0]) ring();
else if(mode=="select") rotate([180,0,0]) select_cap();
