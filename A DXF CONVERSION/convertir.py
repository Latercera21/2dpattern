import json, ezdxf

def quad_bezier_points(p0, cp, p1, steps=20):
    pts = []
    for i in range(steps + 1):
        t = i / steps
        x = (1-t)**2 * p0[0] + 2*(1-t)*t * cp[0] + t**2 * p1[0]
        y = (1-t)**2 * p0[1] + 2*(1-t)*t * cp[1] + t**2 * p1[1]
        pts.append((x, y))
    return pts

def cubic_bezier_points(p0, cp1, cp2, p1, steps=20):
    pts = []
    for i in range(steps + 1):
        t = i / steps
        x = (1-t)**3*p0[0] + 3*(1-t)**2*t*cp1[0] + 3*(1-t)*t**2*cp2[0] + t**3*p1[0]
        y = (1-t)**3*p0[1] + 3*(1-t)**2*t*cp1[1] + 3*(1-t)*t**2*cp2[1] + t**3*p1[1]
        pts.append((x, y))
    return pts

with open("input.json", encoding="utf-8") as f:
    data = json.load(f)

px_per_cm = data["pxPerCm"]

doc = ezdxf.new("R2000")
msp = doc.modelspace()

for fig_idx, fig in enumerate(data["figures"]):
    layer_name = f"FIGURA_{fig_idx+1:02d}"
    doc.layers.new(name=layer_name, dxfattribs={"color": (fig_idx % 7) + 1})

    verts = fig["vertices"]
    edges = fig["edges"]
    poly_pts = []

    for edge in edges:
        s = edge["start"]
        e = edge["end"]
        p0 = (verts[s]["x"] / px_per_cm, verts[s]["y"] / px_per_cm)
        p1 = (verts[e]["x"] / px_per_cm, verts[e]["y"] / px_per_cm)

        if not edge["curved"]:
            if not poly_pts:
                poly_pts.append(p0)
            poly_pts.append(p1)
        else:
            cx = edge.get("controlX")
            cy = edge.get("controlY")
            cx2 = edge.get("control2X")
            cy2 = edge.get("control2Y")

            if edge.get("cubic") and cx2 is not None:
                cp1 = (cx / px_per_cm, cy / px_per_cm)
                cp2 = (cx2 / px_per_cm, cy2 / px_per_cm)
                seg = cubic_bezier_points(p0, cp1, cp2, p1)
            else:
                cp = (cx / px_per_cm, cy / px_per_cm)
                seg = quad_bezier_points(p0, cp, p1)

            if not poly_pts:
                poly_pts.extend(seg)
            else:
                poly_pts.extend(seg[1:])

    # Invertir Y para DXF estandar
    dxf_pts = [(x, -y) for x, y in poly_pts]

    msp.add_lwpolyline(dxf_pts, close=True, dxfattribs={"layer": layer_name})

doc.saveas("output.dxf")
print(f"Listo: {len(data['figures'])} figuras exportadas -> output.dxf")