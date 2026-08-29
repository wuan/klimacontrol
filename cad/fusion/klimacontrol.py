"""KlimaControl — Gira System 55 case. Fusion entry point.

This is the ONLY file to register as a Fusion script:

    Utilities -> Add-Ins -> Scripts and Add-Ins -> Scripts -> "+" (Create)
    -> pick this .py, then Run

Everything it builds lives in the `kc` package next to it, one module per
component. To add a component, write `kc/<name>.py` with a
`build(comp, p, g)` and add a line to COMPONENTS below — that is the whole
registration.

Dimensions are in `kc/params.py`; positions derived from them, and the fit
checks, are in `kc/layout.py`. Neither imports Fusion, so

    python3 cad/fusion/kc/layout.py

prints the same report the dialog shows, without opening Fusion.

Currently at step 1 of the rebuild: the front plate's viewport only. The rib,
antenna pocket, clamp posts, vent grille and the whole body are still to come;
`klimacontrol_old.py` is the earlier one-shot script kept as reference.
"""

import importlib
import os
import sys
import traceback

import adsk.core
import adsk.fusion

# Build stand-ins for the boards alongside the case. Set to False before
# exporting for print.
PLACEHOLDERS = False

PKG = "kc"
HERE = os.path.dirname(os.path.realpath(__file__))
LOG = os.path.join(os.path.expanduser("~"), "klimacontrol_cad_log.txt")


def _load():
    """Import the kc package, discarding anything cached from a previous run.

    Fusion keeps one Python interpreter alive for the whole session, so a
    plain `import kc.faceplate` on the second run hands back the module object
    from the first and silently ignores every edit made in between. Dropping
    the package from sys.modules first is what makes the edit-and-re-run loop
    actually work.
    """
    if HERE not in sys.path:
        sys.path.insert(0, HERE)
    for name in [n for n in sys.modules
                 if n == PKG or n.startswith(PKG + ".")]:
        del sys.modules[name]
    return {name: importlib.import_module("{}.{}".format(PKG, name))
            for name in ("params", "layout", "primitives",
                         "faceplate", "placeholders")}


def _log(text):
    """Fusion message boxes cannot be copied from, so mirror to a file."""
    try:
        with open(LOG, "w") as fh:
            fh.write(text)
    except Exception:
        pass


def run(context):
    app = adsk.core.Application.get()
    ui = app.userInterface
    step = "loading kc package"
    try:
        kc = _load()
        p = kc["params"].P
        g = kc["layout"].geom(p)

        # The components to build, in order. One line per component; the
        # builder signature is always build(comp, p, g).
        components = [
            ("faceplate", kc["faceplate"].build),
        ]
        if PLACEHOLDERS:
            components.append(("ph_display", kc["placeholders"].display))

        step = "new document"
        app.documents.add(adsk.core.DocumentTypes.FusionDesignDocumentType)
        design = adsk.fusion.Design.cast(app.activeProduct)
        design.designType = adsk.fusion.DesignTypes.ParametricDesignType
        root = design.rootComponent

        # The root component's name tracks the document name, and on an
        # unsaved document Fusion rejects the assignment. Not worth failing on.
        try:
            root.name = "KlimaControl Gira 55"
        except Exception:
            pass

        for name, build in components:
            step = name
            build(kc["primitives"].add_component(root, name), p, g)

        text = "Built: {}\n\n{}".format(
            ", ".join(n for n, _ in components), kc["layout"].summary(p))
        _log("OK\n\n" + text)
        ui.messageBox(text)
    except Exception:
        report = "FAILED while building: {}\n\n{}".format(
            step, traceback.format_exc())
        _log(report)
        if ui:
            ui.messageBox(report + "\n\nAlso written to:\n" + LOG)
