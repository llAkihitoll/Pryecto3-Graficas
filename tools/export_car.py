# Convierte el Fennec (FBX) a OBJ + MTL para el renderizador.
#
# Uso (desde la raiz del repositorio):
#   blender -b --factory-startup --python tools/export_car.py
#
# Genera:
#   assets/models/fennec.obj      modelo completo (~85k triangulos)
#   assets/models/fennec_lod.obj  version decimada para cuando esta lejos
#   assets/models/fennec.mtl      colores de los materiales (compartido)
#
# Escribo el OBJ a mano en vez de usar el exportador de Blender para controlar
# exactamente los ejes, las ruedas espejadas y las normales.

import bpy
import os
from mathutils import Vector

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
FBX = os.path.join(ROOT, "fennec-rocket-league-car", "source", "Fennec.fbx")
OUT = os.path.join(ROOT, "assets", "models")

# Que tanto se reduce cada malla en la version LOD.
LOD_RATIO = {"Fennec": 0.18}
LOD_RATIO_WHEEL = 0.2


def to_render_axes(v):
    # En Blender el carro mira a +X, +Y es su izquierda y +Z es arriba.
    # En el renderizador: +X derecha, +Y arriba, el frente mira a -Z.
    # Es una rotacion (determinante 1), asi que no cambia el sentido de las caras.
    return Vector((-v.y, v.z, -v.x))


def linear_to_srgb(c):
    # Blender guarda los colores en espacio lineal; el framebuffer es sRGB.
    if c <= 0.0031308:
        return 12.92 * c
    return 1.055 * (c ** (1.0 / 2.4)) - 0.055


def material_color(mat):
    for node in mat.node_tree.nodes:
        if node.type == "BSDF_PRINCIPLED":
            col = node.inputs["Base Color"].default_value
            return [linear_to_srgb(col[i]) for i in range(3)]
    return [0.8, 0.8, 0.8]


def collect_triangles():
    """Devuelve posiciones, normales y triangulos (con material) ya en ejes del renderizador."""
    depsgraph = bpy.context.evaluated_depsgraph_get()
    positions, normals, tris = [], [], []
    normal_index = {}

    for ob in sorted(bpy.data.objects, key=lambda o: o.name):
        if ob.type != "MESH":
            continue
        ob_eval = ob.evaluated_get(depsgraph)
        me = ob_eval.to_mesh()
        me.calc_loop_triangles()

        mw = ob.matrix_world
        normal_mat = mw.to_3x3().inverted().transposed()
        # Las ruedas derechas tienen escala negativa (espejo). Un espejo invierte
        # el orden de los vertices, asi que hay que voltear los triangulos.
        flip = mw.to_3x3().determinant() < 0

        base = len(positions)
        for v in me.vertices:
            positions.append(to_render_axes(mw @ v.co))

        corner_normals = me.corner_normals
        for lt in me.loop_triangles:
            idx = []
            for vi, li in zip(lt.vertices, lt.loops):
                n = to_render_axes((normal_mat @ corner_normals[li].vector).normalized())
                key = (round(n.x, 4), round(n.y, 4), round(n.z, 4))
                if key not in normal_index:
                    normal_index[key] = len(normals)
                    normals.append(key)
                idx.append((base + vi, normal_index[key]))
            if flip:
                idx = [idx[0], idx[2], idx[1]]
            mat = ob.material_slots[lt.material_index].material.name if ob.material_slots else "Default"
            tris.append((mat, idx))

        ob_eval.to_mesh_clear()

    return positions, normals, tris


def write_obj(path, positions, normals, tris):
    # Centra el carro en el origen para que rote alrededor de su centro.
    mn = Vector((min(p.x for p in positions), min(p.y for p in positions), min(p.z for p in positions)))
    mx = Vector((max(p.x for p in positions), max(p.y for p in positions), max(p.z for p in positions)))
    center = (mn + mx) / 2

    tris = sorted(tris, key=lambda t: t[0])  # agrupa por material
    with open(path, "w") as f:
        f.write("# Fennec convertido con tools/export_car.py\n")
        f.write("# Ejes: +X derecha, +Y arriba, frente hacia -Z. Unidades: metros.\n")
        f.write("mtllib fennec.mtl\n")
        for p in positions:
            q = p - center
            f.write(f"v {q.x:.5f} {q.y:.5f} {q.z:.5f}\n")
        for n in normals:
            f.write(f"vn {n[0]:.4f} {n[1]:.4f} {n[2]:.4f}\n")
        current = None
        for mat, idx in tris:
            if mat != current:
                f.write(f"usemtl {mat}\n")
                current = mat
            f.write("f " + " ".join(f"{v + 1}//{n + 1}" for v, n in idx) + "\n")
    print(f"{path}: {len(positions)} vertices, {len(tris)} triangulos")


def write_mtl(path):
    with open(path, "w") as f:
        for mat in bpy.data.materials:
            r, g, b = material_color(mat)
            f.write(f"newmtl {mat.name}\nKd {r:.4f} {g:.4f} {b:.4f}\n\n")


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=FBX)
    os.makedirs(OUT, exist_ok=True)

    write_mtl(os.path.join(OUT, "fennec.mtl"))
    write_obj(os.path.join(OUT, "fennec.obj"), *collect_triangles())

    for ob in bpy.data.objects:
        if ob.type == "MESH":
            mod = ob.modifiers.new("lod", "DECIMATE")
            mod.ratio = LOD_RATIO.get(ob.name, LOD_RATIO_WHEEL)
    write_obj(os.path.join(OUT, "fennec_lod.obj"), *collect_triangles())


main()
