"""SC7A20H (LGA-12 2x2x1.0) の簡易 3D モデル (STEP AP214) を書き出す。

CAD を使わずに、直方体だけの B-rep を直接書く。寸法はデータシート (説明書 版本 0.7) の公称値:
  本体 2.0 × 2.0 mm、高さ A = 0.90〜1.05 (0.95 とする)
  端子 0.325 × 0.25 mm、ピッチ 0.5、左右の列の中心間 1.58、上下の行の中心間 1.50
原点は本体の中心、z = 0 が基板の表面。端子は厚さ 0.02 mm で本体の下に置く (見た目用)。
1番の目印として、本体上面の左上に薄い四角を置く。

使い方: python make_sc7a20h_step.py  → 同じフォルダに SC7A20H.stp
"""
from pathlib import Path

BODY = 2.0
HEIGHT = 0.95
PAD_T = 0.02          # 端子の厚さ (見た目用)
PAD_W, PAD_H = 0.325, 0.25
E1, D1 = 1.58, 1.50

# 部品面から見た端子の中心と向き (横長 = True)
PINS = [
    (-E1 / 2, 0.75, True), (-E1 / 2, 0.25, True), (-E1 / 2, -0.25, True), (-E1 / 2, -0.75, True),   # 1-4
    (-0.25, -D1 / 2, False), (0.25, -D1 / 2, False),                                                 # 5-6
    (E1 / 2, -0.75, True), (E1 / 2, -0.25, True), (E1 / 2, 0.25, True), (E1 / 2, 0.75, True),        # 7-10
    (0.25, D1 / 2, False), (-0.25, D1 / 2, False),                                                   # 11-12
]


class Step:
    def __init__(self):
        self.lines = []

    def add(self, text):
        self.lines.append(text)
        return f"#{len(self.lines)}"


def f(v):
    s = f"{v:.6f}".rstrip("0")
    return s + "0" if s.endswith(".") else s


def point(st, p):
    return st.add(f"CARTESIAN_POINT('',({f(p[0])},{f(p[1])},{f(p[2])}))")


def direction(st, d):
    return st.add(f"DIRECTION('',({f(d[0])},{f(d[1])},{f(d[2])}))")


def box(st, x0, y0, z0, x1, y1, z1):
    """直方体の MANIFOLD_SOLID_BREP を書き、その参照を返す"""
    corner = lambda i: (x1 if i & 1 else x0, y1 if i & 2 else y0, z1 if i & 4 else z0)  # noqa: E731
    verts = {}
    for i in range(8):
        verts[i] = st.add(f"VERTEX_POINT('',{point(st, corner(i))})")
    edges = {}

    def edge(a, b):
        key = (min(a, b), max(a, b))
        if key not in edges:
            pa, pb = corner(key[0]), corner(key[1])
            d = [pb[k] - pa[k] for k in range(3)]
            length = max(abs(c) for c in d)
            unit = [c / length for c in d]
            vec = st.add(f"VECTOR('',{direction(st, unit)},{f(length)})")
            line = st.add(f"LINE('',{point(st, pa)},{vec})")
            edges[key] = st.add(f"EDGE_CURVE('',{verts[key[0]]},{verts[key[1]]},{line},.T.)")
        return edges[key], a < b

    # 外から見て反時計回り (右手の法則で法線が外向き) の頂点の並び。頂点番号のビット: 1=x, 2=y, 4=z
    faces = [
        ((0, 2, 3, 1), (0, 0, -1)),
        ((4, 5, 7, 6), (0, 0, 1)),
        ((0, 1, 5, 4), (0, -1, 0)),
        ((2, 6, 7, 3), (0, 1, 0)),
        ((0, 4, 6, 2), (-1, 0, 0)),
        ((1, 3, 7, 5), (1, 0, 0)),
    ]
    face_refs = []
    for loop, normal in faces:
        oes = []
        for k in range(4):
            a, b = loop[k], loop[(k + 1) % 4]
            e, same = edge(a, b)
            oes.append(st.add(f"ORIENTED_EDGE('',*,*,{e},{'.T.' if same else '.F.'})"))
        lp = st.add(f"EDGE_LOOP('',({','.join(oes)}))")
        bound = st.add(f"FACE_OUTER_BOUND('',{lp},.T.)")
        p0, p1 = corner(loop[0]), corner(loop[1])
        ref = [p1[k] - p0[k] for k in range(3)]
        ref_len = max(abs(c) for c in ref)
        ref = [c / ref_len for c in ref]
        place = st.add(f"AXIS2_PLACEMENT_3D('',{point(st, p0)},{direction(st, normal)},{direction(st, ref)})")
        plane = st.add(f"PLANE('',{place})")
        face_refs.append(st.add(f"ADVANCED_FACE('',({bound}),{plane},.T.)"))
    shell = st.add(f"CLOSED_SHELL('',({','.join(face_refs)}))")
    return st.add(f"MANIFOLD_SOLID_BREP('',{shell})")


