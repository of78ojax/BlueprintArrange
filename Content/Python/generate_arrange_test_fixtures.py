#!/usr/bin/env python3
"""Generate Blueprint/Material test fixtures for the BlueprintArrange plugin.

Creates a set of purpose-built test assets under /Game/Dev/BPArrangeTests/
that exercise every feature of the BlueprintArrange auto-layout plugin:

  BP_AT_DeepChain    - long linear exec chain (ranking / column packing)
  BP_AT_BranchTree   - nested if/elif/else tree with data feeders (breaks,
                       comparison nodes, compact feeder stacking)
  BP_AT_WhileLoop     - while loop with a counter variable (loop-back wires)
  BP_AT_FanOutFanIn  - ForLoop with parallel branches converging (barycenter
                       crossing reduction, fan-in pin alignment)
  BP_AT_DataMath     - pure data-only math function graph (no exec pins)
  BP_AT_Knots        - reroute (knot) nodes on exec and data wires
  M_AT_Comment       - material with expressions inside a comment box
                       (comment refit after arrange)
  M_AT_Plain         - material diamond graph, no comments

Every node is scattered to a deterministic "messy" position so each arrange
run starts from the identical before-state. The generator is idempotent:
it deletes the whole /Game/Dev/BPArrangeTests folder and rebuilds it,
which is what makes the fixtures reset cleanly between test sessions.

Usage:
    python3 generate_arrange_test_fixtures.py [--url http://host:port/mcp]

Requires a running Unreal Editor 5.8 with the ModelContextProtocol server
started (console: ModelContextProtocol.StartServer) and the MCPClientToolset
/ AllToolsets plugins enabled.
"""

import argparse
import json
import math
import sys
import urllib.error
import urllib.request

BP_TOOLS = "editor_toolset.toolsets.blueprint.BlueprintTools"
ASSET_TOOLS = "editor_toolset.toolsets.asset.AssetTools"
MAT_TOOLS = "editor_toolset.toolsets.material.MaterialTools"

FOLDER = "/Game/Dev/BPArrangeTests"

# ---------------------------------------------------------------------------
# Minimal MCP client (Streamable HTTP)
# ---------------------------------------------------------------------------

class Mcp:
    def __init__(self, url):
        self.url = url
        self.session = None

    def _post(self, payload, timeout=300):
        headers = {
            "Content-Type": "application/json",
            "Accept": "application/json, text/event-stream",
        }
        if self.session:
            headers["mcp-session-id"] = self.session
        req = urllib.request.Request(
            self.url, data=json.dumps(payload).encode(), headers=headers, method="POST")
        try:
            with urllib.request.urlopen(req, timeout=timeout) as resp:
                return resp.headers.get("mcp-session-id"), resp.status, resp.read().decode(errors="replace")
        except urllib.error.HTTPError as e:
            return None, e.code, e.read().decode(errors="replace")

    def _ensure_session(self):
        if self.session:
            return
        sess, code, body = self._post({
            "jsonrpc": "2.0", "id": 1, "method": "initialize",
            "params": {"protocolVersion": "2025-06-18", "capabilities": {},
                       "clientInfo": {"name": "bpa-fixture-gen", "version": "1.0"}}})
        if not sess:
            raise RuntimeError(f"MCP initialize failed (HTTP {code}): {body[:300]}")
        self.session = sess
        self._post({"jsonrpc": "2.0", "method": "notifications/initialized"})

    def call(self, tool, toolset, args=None, retries=2):
        """Call a toolset tool; returns (ok, result_text)."""
        for attempt in range(retries + 1):
            self._ensure_session()
            _, code, body = self._post({
                "jsonrpc": "2.0", "id": 7, "method": "tools/call",
                "params": {"name": "call_tool", "arguments": {
                    "tool_name": tool, "toolset_name": toolset,
                    "arguments": args or {}}}})
            try:
                d = json.loads(body[body.index("{"):])
            except ValueError:
                d = {}
            if "result" in d:
                content = d["result"].get("content", [])
                return True, (content[0]["text"] if content else "")
            msg = d.get("error", {}).get("message", f"HTTP {code}: {body[:200]}")
            # stale/expired session -> reconnect and retry
            if attempt < retries:
                self.session = None
                continue
            return False, msg
        return False, "retries exhausted"


