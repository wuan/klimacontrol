"""KlimaControl — Gira System 55 case, model package.

The Fusion entry point is `../klimacontrol.py`. Nothing in here calls into
Fusion's document API on import, so the whole package can be imported outside
Fusion to check the arithmetic — see `kc.layout`.

Modules:

    params        the P dict. The only place a dimension is typed.
    layout        positions derived from P, plus the fit checks. No Fusion.
    primitives    the thin sketch-and-extrude layer over the Fusion API.
    faceplate     the front plate.
    placeholders  stand-ins for the boards, for interference checks.
"""
