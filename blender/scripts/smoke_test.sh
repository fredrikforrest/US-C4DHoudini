#!/usr/bin/env bash
# Headless smoke test for a fork build. No display needed.
#
#   scripts/smoke_test.sh [build-dir]      (default: $HOME/blender-fork/build)
set -euo pipefail
BUILD="${1:-$HOME/blender-fork/build}"
DEPS="${DEPS:-$(dirname "$BUILD")/deps}"   # layout: <workspace>/{blender,build,deps}
export LD_LIBRARY_PATH="$DEPS/ocio/lib:$DEPS/oiio/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
B="$BUILD/bin/blender"

echo "== version"
"$B" --version

echo "== python, build options, fork operator, render engines"
"$B" -b --factory-startup --python-exit-code 1 --python-expr "
import bpy, sys
print('python', sys.version.split()[0])
opts = bpy.app.build_options
print('build options:', ', '.join(k for k in dir(opts) if not k.startswith('_') and getattr(opts, k) is True))
engines = [getattr(e, 'bl_idname', e.__name__) for e in bpy.types.RenderEngine.__subclasses__()]
print('render engines:', engines or 'EEVEE/Workbench (built-in)')
assert hasattr(bpy.ops.object, 'fork_hello'), 'fork operator missing'
bpy.ops.mesh.primitive_cube_add()
r = bpy.ops.object.fork_hello()
assert r == {'FINISHED'}, r
print('fork_hello:', r)
"

echo "== geometry nodes + save/load round trip"
"$B" -b --factory-startup --python-exit-code 1 --python-expr "
import bpy, tempfile, os
bpy.ops.mesh.primitive_cube_add()
ob = bpy.context.active_object
mod = ob.modifiers.new('GN', 'NODES')
tree = bpy.data.node_groups.new('Fork Tree', 'GeometryNodeTree')
tree.interface.new_socket('Geometry', in_out='INPUT', socket_type='NodeSocketGeometry')
tree.interface.new_socket('Geometry', in_out='OUTPUT', socket_type='NodeSocketGeometry')
n_in = tree.nodes.new('NodeGroupInput'); n_out = tree.nodes.new('NodeGroupOutput')
# Triangulate, not Subdivide: subdivision is a no-op in builds without OpenSubdiv.
tri = tree.nodes.new('GeometryNodeTriangulate')
tree.links.new(n_in.outputs[0], tri.inputs['Mesh']); tree.links.new(tri.outputs['Mesh'], n_out.inputs[0])
mod.node_group = tree
dg = bpy.context.evaluated_depsgraph_get()
me = ob.evaluated_get(dg).data
print('triangulated cube faces:', len(me.polygons))
assert len(me.polygons) == 12, len(me.polygons)
path = os.path.join(tempfile.mkdtemp(), 'fork_smoke.blend')
bpy.ops.wm.save_as_mainfile(filepath=path)
bpy.ops.wm.open_mainfile(filepath=path)
assert 'Fork Tree' in bpy.data.node_groups
print('round trip ok:', path)
"
echo "== OK"