def colour(st, solid, rgb):
    c = st.add(f"COLOUR_RGB('',{f(rgb[0])},{f(rgb[1])},{f(rgb[2])})")
    fac = st.add(f"FILL_AREA_STYLE_COLOUR('',{c})")
    fill = st.add(f"FILL_AREA_STYLE('',({fac}))")
    sfa = st.add(f"SURFACE_STYLE_FILL_AREA({fill})")
    side = st.add(f"SURFACE_SIDE_STYLE('',({sfa}))")
    usage = st.add(f"SURFACE_STYLE_USAGE(.BOTH.,{side})")
    psa = st.add(f"PRESENTATION_STYLE_ASSIGNMENT(({usage}))")
    return st.add(f"STYLED_ITEM('',({psa}),{solid})")


def main():
    st = Step()
    app = st.add("APPLICATION_CONTEXT('automotive_design')")
    st.add(f"APPLICATION_PROTOCOL_DEFINITION('international standard','automotive_design',2000,{app})")
    pctx = st.add(f"PRODUCT_CONTEXT('',{app},'mechanical')")
    prod = st.add(f"PRODUCT('SC7A20H','SC7A20H','LGA-12 2x2x1.0',({pctx}))")
    st.add(f"PRODUCT_RELATED_PRODUCT_CATEGORY('part',$,({prod}))")
    pdf = st.add(f"PRODUCT_DEFINITION_FORMATION('','',{prod})")
    pdctx = st.add(f"PRODUCT_DEFINITION_CONTEXT('part definition',{app},'design')")
    pd = st.add(f"PRODUCT_DEFINITION('design','',{pdf},{pdctx})")
    pds = st.add(f"PRODUCT_DEFINITION_SHAPE('','',{pd})")

    length = st.add("( LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(.MILLI.,.METRE.) )")
    angle = st.add("( NAMED_UNIT(*) PLANE_ANGLE_UNIT() SI_UNIT($,.RADIAN.) )")
    solid_angle = st.add("( NAMED_UNIT(*) SI_UNIT($,.STERADIAN.) SOLID_ANGLE_UNIT() )")
    unc = st.add(f"UNCERTAINTY_MEASURE_WITH_UNIT(LENGTH_MEASURE(1.E-07),{length},'distance_accuracy_value','confusion accuracy')")
    ctx = st.add(f"( GEOMETRIC_REPRESENTATION_CONTEXT(3) GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT(({unc})) "
                 f"GLOBAL_UNIT_ASSIGNED_CONTEXT(({length},{angle},{solid_angle})) "
                 f"REPRESENTATION_CONTEXT('Context3D','3D Context') )")

    origin = st.add(f"AXIS2_PLACEMENT_3D('',{point(st, (0, 0, 0))},{direction(st, (0, 0, 1))},{direction(st, (1, 0, 0))})")

    h = BODY / 2
    body = box(st, -h, -h, PAD_T, h, h, PAD_T + HEIGHT)
    mark = box(st, -h + 0.15, h - 0.45, PAD_T + HEIGHT, -h + 0.45, h - 0.15, PAD_T + HEIGHT + 0.005)
    pads = []
    for x, y, wide in PINS:
        w, d = (PAD_W, PAD_H) if wide else (PAD_H, PAD_W)
        pads.append(box(st, x - w / 2, y - d / 2, 0, x + w / 2, y + d / 2, PAD_T))

    solids = [body, mark] + pads
    rep = st.add(f"ADVANCED_BREP_SHAPE_REPRESENTATION('SC7A20H',({origin},{','.join(solids)}),{ctx})")
    st.add(f"SHAPE_DEFINITION_REPRESENTATION({pds},{rep})")

    styled = [colour(st, body, (0.15, 0.15, 0.15)), colour(st, mark, (0.85, 0.85, 0.85))]
    styled += [colour(st, p, (0.85, 0.75, 0.35)) for p in pads]
    st.add(f"MECHANICAL_DESIGN_GEOMETRIC_PRESENTATION_REPRESENTATION('',({','.join(styled)}),{ctx})")

    out = ["ISO-10303-21;", "HEADER;",
           "FILE_DESCRIPTION(('SC7A20H LGA-12 2x2x1.0, simplified model'),'2;1');",
           "FILE_NAME('SC7A20H.stp','2026-11-23T00:00:00',(''),(''),'make_sc7a20h_step.py','','');",
           "FILE_SCHEMA(('AUTOMOTIVE_DESIGN { 1 0 10303 214 1 1 1 1 }'));",
           "ENDSEC;", "DATA;"]
    for i, line in enumerate(st.lines, 1):
        body_text = line[1:-1] if line.startswith("( ") else line
        out.append(f"#{i}={'(' + body_text + ')' if line.startswith('( ') else line};")
    out += ["ENDSEC;", "END-ISO-10303-21;"]
    path = Path(__file__).with_name("SC7A20H.stp")
    path.write_text("\n".join(out) + "\n", encoding="ascii")
    print(f"{path}: {len(st.lines)} entities, {len(solids)} solids")


if __name__ == "__main__":
    main()
