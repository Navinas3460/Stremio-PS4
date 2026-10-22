#!/usr/bin/env python3
"""Converts a CFF-flavoured OpenType font (.otf) to TrueType outlines (.ttf).

Sony's FreeType on the PS4 (2.5.1) opens CFF fonts but crashes rendering
them, so the Noto CJK fallback ships converted. It also rejects a long
`loca` table of 0x40000 bytes or more (src/truetype/ttpload.c), which a font
of 65535 glyphs needs, so glyphs no character or layout feature reaches are
dropped as well:

    python3 tools/otf2ttf.py NotoSansCJKsc-Medium.otf assets/fonts/NotoSansCJKsc-Medium.ttf

Needs fontTools (pip install fonttools). Adapted from fontTools'
Snippets/otf2ttf.py, Copyright (c) 2017 Just van Rossum, MIT licence
(third_party/fonttools/LICENSE).
"""
import io
import sys

from fontTools import subset
from fontTools.pens.cu2quPen import Cu2QuPen
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib import TTFont, newTable

MAX_ERROR = 1.0  # font units: how far quadratic curves may stray from the cubic ones
MAX_LOCA = 0x40000  # FreeType 2.5.1's limit, in bytes


def main(src, dst):
    font = TTFont(src)
    order = font.getGlyphOrder()
    glyph_set = font.getGlyphSet()
    glyphs = {}
    for name in order:
        pen = TTGlyphPen(glyph_set)
        glyph_set[name].draw(Cu2QuPen(pen, MAX_ERROR, reverse_direction=True))
        glyphs[name] = pen.glyph()

    font["loca"] = newTable("loca")
    font["glyf"] = glyf = newTable("glyf")
    glyf.glyphOrder = order
    glyf.glyphs = glyphs
    for tag in ("CFF ", "VORG", "DSIG"):  # DSIG: the signature no longer matches
        if tag in font:
            del font[tag]
    glyf.compile(font)

    font["maxp"] = maxp = newTable("maxp")
    maxp.tableVersion = 0x00010000
    for attr in ("maxTwilightPoints", "maxStorage", "maxFunctionDefs", "maxInstructionDefs",
                 "maxStackElements", "maxSizeOfInstructions", "maxComponentElements"):
        setattr(maxp, attr, 0)
    maxp.maxZones = 1

    post = font["post"]
    post.formatType = 3.0  # no glyph names: 65535 of them overflow format 2
    post.extraNames = []
    post.mapping = {}
    post.glyphOrder = order

    font.sfntVersion = "\000\001\000\000"

    # Unreachable glyphs out: keep every character, feature and name.
    buffer = io.BytesIO()
    font.save(buffer)
    buffer.seek(0)
    font = TTFont(buffer)
    options = subset.Options()
    options.layout_features = ["*"]
    options.name_IDs = ["*"]
    options.name_languages = ["*"]
    options.notdef_outline = True
    options.glyph_names = False
    subsetter = subset.Subsetter(options)
    subsetter.populate(unicodes=font.getBestCmap().keys())
    subsetter.subset(font)
    font.save(dst)

    loca = TTFont(dst).reader.tables["loca"].length
    if loca >= MAX_LOCA:
        sys.exit(f"{dst}: loca is {loca} bytes, the PS4's FreeType rejects {MAX_LOCA} or more")
    print(f"{dst}: {len(font.getGlyphOrder())} glyphs, loca {loca} bytes")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit("usage: otf2ttf.py input.otf output.ttf")
    main(sys.argv[1], sys.argv[2])