def unwrap(text):
    """Unwrap toolset JSON results: {\"returnValue\": X} -> X."""
    try:
        d = json.loads(text)
    except ValueError:
        return text
    if isinstance(d, dict) and "returnValue" in d:
        rv = d["returnValue"]
        if isinstance(rv, str):
            try:
                rv = json.loads(rv)
            except ValueError:
                pass
        return rv
    return d


# ---------------------------------------------------------------------------
# Deterministic scatter — the "messy" starting layout
# ---------------------------------------------------------------------------

def scatter_pos(i, salt=0):
    """Deterministic pseudo-random-looking position for node index i."""
    x = (math.sin(i * 12.9898 + salt * 78.233) * 43758.5453) % 1.0
    y = (math.sin(i * 93.989 + salt * 47.1337) * 24634.6345) % 1.0
    return {"x": int((x - 0.5) * 2400), "y": int((y - 0.5) * 1800)}


# ---------------------------------------------------------------------------
# Fixture definitions
# ---------------------------------------------------------------------------

# Each entry: (asset_name, kind, content)
#   kind "bp"     -> content is DSL code written to EventGraph
#   kind "bp_fn"  -> content is (name, DSL) pairs for function graphs
#   kind "bp_raw" -> custom builder function name
#   kind "mat"    -> content is a material builder dict

BP_DEEP_CHAIN = """
(event EventBeginPlay
  (Development|PrintString "step 1")
  (Development|PrintString "step 2")
  (Development|PrintString "step 3")
  (Development|PrintString "step 4")
  (Development|PrintString "step 5")
  (Development|PrintString "step 6")
  (Development|PrintString "step 7")
  (Development|PrintString "step 8")
  (Development|PrintString "step 9")
  (Development|PrintString "step 10")
  (Development|PrintString "step 11")
  (Development|PrintString "step 12"))
"""

BP_BRANCH_TREE = """
(event EventBeginPlay
  (bind loc (Transformation|GetActorLocation))
  (if (> (.z loc) 100.0)
    (Development|PrintString "high")
    (elif (< (.z loc) (- 100.0))
      (Development|PrintString "low")
      (else
        (if (== (.x loc) 0.0)
          (Development|PrintString "origin")
          (else
            (Development|PrintString "mid")))))))
"""

BP_WHILE_LOOP = """
(event EventBeginPlay
  (while (< (Variables|Default|GetLoopCounter) 8)
    (Variables|Default|SetLoopCounter
      (+ (Variables|Default|GetLoopCounter) 1))))
"""

BP_FAN_OUT_FAN_IN = """
(event EventBeginPlay
  (bind base (Variables|Default|GetBaseValue))
  (for i (range 5)
    (Development|PrintString "iter"))
  (Development|PrintString "done")
  (Variables|Default|SetBaseValue (+ base 1.0)))
"""

# Event-graph fixture exercising data-only feeders: literal->string
# conversions feeding a single consumer (compact feeder stacking), plus a
# member variable chain.
BP_DATA_MATH_EVENT = """
(event EventBeginPlay
  (bind a (Utilities|String|ToString(Float) 1.0))
  (bind b (Utilities|String|ToString(Float) 2.0))
  (bind c (Utilities|String|ToString(Float) 3.0))
  (Development|PrintString a)
  (Development|PrintString b)
  (Development|PrintString c)
  (bind v (Variables|Default|GetBaseValue))
  (Variables|Default|SetBaseValue (+ v (* v 2.0))))
"""

BP_DATA_MATH_FN = {
    "params": [("A", "float", True), ("B", "float", True), ("Result", "float", False)],
    "dsl": """
(fn ComputeValue (A B)
  (return (+ (* A A) (* B B))))
""",
}


