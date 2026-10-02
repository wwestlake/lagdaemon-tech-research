import math
from pathlib import Path


OUT_DIR = Path(__file__).resolve().parent
OBJ_PATH = OUT_DIR / "djehuti_head.obj"
MTL_PATH = OUT_DIR / "djehuti_head.mtl"
README_PATH = OUT_DIR / "README.md"


class Mesh:
    def __init__(self):
        self.vertices = []
        self.faces = []

    def v(self, p):
        self.vertices.append(p)
        return len(self.vertices)

    def face(self, material, ids):
        self.faces.append((material, ids))

    def add_uv_ellipsoid(self, material, center, radius, seg=32, rings=16, theta_min=0.0, theta_max=math.pi):
        grid = []
        for r in range(rings + 1):
            theta = theta_min + (theta_max - theta_min) * r / rings
            row = []
            for s in range(seg):
                phi = 2.0 * math.pi * s / seg
                x = center[0] + radius[0] * math.sin(theta) * math.cos(phi)
                y = center[1] + radius[1] * math.cos(theta)
                z = center[2] + radius[2] * math.sin(theta) * math.sin(phi)
                row.append(self.v((x, y, z)))
            grid.append(row)

        for r in range(rings):
            for s in range(seg):
                self.face(material, [
                    grid[r][s],
                    grid[r][(s + 1) % seg],
                    grid[r + 1][(s + 1) % seg],
                    grid[r + 1][s],
                ])

    def add_tube_x(self, material, points, radii_yz, seg=18, cap=True):
        rings = []
        for (x, y, z), (ry, rz) in zip(points, radii_yz):
            row = []
            for s in range(seg):
                a = 2.0 * math.pi * s / seg
                row.append(self.v((x, y + ry * math.cos(a), z + rz * math.sin(a))))
            rings.append(row)

        for i in range(len(rings) - 1):
            for s in range(seg):
                self.face(material, [
                    rings[i][s],
                    rings[i][(s + 1) % seg],
                    rings[i + 1][(s + 1) % seg],
                    rings[i + 1][s],
                ])

        if cap:
            start = self.v(points[0])
            end = self.v(points[-1])
            for s in range(seg):
                self.face(material, [start, rings[0][s], rings[0][(s + 1) % seg]])
                self.face(material, [end, rings[-1][(s + 1) % seg], rings[-1][s]])

    def add_tube_y(self, material, points, radii_xz, seg=24, cap=True):
        rings = []
        for (x, y, z), (rx, rz) in zip(points, radii_xz):
            row = []
            for s in range(seg):
                a = 2.0 * math.pi * s / seg
                row.append(self.v((x + rx * math.cos(a), y, z + rz * math.sin(a))))
            rings.append(row)

        for i in range(len(rings) - 1):
            for s in range(seg):
                self.face(material, [
                    rings[i][s],
                    rings[i][(s + 1) % seg],
                    rings[i + 1][(s + 1) % seg],
                    rings[i + 1][s],
                ])

        if cap:
            start = self.v(points[0])
            end = self.v(points[-1])
            for s in range(seg):
                self.face(material, [start, rings[0][(s + 1) % seg], rings[0][s]])
                self.face(material, [end, rings[-1][s], rings[-1][(s + 1) % seg]])

    def add_box(self, material, center, size):
        cx, cy, cz = center
        sx, sy, sz = size[0] / 2.0, size[1] / 2.0, size[2] / 2.0
        pts = [
            (cx - sx, cy - sy, cz - sz), (cx + sx, cy - sy, cz - sz),
            (cx + sx, cy + sy, cz - sz), (cx - sx, cy + sy, cz - sz),
            (cx - sx, cy - sy, cz + sz), (cx + sx, cy - sy, cz + sz),
            (cx + sx, cy + sy, cz + sz), (cx - sx, cy + sy, cz + sz),
        ]
        ids = [self.v(p) for p in pts]
        for f in [(1, 2, 3, 4), (5, 8, 7, 6), (1, 5, 6, 2), (2, 6, 7, 3), (3, 7, 8, 4), (4, 8, 5, 1)]:
            self.face(material, [ids[i - 1] for i in f])

    def write_obj(self, obj_path, mtl_name):
        with obj_path.open("w", encoding="utf-8", newline="\n") as f:
            f.write("# Stylized 3D Djehuti/Thoth ibis head bust\n")
            f.write(f"mtllib {mtl_name}\n")
            for x, y, z in self.vertices:
                f.write(f"v {x:.6f} {y:.6f} {z:.6f}\n")
            current = None
            for material, ids in self.faces:
                if material != current:
                    f.write(f"g {material}\n")
                    f.write(f"usemtl {material}\n")
                    current = material
                f.write("f " + " ".join(str(i) for i in ids) + "\n")


