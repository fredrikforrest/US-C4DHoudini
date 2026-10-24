"""
Builds the HDA test set used to validate the Cinema 4D bridge.

Run with hython:   hython tests/houdini/make_test_hdas.py [output_dir]

Each asset targets one area of the plugin:
  parm_zoo.hda         every parameter type the description builder handles
  multi_object.hda     OBJ subnet with three transformed geometry objects
  packed_scatter.hda   packed primitive instancer (copy to points, pack)
  curves_out.hda       polyline + NURBS curve output
  deformer.hda         time dependent, takes input 0, Python callback edits a menu
  named_parts.hda      name / c4d_material / prim group / uv / Cd / N conventions
  facing_check.hda     one triangle facing +Y at a known position (winding/axis test)
"""
import os
import sys

import hou

OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "hda")
os.makedirs(OUT, exist_ok=True)
OBJ = hou.node("/obj")


def make_sop_asset(name, label, build, parms=None, min_inputs=0, max_inputs=0, after=None):
    geo = OBJ.createNode("geo", name + "_src")
    sub = geo.createNode("subnet", name)
    build(sub)
    path = os.path.join(OUT, name + ".hda")
    if os.path.exists(path):
        os.remove(path)
    asset = sub.createDigitalAsset(name="us::" + name + "::1.0", hda_file_name=path, description=label,
                                   min_num_inputs=min_inputs, max_num_inputs=max_inputs)
    definition = asset.type().definition()
    if parms:
        ptg = definition.parmTemplateGroup()
        for p in parms:
            ptg.append(p)
        definition.setParmTemplateGroup(ptg)
    if after:
        after(asset, definition)
    definition.save(path, template_node=asset)
    asset.matchCurrentDefinition()
    geo.destroy()
    print("wrote", path)


# ---------------------------------------------------------------- parm_zoo
def build_parm_zoo(sub):
    box = sub.createNode("box")
    box.parm("sizex").setExpression('ch("../size")*ch("../scale_x")')
    box.parm("sizey").setExpression('ch("../size")*ch("../scale_y")')
    box.parm("sizez").setExpression('ch("../size")*ch("../scale_z")')
    box.parm("divrate1").setExpression('ch("../divisions")')
    box.parm("divrate2").setExpression('ch("../divisions")')
    box.parm("divrate3").setExpression('ch("../divisions")')
    color = sub.createNode("color")
    color.setInput(0, box)
    color.parm("colorr").setExpression('ch("../tint_r")')
    color.parm("colorg").setExpression('ch("../tint_g")')
    color.parm("colorb").setExpression('ch("../tint_b")')
    subdiv = sub.createNode("subdivide")
    subdiv.setInput(0, color)
    subdiv.parm("iterations").setExpression('ch("../smooth")')
    out = sub.createNode("output")
    out.setInput(0, subdiv)
    out.setDisplayFlag(True)
    out.setRenderFlag(True)


parm_zoo_parms = [
    hou.FolderParmTemplate("shape_folder", "Shape", folder_type=hou.folderType.Tabs, parm_templates=[
        hou.FloatParmTemplate("size", "Size", 1, default_value=(1.0,), min=0.01, max=10.0),
        hou.FloatParmTemplate("scale", "Scale", 3, default_value=(1.0, 1.0, 1.0), min=0.0, max=5.0),
        hou.IntParmTemplate("divisions", "Divisions", 1, default_value=(3,), min=1, max=20),
        hou.ToggleParmTemplate("smooth", "Smooth", default_value=False),
        hou.MenuParmTemplate("mode", "Mode", ("box", "sphere", "torus"), ("Box", "Sphere", "Torus")),
        hou.StringParmTemplate("quality", "Quality", 1, default_value=("medium",),
                               menu_items=("low", "medium", "high"), menu_labels=("Low", "Medium", "High")),
    ]),
    hou.FolderParmTemplate("look_folder", "Look", folder_type=hou.folderType.Tabs, parm_templates=[
        hou.FloatParmTemplate("tint", "Tint", 3, default_value=(0.8, 0.3, 0.1), look=hou.parmLook.ColorSquare,
                              naming_scheme=hou.parmNamingScheme.RGBA),
        hou.StringParmTemplate("label", "Label", 1, default_value=("hello",)),
        hou.StringParmTemplate("texture", "Texture", 1, string_type=hou.stringParmType.FileReference),
        hou.LabelParmTemplate("note", "Note", column_labels=("A static label",)),
        hou.SeparatorParmTemplate("sep1"),
        hou.ButtonParmTemplate("reset", "Reset Tint", script_callback='hou.pwd().parmTuple("tint").set((0.8,0.3,0.1))',
                               script_callback_language=hou.scriptLanguage.Python),
    ]),
    hou.FolderParmTemplate("adv_folder", "Advanced", folder_type=hou.folderType.Collapsible, parm_templates=[
        hou.FolderParmTemplate("points", "Points", folder_type=hou.folderType.MultiparmBlock, parm_templates=[
            hou.FloatParmTemplate("pt#", "Point #", 3),
            hou.ToggleParmTemplate("on#", "Enabled #", default_value=True),
        ]),
        hou.RampParmTemplate("falloff", "Falloff", hou.rampParmType.Float),
        hou.RampParmTemplate("gradient", "Gradient", hou.rampParmType.Color),
        hou.FloatParmTemplate("hidden_when_smooth", "Hidden When Smooth", 1,
                              conditionals={hou.parmCondType.HideWhen: "{ smooth == 1 }"}),
        hou.FloatParmTemplate("disabled_when_smooth", "Disabled When Smooth", 1,
                              conditionals={hou.parmCondType.DisableWhen: "{ smooth == 1 }"}),
        hou.StringParmTemplate("extra_geo", "Extra Geometry", 1, string_type=hou.stringParmType.NodeReference),
    ]),
]

