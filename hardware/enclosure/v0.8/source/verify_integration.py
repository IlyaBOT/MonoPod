"""Check the exported display transfer and the revised connector fit."""

import json

import cadquery as cq

import build_enclosure as enclosure


ROOT = enclosure.ROOT
REFERENCE = ROOT / "reference/tft_52x30p4"


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8"))


def load_step(path):
    shape = cq.importers.importStep(str(path)).val()
    assert shape.isValid(), path
    return shape


def same_solid(first, second):
    return first.cut(second).Volume() + second.cut(first).Volume()


def axial_thickness(shape, x, depth, start_y, end_y):
    probe = enclosure.ypill(0.02, 0.02, start_y, end_y, x, depth)
    material = shape.intersect(probe)
    assert len(material.Solids()) == 1
    return material.BoundingBox().ylen


def main():
    build_report = read_json(ROOT / "validation/geometry_check.json")
    assembly = build_report["assembly"]
    assert build_report["passed"] and build_report["full_roundtrip"]
    assert build_report["generator_sha256"] == enclosure.sha(ROOT / "source/build_enclosure.py")
    assert build_report["parameters_sha256"] == enclosure.sha(ROOT / "source/parameters.json")
    assert assembly["passed"] and assembly["installation"]["passed"] and assembly["usb_cable_fit"]["passed"]
    for name, info in build_report["parts"].items():
        assert enclosure.sha(ROOT / "cad" / f"{name}.step") == info["step_sha256"]
        assert enclosure.sha(ROOT / "stl" / f"{name}.stl") == info["stl_sha256"]
    for name, digest in read_json(REFERENCE / "SHA256SUMS.json").items():
        assert enclosure.sha(REFERENCE / name) == digest, name

    transferred = {}
    names = {"display_window_coupon": "Probnik", "display_lens_carrier": "Ramka",
             "front_panel": "Front_Panel", "back_chassis": "Back_Chassis"}
    for output_name, original_name in names.items():
        original = load_step(REFERENCE / "cad" / f"{original_name}.step")
        current = load_step(ROOT / "cad" / f"{output_name}.step")
        difference = same_solid(original, current)
        assert difference < 1e-5, (output_name, difference)
        transferred[output_name] = difference

    params = read_json(ROOT / "source/parameters.json")
    model = enclosure.Model(params).build(False)
    lens = model.acrylic_datums()
    display = params["display"]
    front = model.parts["front_panel"].shape
    carrier = model.parts["display_lens_carrier"].shape
    coupon = load_step(ROOT / "cad/display_window_coupon.step")
    assert abs(display["pcb_front_depth"] - 6.0) < 1e-8
    assert lens["panel_glue_overlap"] == 2.0
    mount_x, mount_y = zip(*model.display_mounts())
    pitch = [max(mount_x) - min(mount_x), max(mount_y) - min(mount_y)]
    assert abs(pitch[0] - 52.0) < 1e-8 and abs(pitch[1] - 30.4) < 1e-8
    assert abs(display["pcb_front_depth"] - display["glass_above_pcb"] - lens["display_relief_front"] - .3) < 1e-8

    allowance = display["view_registration_allowance"]
    optics = model.display_shape(display["view_width"] + 2 * allowance,
        display["view_height"] + 2 * allowance, -.01,
        display["pcb_front_depth"] - display["glass_above_pcb"],
        y_offset=display["active_y_offset"])
    optics_overlap = {name: enclosure.volume_overlap(optics, solid) for name, solid in
        [("panel", front), ("carrier", carrier), ("coupon", coupon)]}
    assert max(optics_overlap.values()) < 1e-5
    installation_samples = []
    for distance in (0., 0.1, 0.5, 1., 2., 4., 10.):
        opening = carrier.translate((0, 0, -distance))
        row = {"rear_offset_mm": distance,
               "panel_overlap_mm3": enclosure.volume_overlap(opening, front),
               "coupon_overlap_mm3": enclosure.volume_overlap(opening, coupon)}
        assert max(row["panel_overlap_mm3"], row["coupon_overlap_mm3"]) < 1e-5, row
        installation_samples.append(row)
    glue_offset = params["lens_carrier"]["panel_pocket_axial_clearance"]
    glue = model.refs["panel_glue_adhesive_envelope"]
    bonded_area = glue.Volume() / glue_offset
    assert glue.isValid() and len(glue.Solids()) == 1 and bonded_area > 0

    old = read_json(REFERENCE / "source/parameters.json")
    baseline = enclosure.Model(old)
    usb = model.charger_datums()
    jack = model.jack_datums()
    before_usb = baseline.charger_datums()
    before_jack = baseline.jack_datums()
    dx = usb["x"] - before_usb["x"]
    dz = before_usb["axis_depth"] - usb["axis_depth"]
    assert abs(dx - 1) < 1e-8 and abs(dz - .5) < 1e-8
    assert abs(jack["x"] - before_jack["x"]) < 1e-8
    assert abs(jack["axis_depth"] - before_jack["axis_depth"]) < 1e-8
    insert = load_step(ROOT / "cad/bottom_connector_insert.step")
    cable = model.usb_cable_datums()
    outer_y = -params["case"]["height"] / 2
    usb_web = axial_thickness(insert, usb["x"] + 5, usb["axis_depth"],
        outer_y - .1, cable["well_rear_y"] + .1)
    jack_web = axial_thickness(insert, jack["x"] + 4, jack["axis_depth"],
        outer_y - .1, jack["shoulder_y"] + .1)
    assert abs(usb_web - .8) < 1e-8 and abs(jack_web - 1.) < 1e-8

    # Compare the void itself, in a thin slice through the web.
    section_y = (cable["well_floor_y"] + cable["well_rear_y"]) / 2
    scope = enclosure.ypill(11, 5, section_y - .005, section_y + .005, usb["x"], usb["axis_depth"])
    old_insert = baseline.connector_insert().translate((dx, 0, dz))
    aperture_difference = same_solid(scope.cut(insert), scope.cut(old_insert))
    assert aperture_difference < 1e-5

    hood = params["usb_cable"]
    extra_hood = enclosure.ypill(hood["body_width"] + hood["measurement_size_allowance"],
        hood["body_height"] + hood["measurement_size_allowance"], cable["body_rear_y"],
        cable["body_front_y"] + hood["tip_length_allowance"] + .8, usb["x"], usb["axis_depth"])
    hood_solids = [insert, front, model.parts["back_chassis"].shape]
    hood_solids.extend(model.refs[name] for name in model.groups["rigid_electronics"] + ["jack_nut"])
    extra_hood_overlap = max(enclosure.volume_overlap(extra_hood, solid) for solid in hood_solids)
    assert extra_hood_overlap < 1e-5

    laser = read_json(ROOT / "validation/laser_check.json")
    assert laser["passed"] and laser["lens_mm"] == [45, 38, 1] and not laser["kerf_compensation_applied"]
    report = {"passed": True, "version": params["version"], "printable_parts": len(build_report["parts"]),
        "display_pitch_xy_mm": pitch, "display_depth_mm": display["pcb_front_depth"],
        "supplied_step_differences_mm3": transferred,
        "mount_glue_gap_mm": glue_offset, "mount_glue_area_mm2": bonded_area,
        "optical_overlap_mm3": optics_overlap, "carrier_insertion_samples": installation_samples,
        "usb_shift_xz_mm": [dx, dz], "usb_aperture_difference_mm3": aperture_difference,
        "usb_hood_pocket_mm": [cable["well_width"], cable["well_height"], cable["well_depth"]],
        "usb_web_mm": usb_web, "jack_web_mm": jack_web,
        "extra_hood_insertion_overlap_mm3": extra_hood_overlap,
        "combined_revision_physically_tested": False}
    (ROOT / "validation/integration_check.json").write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("PASS supplied display geometry, optical opening, installation, connectors and laser exports")


if __name__ == "__main__":
    main()