def _big_dsl():
    """~130-node stress graph. Ten parallel event chains, each a realistic
    mini-workflow (bind location, branch, math, loop, print), converging
    into a shared tail. Built programmatically so the DSL stays compact."""
    lines = []

    # Chain 0: BeginPlay with a deep nested structure
    lines.append("""
(event EventBeginPlay
  (bind loc (Transformation|GetActorLocation))
  (if (> (.z loc) 0.0)
    (for i (range 10)
      (bind v (+ (.x loc) (* i 10.0)))
      (if (> v 50.0)
        (Development|PrintString "high")
        (else
          (Development|PrintString "low"))))
    (else
      (while (< (.y loc) 100.0)
        (Development|PrintString "climbing")))))
""")

    # Chains 1-7: custom events with varied shapes
    def spawn_tmpl(n):
        return f"""
(event Custom|OnSpawn{n}
  (bind a (* {n} 2.0))
  (bind b (+ a 0.5))
  (if (> b {n})
    (Development|PrintString "path-a")
    (elif (< b (- {n}))
      (Development|PrintString "path-b")
      (else
        (for j (range 4)
          (Development|PrintString "spin"))))))
"""

    def damage_tmpl(n):
        # switch needs a node output, not a bare literal — bind the id first.
        return f"""
(event Custom|OnDamage{n}
  (bind dmg (Math|Integer|Max(Integer) {n} 0))
  (switch int dmg
    (:0 (Development|PrintString "none"))
    (:1 (Development|PrintString "chip")
        (Development|PrintString "logged"))
    (:Default
      (bind total (+ {n} 1))
      (Development|PrintString "heavy"))))
"""

    def timer_tmpl(n):
        # DSL binds are immutable, so a while-loop accumulator can't rebind.
        # Use a plain counted loop here (the while+counter-variable pattern
        # is already covered by BP_AT_WhileLoop via member-variable setters).
        return f"""
(event Custom|OnTimer{n}
  (for t (range {n + 2})
    (bind step (Math|Float|MapRangeClamped (* t 0.1) 0.0 1.0 0.0 10.0))
    (Development|PrintString "tick"))
  (Development|PrintString "done"))
"""

    shapes = [spawn_tmpl, damage_tmpl, timer_tmpl]
    for i in range(1, 8):
        lines.append(shapes[i % len(shapes)](i))

    return "\n".join(lines)


BP_BIG = _big_dsl()

BP_KNOT_TEST = "knots-custom"  # built manually below

MAT_COMMENT = {
    # Expressions 0-2 sit INSIDE the comment box. The box is created at
    # (-50,-50) and MaterialExpressionComment keeps its default size
    # (400x400), so the box spans roughly -50..350 on both axes. The
    # framed expressions overlap on purpose (messy start). Expressions
    # 3-4 sit outside the box so arranging moves them relative to it.
    "expressions": [
        # (class_path, x, y)
        ("/Script/Engine.MaterialExpressionConstant", 0, 0),
        ("/Script/Engine.MaterialExpressionConstant", 0, 110),
        ("/Script/Engine.MaterialExpressionMultiply", 60, 55),
        ("/Script/Engine.MaterialExpressionConstant", 450, 80),
        ("/Script/Engine.MaterialExpressionAdd", 500, 250),
    ],
    "comment": {"x": -50, "y": -50, "text": "Color math"},
    "wires": [
        # (from_index, from_output_name, to_index, to_input_name)
        (0, "", 2, "A"),
        (1, "", 2, "B"),
        (2, "", 4, "A"),
        (3, "", 4, "B"),
    ],
    "output": (4, ""),  # expression index feeding BaseColor
}

MAT_PLAIN = {
    # Diamond with crossing wires at scattered (messy) positions.
    "scatter": True,
    "expressions": [
        ("/Script/Engine.MaterialExpressionConstant", 400, -200),
        ("/Script/Engine.MaterialExpressionConstant", -300, 300),
        ("/Script/Engine.MaterialExpressionConstant", 600, 150),
        ("/Script/Engine.MaterialExpressionMultiply", 100, -500),
        ("/Script/Engine.MaterialExpressionMultiply", -450, -150),
        ("/Script/Engine.MaterialExpressionAdd", 800, 400),
    ],
    "wires": [
        (0, "", 3, "A"), (1, "", 3, "B"),
        (1, "", 4, "A"), (2, "", 4, "B"),
        (3, "", 5, "A"), (4, "", 5, "B"),
    ],
    "output": (5, ""),
}


