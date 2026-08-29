"""The thin sketch-and-extrude layer over the Fusion API.

Every component builder works through these, so no builder ever touches a
ValueInput or an ExtrudeInput directly, and no builder ever has to remember
that Fusion's internal unit is the centimetre.

Add primitives here as components need them — a slot, a cylinder — rather than
inlining sketch code in a builder.
"""

import adsk.core
import adsk.fusion

# Fusion's API is unitless-internal in centimetres. Every dimension in this
# package is in millimetres and goes through vi()/pt(), which apply the factor.
MM = 0.1

NEW = adsk.fusion.FeatureOperations.NewBodyFeatureOperation
JOIN = adsk.fusion.FeatureOperations.JoinFeatureOperation
CUT = adsk.fusion.FeatureOperations.CutFeatureOperation


def vi(v):
    return adsk.core.ValueInput.createByReal(v * MM)


def pt(x, y):
    return adsk.core.Point3D.create(x * MM, y * MM, 0.0)


def _extrude(comp, prof, z0, z1, op):
    ext = comp.features.extrudeFeatures
    ei = ext.createInput(prof, op)
    ei.setDistanceExtent(False, vi(z1 - z0))
    if abs(z0) > 1e-9:
        ei.startExtent = adsk.fusion.OffsetStartDefinition.create(vi(z0))
    return ext.add(ei)


def rect(comp, x0, y0, x1, y1, z0, z1, op):
    """Extrude a corner-to-corner rectangle between two z planes."""
    sk = comp.sketches.add(comp.xYConstructionPlane)
    sk.sketchCurves.sketchLines.addTwoPointRectangle(pt(x0, y0), pt(x1, y1))
    return _extrude(comp, sk.profiles.item(0), z0, z1, op)


def boxc(comp, cx, cy, w, h, z0, z1, op):
    """Same, but centred on (cx, cy)."""
    return rect(comp, cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2, z0, z1, op)


def add_component(root, name):
    occ = root.occurrences.addNewComponent(adsk.core.Matrix3D.create())
    occ.component.name = name
    return occ.component