# ------------------------------------------------------------ multi_object
def make_multi_object():
    sub = OBJ.createNode("subnet", "multi_object")
    specs = [("left", (-2, 0, 0), "box"), ("middle", (0, 1, 0), "sphere"), ("right", (2, 0, -1), "tube")]
    for name, pos, prim in specs:
        g = sub.createNode("geo", name)
        g.parmTuple("t").set(pos)
        g.parm("ry").set(30)
        n = g.createNode(prim)
        n.setDisplayFlag(True)
    path = os.path.join(OUT, "multi_object.hda")
    if os.path.exists(path):
        os.remove(path)
    asset = sub.createDigitalAsset(name="us::multi_object::1.0", hda_file_name=path, description="Multi Object")
    ptg = asset.type().definition().parmTemplateGroup()
    ptg.append(hou.FloatParmTemplate("spread", "Spread", 1, default_value=(1.0,), min=0, max=3))
    asset.type().definition().setParmTemplateGroup(ptg)
    for name, pos, prim in specs:
        g = asset.node(name)
        g.parm("tx").setExpression('%g*ch("../spread")' % pos[0])
    asset.type().definition().save(path, template_node=asset)
    asset.destroy()
    print("wrote", path)


# ----------------------------------------------------------- packed_scatter
def build_packed_scatter(sub):
    grid = sub.createNode("grid")
    grid.parmTuple("size").set((4, 4))
    scatter = sub.createNode("scatter")
    scatter.setInput(0, grid)
    scatter.parm("npts").setExpression('ch("../count")')
    attr = sub.createNode("attribwrangle", "orient_scale")
    attr.setInput(0, scatter)
    attr.parm("snippet").set('@pscale = fit01(rand(@ptnum), 0.5, 1.5);\n@orient = quaternion(rand(@ptnum)*6.28, {0,1,0});')
    box = sub.createNode("box")
    box.parmTuple("size").set((0.3, 0.3, 0.3))
    copy = sub.createNode("copytopoints::2.0")
    copy.setInput(0, box)
    copy.setInput(1, attr)
    copy.parm("pack").set(1)
    out = sub.createNode("output")
    out.setInput(0, copy)
    out.setDisplayFlag(True)
    out.setRenderFlag(True)


# --------------------------------------------------------------- curves_out
def build_curves_out(sub):
    circle = sub.createNode("circle")
    circle.parm("type").set(1)   # polygon
    circle.parm("divs").set(24)
    nurbs = sub.createNode("circle", "nurbs_circle")
    nurbs.parm("type").set(2)    # NURBS
    nurbs.parmTuple("t").set((2, 0, 0))
    line = sub.createNode("line")
    line.parm("points").set(6)
    merge = sub.createNode("merge")
    for i, n in enumerate((circle, nurbs, line)):
        merge.setInput(i, n)
    out = sub.createNode("output")
    out.setInput(0, merge)
    out.setDisplayFlag(True)
    out.setRenderFlag(True)


# ----------------------------------------------------------------- deformer
def build_deformer(sub):
    inp = sub.indirectInputs()[0]
    fallback = sub.createNode("sphere", "fallback")
    fallback.parm("type").set(1)
    switch = sub.createNode("switch")
    switch.setInput(0, fallback)
    switch.setInput(1, inp)
    switch.parm("input").setExpression('opninputs("..") > 0')
    noise = sub.createNode("mountain::2.0")
    noise.setInput(0, switch)
    noise.parm("height").setExpression('ch("../amount")')
    noise.parm("offsetx").setExpression('$T*ch("../speed")')
    out = sub.createNode("output")
    out.setInput(0, noise)
    out.setDisplayFlag(True)
    out.setRenderFlag(True)