def build_model():
    mesh = Mesh()

    # Headdress: rounded navy back mass plus two long lappets.
    mesh.add_uv_ellipsoid("deep_blue_headdress", center=(0.22, 1.23, 0.0), radius=(0.43, 0.78, 0.36), seg=36, rings=18)
    mesh.add_box("deep_blue_headdress", center=(-0.02, 0.73, -0.24), size=(0.18, 0.78, 0.11))
    mesh.add_box("deep_blue_headdress", center=(-0.02, 0.73, 0.24), size=(0.18, 0.78, 0.11))

    # Ibis head and neck.
    mesh.add_uv_ellipsoid("green_ibis_head", center=(-0.18, 1.62, 0.0), radius=(0.28, 0.33, 0.23), seg=32, rings=16)
    neck_points = [
        (-0.03, 0.55, 0.0),
        (-0.04, 0.78, 0.0),
        (-0.10, 1.02, 0.0),
        (-0.17, 1.25, 0.0),
        (-0.20, 1.45, 0.0),
    ]
    neck_radii = [(0.15, 0.14), (0.16, 0.14), (0.15, 0.13), (0.13, 0.11), (0.11, 0.10)]
    mesh.add_tube_y("green_ibis_head", neck_points, neck_radii, seg=28)

    # Long curved ibis beak.
    beak_points = [
        (-0.38, 1.58, 0.0),
        (-0.62, 1.53, 0.0),
        (-0.88, 1.43, 0.0),
        (-1.14, 1.30, 0.0),
        (-1.40, 1.14, 0.0),
        (-1.62, 0.96, 0.0),
    ]
    beak_radii = [(0.075, 0.050), (0.065, 0.044), (0.054, 0.038), (0.043, 0.031), (0.032, 0.024), (0.016, 0.012)]
    mesh.add_tube_x("black_beak", beak_points, beak_radii, seg=20)

    # Eyes.
    mesh.add_uv_ellipsoid("black_eye", center=(-0.32, 1.69, -0.145), radius=(0.025, 0.025, 0.014), seg=12, rings=8)
    mesh.add_uv_ellipsoid("black_eye", center=(-0.32, 1.69, 0.145), radius=(0.025, 0.025, 0.014), seg=12, rings=8)

    # Collar/base accents so the head reads as an Egyptian bust.
    mesh.add_tube_y("gold_collar", [(-0.02, 0.42, 0.0), (-0.02, 0.49, 0.0)], [(0.46, 0.34), (0.43, 0.31)], seg=36)
    mesh.add_tube_y("red_collar", [(-0.02, 0.50, 0.0), (-0.02, 0.56, 0.0)], [(0.37, 0.27), (0.33, 0.24)], seg=36)
    mesh.add_box("gold_collar", center=(-0.02, 0.96, -0.33), size=(0.22, 0.08, 0.08))
    mesh.add_box("gold_collar", center=(-0.02, 0.96, 0.33), size=(0.22, 0.08, 0.08))

    return mesh


def write_mtl():
    MTL_PATH.write_text(
        """# Materials for stylized Djehuti head
newmtl green_ibis_head
Kd 0.05 0.48 0.30
Ka 0.02 0.16 0.10
Ks 0.20 0.20 0.18
Ns 35

newmtl black_beak
Kd 0.01 0.01 0.012
Ka 0.00 0.00 0.00
Ks 0.20 0.20 0.22
Ns 50

newmtl deep_blue_headdress
Kd 0.00 0.10 0.33
Ka 0.00 0.03 0.10
Ks 0.16 0.18 0.25
Ns 25

newmtl black_eye
Kd 0.00 0.00 0.00
Ka 0.00 0.00 0.00
Ks 0.80 0.80 0.80
Ns 90

newmtl gold_collar
Kd 1.00 0.68 0.12
Ka 0.25 0.14 0.02
Ks 0.70 0.55 0.20
Ns 80

newmtl red_collar
Kd 0.62 0.18 0.10
Ka 0.16 0.04 0.02
Ks 0.20 0.10 0.05
Ns 25
""",
        encoding="utf-8",
    )


def write_readme():
    README_PATH.write_text(
        f"""# Djehuti Head 3D Model

Generated stylized OBJ bust of Djehuti/Thoth based on the ibis-headed reference.

Files:

- `{OBJ_PATH.name}` - mesh
- `{MTL_PATH.name}` - materials
- `generate_djehuti_head.py` - procedural source

Design notes:

- green ibis head and curved neck
- long dark curved ibis beak
- deep blue Egyptian headdress with side lappets
- gold/red collar accents for Egyptian bust context

Import `{OBJ_PATH.name}` into Blender, Unity, Unreal, Three.js, or any OBJ-capable tool. Keep the MTL file beside the OBJ for colors.
""",
        encoding="utf-8",
    )


def main():
    mesh = build_model()
    write_mtl()
    mesh.write_obj(OBJ_PATH, MTL_PATH.name)
    write_readme()
    print(f"Wrote {OBJ_PATH}")
    print(f"Wrote {MTL_PATH}")
    print(f"Vertices: {len(mesh.vertices)}")
    print(f"Faces: {len(mesh.faces)}")


if __name__ == "__main__":
    main()