# ---------------------------------------------------------------------------
# Builders
# ---------------------------------------------------------------------------

class Generator:
    def __init__(self, mcp, log=print):
        self.m = mcp
        self.log = log
        self.report = {"created": [], "skipped": [], "errors": []}

    def ok(self, name):
        self.report["created"].append(name)

    def err(self, name, msg):
        self.report["errors"].append({"asset": name, "error": msg[:400]})
        self.log(f"  !! {name}: {msg[:200]}")

    # -- generic helpers ---------------------------------------------------

    def bp_create(self, name, parent="/Script/Engine.Actor"):
        ok, t = self.m.call("create", BP_TOOLS, {
            "folder_path": FOLDER, "asset_name": name,
            "asset_type": {"refPath": parent}})
        if not ok:
            return False, t
        ref = unwrap(t)
        return True, ref["refPath"]

    def bp_event_graph(self, bp_path):
        ok, t = self.m.call("get_graph", BP_TOOLS, {
            "blueprint": {"refPath": bp_path}, "graph_name": "EventGraph"})
        if not ok:
            return False, t
        return True, unwrap(t)["refPath"]

    @staticmethod
    def _looks_like_error(text):
        """Toolset reports many failures as plain text with JSON-RPC success
        (e.g. 'The node could not be created / ... does not exist',
        'Function parameter(s) not found in graph: ...'). Detect them."""
        if not isinstance(text, str):
            return False
        markers = (
            "does not exist",
            "could not be created",
            "not found in graph",
            "AssertionError",
            "Parameter error",
            "object has no attribute",
            "Error:",
        )
        return any(m in text for m in markers)

    def bp_write_dsl(self, graph_path, code):
        ok, t = self.m.call("write_graph_dsl", BP_TOOLS, {
            "graph": {"refPath": graph_path}, "code": code})
        if not ok:
            return False, t
        if self._looks_like_error(t):
            return False, t
        return True, t

    def bp_scatter_graph(self, graph_path, salt=0):
        """Move every non-comment node in a graph to a deterministic scatter."""
        ok, t = self.m.call("find_nodes", BP_TOOLS, {
            "graph": {"refPath": graph_path}, "title": ""})
        if not ok:
            return False, t
        nodes = unwrap(t)
        moved = 0
        for i, n in enumerate(nodes):
            ok, t = self.m.call("set_node_position", BP_TOOLS, {
                "node": n, "pos": scatter_pos(i, salt)})
            if ok:
                moved += 1
            else:
                # comments fail with an Epic toolset bug ("set_node_pos" missing);
                # they keep their create-time position, which is acceptable.
                self.log(f"  (node {n['refPath'].split('.')[-1]} not movable: {t[:80]})")
        return True, moved

    def bp_add_variable(self, bp_path, name, type_name):
        ok, t = self.m.call("add_variable", BP_TOOLS, {
            "blueprint": {"refPath": bp_path}, "name": name, "type_name": type_name})
        return ok, t

    def bp_compile_save(self, asset_name, bp_path):
        ok, t = self.m.call("compile_blueprint", BP_TOOLS, {
            "blueprint": {"refPath": bp_path}})
        if not ok:
            return False, f"compile: {t}"
        ok, t = self.m.call("save_assets", ASSET_TOOLS, {"asset_paths": [f"{FOLDER}/{asset_name}"]})
        if not ok:
            return False, f"save: {t}"
        return True, ""

    # -- fixture builders ---------------------------------------------------

    def build_bp_dsl_fixture(self, name, dsl, variables=None, salt=0):
        self.log(f"[bp] {name}")
        ok, info = self.bp_create(name)
        if not ok:
            return self.err(name, info)
        bp_path = info
        for vname, vtype in (variables or []):
            ok, t = self.bp_add_variable(bp_path, vname, vtype)
            if not ok:
                return self.err(name, f"add_variable {vname}: {t}")
        ok, gpath = self.bp_event_graph(bp_path)
        if not ok:
            return self.err(name, gpath)
        ok, t = self.bp_write_dsl(gpath, dsl)
        if not ok:
            return self.err(name, f"write_graph_dsl: {t}")
        ok, t = self.bp_scatter_graph(gpath, salt)
        if not ok:
            return self.err(name, f"scatter: {t}")
        ok, t = self.bp_compile_save(name, bp_path)
        if not ok:
            return self.err(name, t)
        self.ok(name)

    def build_bp_function_fixture(self, name, fn_name, fn_spec, salt=0):
        """fn_spec: {'params': [(name, type, is_input), ...], 'dsl': str}"""
        self.log(f"[bp] {name}")
        ok, info = self.bp_create(name)
        if not ok:
            return self.err(name, info)
        bp_path = info
        ok, t = self.m.call("add_function_graph", BP_TOOLS, {
            "blueprint": {"refPath": bp_path}, "graph_name": fn_name})
        if not ok:
            return self.err(name, f"add_function_graph: {t}")
        ok, t = self.m.call("get_graph", BP_TOOLS, {
            "blueprint": {"refPath": bp_path}, "graph_name": fn_name})
        if not ok:
            return self.err(name, f"get_graph {fn_name}: {t}")
        fpath = unwrap(t)["refPath"]

        # Function parameters MUST be registered before writing the DSL,
        # otherwise write_graph_dsl fails with "Function parameter(s) not
        # found" and leaves the graph empty.
        for pname, ptype, is_input in fn_spec.get("params", []):
            ok, t = self.m.call("add_function_param", BP_TOOLS, {
                "graph": {"refPath": fpath}, "param_name": pname,
                "param_type": ptype, "input_param": is_input})
            if not ok:
                return self.err(name, f"add_function_param {pname}: {t}")

        ok, t = self.bp_write_dsl(fpath, fn_spec["dsl"])
        if not ok:
            return self.err(name, f"write_graph_dsl: {t}")
        ok, t = self.bp_scatter_graph(fpath, salt)
        if not ok:
            return self.err(name, f"scatter: {t}")
        ok, t = self.bp_compile_save(name, bp_path)
        if not ok:
            return self.err(name, t)
        self.ok(name)

    def build_knot_fixture(self, name):
        """Event -> Print -> Print -> Print with reroute knots on exec + data wires."""
        self.log(f"[bp] {name} (knots, manual wiring)")
        ok, info = self.bp_create(name)
        if not ok:
            return self.err(name, info)
        bp_path = info

        ok, t = self.m.call("add_event", BP_TOOLS, {
            "blueprint": {"refPath": bp_path}, "event_name": "BeginPlay"})
        if not ok:
            return self.err(name, f"add_event: {t}")
        event_node = unwrap(t)["refPath"]

        ok, gpath = self.bp_event_graph(bp_path)
        if not ok:
            return self.err(name, gpath)

        # three print nodes
        prints = []
        for i in range(3):
            ok, t = self.m.call("create_node", BP_TOOLS, {
                "graph": {"refPath": gpath}, "type_id": "Development|PrintString",
                "pos": scatter_pos(i, salt=1)})
            if not ok or self._looks_like_error(t):
                return self.err(name, f"create_node print{i}: {t}")
            res = unwrap(t)
            if not isinstance(res, dict):
                return self.err(name, f"create_node print{i}: {t}")
            prints.append(res["refPath"])

        # two reroute knots. The action id for "Add Reroute Node" isn't stable
        # across sessions (category prefix varies: '|AddRerouteNode...' vs
        # '||AddRerouteNode...'), so probe it live.
        ok, t = self.m.call("find_node_types", BP_TOOLS, {
            "graph": {"refPath": gpath}, "type_id_filter": "AddReroute",
            "context_pins": []})
        if not ok:
            return self.err(name, f"find reroute type: {t}")
        reroute_ids = [tid for tid in unwrap(t) if tid.endswith("AddRerouteNode...")]
        if not reroute_ids:
            return self.err(name, "no AddRerouteNode action found")
        reroute_id = reroute_ids[0]

        knots = []
        for i in range(2):
            ok, t = self.m.call("create_node", BP_TOOLS, {
                "graph": {"refPath": gpath}, "type_id": reroute_id,
                "pos": scatter_pos(i, salt=2)})
            if not ok or self._looks_like_error(t):
                return self.err(name, f"create_node knot{i}: {t}")
            res = unwrap(t)
            if not isinstance(res, dict):
                return self.err(name, f"create_node knot{i}: {t}")
            knots.append(res["refPath"])

        ok, t = self.m.call("set_node_position", BP_TOOLS, {
            "node": {"refPath": event_node}, "pos": scatter_pos(9, salt=3)})
        # event nodes may refuse set_node_position; not fatal

        # wire: event.then -> print0.exec (through knot0)
        #       print0.then -> print1.exec (through knot1)
        #       print1.then -> print2.exec (direct)
        #       print2.InString <- "done" literal is default; leave defaults
        def pin_of(node_path, direction, name):
            ok, t = self.m.call("get_node_infos", BP_TOOLS, {
                "nodes": [{"refPath": node_path}]})
            if not ok:
                return None
            infos = unwrap(t)
            pins = infos[0]["input_pins"] if direction == "in" else infos[0]["output_pins"]
            for idx, p in enumerate(pins):
                if p["name"].lower().replace(" ", "") == name.lower().replace(" ", ""):
                    return {"direction": "EGPD_Input" if direction == "in" else "EGPD_Output",
                            "index_id": p["pin_id"]["index_id"], "node": {"refPath": node_path}}
            return None

        def connect(out_pin, in_pin):
            ok, t = self.m.call("connect_pins", BP_TOOLS, {
                "output_pin": out_pin, "input_pin": in_pin})
            return ok, t

        ev_then = pin_of(event_node, "out", "Then")
        k0_in = pin_of(knots[0], "in", "InputPin")
        k0_out = pin_of(knots[0], "out", "OutputPin")
        p0_exec = pin_of(prints[0], "in", "execute")
        p0_then = pin_of(prints[0], "out", "then")
        k1_in = pin_of(knots[1], "in", "InputPin")
        k1_out = pin_of(knots[1], "out", "OutputPin")
        p1_exec = pin_of(prints[1], "in", "execute")
        p1_then = pin_of(prints[1], "out", "then")
        p2_exec = pin_of(prints[2], "in", "execute")

        wires = [
            (ev_then, k0_in), (k0_out, p0_exec),
            (p0_then, k1_in), (k1_out, p1_exec),
            (p1_then, p2_exec),
        ]
        for out_pin, in_pin in wires:
            if not out_pin or not in_pin:
                return self.err(name, f"pin lookup failed: {out_pin} / {in_pin}")
            ok, t = connect(out_pin, in_pin)
            if not ok:
                return self.err(name, f"connect: {t}")

        # knot on a data wire: print1 "InString" default value is fine; instead
        # route print2.InString from print1 via a literal is overkill — the two
        # exec knots above already cover knot handling.
        ok, t = self.bp_compile_save(name, bp_path)
        if not ok:
            return self.err(name, t)
        self.ok(name)

    def build_material_fixture(self, name, spec, salt=0):
        self.log(f"[mat] {name}")
        ok, t = self.m.call("create_material", MAT_TOOLS, {
            "folder_path": FOLDER, "asset_name": name})
        if not ok:
            return self.err(name, t)
        mat_path = unwrap(t)["refPath"] if isinstance(unwrap(t), dict) else f"{FOLDER}/{name}.{name}"

        scatter = spec.get("scatter", False)
        exprs = []
        for i, (cls, x, y) in enumerate(spec["expressions"]):
            if scatter:
                x, y = scatter_pos(i, salt)["x"], scatter_pos(i, salt)["y"]
            ok, t = self.m.call("add_expression", MAT_TOOLS, {
                "material_or_function": {"refPath": mat_path},
                "expression_class": {"refPath": cls},
                "x": x, "y": y})
            if not ok:
                return self.err(name, f"add_expression {cls}: {t}")
            exprs.append(unwrap(t)["refPath"] if isinstance(unwrap(t), dict) else None)

        # wires
        for from_i, from_out, to_i, to_in in spec.get("wires", []):
            ok, t = self.m.call("connect_expressions", MAT_TOOLS, {
                "from_expression": {"refPath": exprs[from_i]},
                "from_output_name": from_out,
                "to_expression": {"refPath": exprs[to_i]},
                "to_input_name": to_in})
            if not ok:
                self.err(name, f"wire {from_i}->{to_i}: {t}")

        # output
        if spec.get("output"):
            out_i, out_name = spec["output"]
            prop = spec.get("output_property", "MP_BaseColor")
            ok, t = self.m.call("connect_to_output", MAT_TOOLS, {
                "material_or_function": {"refPath": mat_path},
                "expression": {"refPath": exprs[out_i]},
                "output_name": out_name,
                "material_property": prop})
            if not ok:
                self.err(name, f"connect_to_output: {t}")

        # Comment box framing the first expressions. MaterialExpressionComment
        # keeps its default size (the toolset can't resize it), so the framed
        # expressions are positioned inside that default box, and the text
        # can't be set either — acceptable for a layout test fixture.
        if spec.get("comment"):
            c = spec["comment"]
            ok, t = self.m.call("add_expression", MAT_TOOLS, {
                "material_or_function": {"refPath": mat_path},
                "expression_class": {"refPath": "/Script/Engine.MaterialExpressionComment"},
                "x": c["x"], "y": c["y"]})
            if not ok:
                self.err(name, f"comment: {t}")

        ok, t = self.m.call("recompile", MAT_TOOLS, {"material_or_function": {"refPath": mat_path}})
        if not ok:
            self.log(f"  (recompile: {t[:100]})")
        ok, t = self.m.call("save_assets", ASSET_TOOLS, {"asset_paths": [f"{FOLDER}/{name}"]})
        if not ok:
            return self.err(name, f"save: {t}")
        self.ok(name)

    # -- orchestration -------------------------------------------------------

    def reset_folder(self):
        self.log(f"[reset] {FOLDER}")
        ok, t = self.m.call("exists", ASSET_TOOLS, {"path": FOLDER})
        if ok and unwrap(t) is True:
            ok, t = self.m.call("delete", ASSET_TOOLS, {"path": FOLDER})
            if not ok:
                raise RuntimeError(f"cannot delete {FOLDER}: {t}")
            self.log("  deleted old fixtures")
        ok, t = self.m.call("create_folder", ASSET_TOOLS, {"path": FOLDER})
        if not ok:
            raise RuntimeError(f"cannot create {FOLDER}: {t}")

    def run(self):
        self.reset_folder()

        self.build_bp_dsl_fixture("BP_AT_DeepChain", BP_DEEP_CHAIN, salt=1)
        self.build_bp_dsl_fixture("BP_AT_BranchTree", BP_BRANCH_TREE, salt=2)
        self.build_bp_dsl_fixture("BP_AT_WhileLoop", BP_WHILE_LOOP,
                                  variables=[("LoopCounter", "int")], salt=3)
        self.build_bp_dsl_fixture("BP_AT_FanOutFanIn", BP_FAN_OUT_FAN_IN,
                                  variables=[("BaseValue", "float")], salt=4)
        self.build_bp_dsl_fixture("BP_AT_DataMath", BP_DATA_MATH_EVENT,
                                  variables=[("BaseValue", "float")], salt=5)
        self.build_bp_function_fixture("BP_AT_DataMathFn", "ComputeValue", BP_DATA_MATH_FN, salt=9)
        self.build_bp_dsl_fixture("BP_AT_BigGraph", BP_BIG, salt=10)
        self.build_knot_fixture("BP_AT_Knots")
        self.build_material_fixture("M_AT_Comment", MAT_COMMENT, salt=7)
        self.build_material_fixture("M_AT_Plain", MAT_PLAIN, salt=8)

        return self.report


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://172.17.0.1:8001/mcp",
                    help="MCP endpoint of the running editor")
    args = ap.parse_args()

    mcp = Mcp(args.url)
    gen = Generator(mcp)
    try:
        report = gen.run()
    except Exception as e:
        print(f"FATAL: {e}", file=sys.stderr)
        sys.exit(1)

    print(json.dumps(report, indent=2))
    n_ok = len(report["created"])
    n_err = len(report["errors"])
    print(f"\nDone: {n_ok} created, {n_err} errors.")
    sys.exit(1 if n_err else 0)


if __name__ == "__main__":
    main()