deformer_parms = [
    hou.FloatParmTemplate("amount", "Amount", 1, default_value=(0.3,), min=0, max=2),
    hou.FloatParmTemplate("speed", "Speed", 1, default_value=(1.0,), min=0, max=5),
    hou.MenuParmTemplate("preset", "Preset", ("a",), ("A",),
                         script_callback='n=hou.pwd(); n.parm("amount").set({"a":0.3,"b":1.0}[n.parm("preset").evalAsString()])',
                         script_callback_language=hou.scriptLanguage.Python),
]


def deformer_after(asset, definition):
    # Menu whose items come from a script: the plugin must cope with choice
    # lists that change between cooks.
    ptg = definition.parmTemplateGroup()
    t = ptg.find("preset")
    t.setMenuItems(("a", "b"))
    t.setMenuLabels(("A", "B"))
    ptg.replace("preset", t)
    definition.setParmTemplateGroup(ptg)


# -------------------------------------------------------------- named_parts
def build_named_parts(sub):
    pieces = []
    for i, (name, prim, mat) in enumerate((("floor", "grid", "concrete"), ("pillar", "tube", "steel"), ("roof", "box", "wood"))):
        n = sub.createNode(prim, name)
        n.parmTuple("t").set((i * 2.0, 0, 0))
        w = sub.createNode("attribwrangle", name + "_attrs")
        w.setInput(0, n)
        w.parm("class").set(1)  # primitive
        w.parm("snippet").set('s@name = "%s";\ns@c4d_material = "%s";' % (name, mat))
        grp = sub.createNode("groupcreate", name + "_grp")
        grp.setInput(0, w)
        grp.parm("groupname").set("grp_" + name)
        pieces.append(grp)
    merge = sub.createNode("merge")
    for i, p in enumerate(pieces):
        merge.setInput(i, p)
    uv = sub.createNode("uvunwrap")
    uv.setInput(0, merge)
    nrm = sub.createNode("normal")
    nrm.setInput(0, uv)
    nrm.parm("type").set(1)   # vertex normals
    cd = sub.createNode("color")
    cd.setInput(0, nrm)
    cd.parm("colortype").set(3)   # random
    out = sub.createNode("output")
    out.setInput(0, cd)
    out.setDisplayFlag(True)
    out.setRenderFlag(True)


# ------------------------------------------------------------- facing_check
def build_facing_check(sub):
    # One triangle a=(0,0,0) b=(1,0,0) c=(0,0,1). With Houdini's clockwise
    # rule it faces +Y. Expected in Cinema 4D after the default conversion:
    # points (0,0,0) (100,0,0) (0,0,-100), normal +Y, plus a marker box at
    # Houdini (0,0,2) which must land at Cinema (0,0,-200).
    add = sub.createNode("add")
    add.parm("points").set(3)
    add.parmTuple("pt0").set((0, 0, 0))
    add.parmTuple("pt1").set((1, 0, 0))
    add.parmTuple("pt2").set((0, 0, 1))
    add.parm("prims").set(1)
    add.parm("prim0").set("0 1 2")
    marker = sub.createNode("box", "marker")
    marker.parmTuple("size").set((0.1, 0.1, 0.1))
    marker.parmTuple("t").set((0, 0, 2))
    merge = sub.createNode("merge")
    merge.setInput(0, add)
    merge.setInput(1, marker)
    out = sub.createNode("output")
    out.setInput(0, merge)
    out.setDisplayFlag(True)
    out.setRenderFlag(True)


if __name__ == "__main__":
    make_sop_asset("parm_zoo", "Parm Zoo", build_parm_zoo, parm_zoo_parms)
    make_multi_object()
    make_sop_asset("packed_scatter", "Packed Scatter", build_packed_scatter,
                   [hou.IntParmTemplate("count", "Count", 1, default_value=(200,), min=1, max=5000)])
    make_sop_asset("curves_out", "Curves Out", build_curves_out)
    make_sop_asset("deformer", "Deformer", build_deformer, deformer_parms, min_inputs=0, max_inputs=1, after=deformer_after)
    make_sop_asset("named_parts", "Named Parts", build_named_parts)
    make_sop_asset("facing_check", "Facing Check", build_facing_check)
    print("done:", OUT)
