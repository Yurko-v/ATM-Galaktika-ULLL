#!/usr/bin/env python3

import json
import re
import sys

KIND = {"P": "P", "D": "D", "R": "R"}


def kind_of(category):
    c = category.strip().upper()
    return KIND.get(c) or KIND.get(c[-1:], "R")

COORD = re.compile(r"^([NSEW])(\d+(?:\.\d+)*)$", re.IGNORECASE)


def parse_half(text):
    m = COORD.match(text.strip())
    if not m:
        return None, None
    hemi = m.group(1).upper()
    parts = m.group(2).split(".")
    if len(parts) >= 3:
        deg = float(parts[0]) + float(parts[1]) / 60.0 + float(parts[2]) / 3600.0
        if len(parts) >= 4 and parts[3]:
            deg += float(parts[3]) / (10.0 ** len(parts[3])) / 3600.0
    else:
        deg = float(".".join(parts))
    if hemi in ("S", "W"):
        deg = -deg
    return ("lat" if hemi in ("N", "S") else "lon"), deg


def parse_point(a, b):
    ka, va = parse_half(a)
    kb, vb = parse_half(b)
    if ka is None or kb is None or ka == kb:
        return None
    lat, lon = (va, vb) if ka == "lat" else (vb, va)
    if not (-90.0 <= lat <= 90.0 and -180.0 <= lon <= 180.0):
        return None
    return round(lat, 6), round(lon, 6)


def level_text(fl):
    if fl <= 0:
        return "GND"
    if fl >= 999:
        return "UNL"
    return "FL%03d" % fl


def convert(path):
    areas = []
    cur = None

    def flush():
        if cur is None:
            return
        if len(cur.get("Points", [])) < 3 and "Circle" not in cur:
            return
        if len(cur.get("Points", [])) < 3:
            cur.pop("Points", None)
        areas.append(cur)

    with open(path, encoding="utf-8-sig", errors="replace") as fh:
        for raw in fh:
            line = raw.strip()
            if not line or line.startswith(";"):
                continue

            if line.startswith("AREA:"):
                flush()
                f = line.split(":", 2)
                cur = {"Id": f[2].strip() if len(f) >= 3 else "", "Points": []}
                continue

            if cur is None:
                continue

            if line.startswith("CATEGORY:"):
                cur["Type"] = kind_of(line[9:])
            elif line.startswith("ACTIVE:"):
                cur["Activation"] = line[7:].strip()
            elif line.startswith("LIMITS:"):
                f = line.split(":")
                if len(f) >= 3:
                    try:
                        cur["Lower"] = level_text(int(float(f[1])))
                        cur["Upper"] = level_text(int(float(f[2])))
                    except ValueError:
                        pass
            elif line.startswith("LABEL:"):
                f = line.split(":", 3)
                if len(f) >= 3:
                    p = parse_point(f[1], f[2])
                    if p:
                        cur["Label"] = [p[0], p[1]]
                if len(f) >= 4:
                    text = f[3].strip()
                    if text and text != cur.get("Id"):
                        cur["Name"] = text
            elif line.startswith("USERTEXT:"):
                note = line[9:].strip()
                if note:
                    cur["Note"] = (cur["Note"] + "\n" + note) if cur.get("Note") else note
            elif line.startswith("CIRCLE:"):
                f = line.split(":")
                if len(f) >= 4:
                    p = parse_point(f[1], f[2])
                    try:
                        radius = float(f[3])
                    except ValueError:
                        radius = 0.0
                    if p and radius > 0:
                        cur["Circle"] = {"Center": [p[0], p[1]], "RadiusNM": round(radius, 3)}
            else:
                f = line.replace(",", " ").split()
                if len(f) >= 2:
                    p = parse_point(f[0], f[1])
                    if p:
                        cur["Points"].append([p[0], p[1]])

    flush()
    return areas


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1

    areas = convert(sys.argv[1])

    body = ",\n".join("    " + json.dumps(a, ensure_ascii=False) for a in areas)
    out = '{\n  "Items": [\n%s\n  ]\n}\n' % body

    with open(sys.argv[2], "w", encoding="utf-8", newline="\n") as fh:
        fh.write(out)

    kinds = {}
    for a in areas:
        kinds[a.get("Type", "R")] = kinds.get(a.get("Type", "R"), 0) + 1
    print("%s: %d areas %s" % (sys.argv[2], len(areas), kinds))
    return 0


if __name__ == "__main__":
    sys.exit(main())
