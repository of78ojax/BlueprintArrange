# Arrange test fixtures

Stress-test assets for the **Arrange Selection / Arrange Graph** actions,
generated into the *project's* content at `/Game/Dev/BPArrangeTests/` —
they are deliberately not part of the plugin (the plugin ships clean; the
fixtures are per-project test data).

## Fixtures

| Asset | Exercises |
|---|---|
| `BP_AT_DeepChain` | 15-node linear exec chain — longest-path ranking, column packing, exec-pin weighting |
| `BP_AT_BranchTree` | 21-node nested if/elif/else tree with GetActorLocation feeders and breakouts — feeder stacking, crossing reduction |
| `BP_AT_WhileLoop` | while-loop on a counter variable — loop-back wire detection (cycle breaking) |
| `BP_AT_FanOutFanIn` | ForLoop with converging branches — barycenter ordering, fan-in pin alignment |
| `BP_AT_DataMath` | literal→string conversions feeding prints plus a member-variable math chain — compact feeder stacking |
| `BP_AT_DataMathFn` | pure-data function graph (`ComputeValue`: A²+B², no exec pins) — data-only layout, data row spacing |
| `BP_AT_BigGraph` | ~100-node stress graph: 8 event chains mixing deep nesting, for-loops, switch-on-int, branch trees and math — full-pipeline performance and quality at scale |
| `BP_AT_Knots` | exec chain wired through two reroute (knot) nodes — knot placement in the source gap |
| `M_AT_Comment` | material diamond whose first half sits inside a comment box — comment refit after arrange |
| `M_AT_Plain` | material diamond, no comments — material graph layout + `LinkMaterialExpressionsFromGraph` |

Every graph starts from a deterministic "messy" scatter (overlapping
positions computed from a fixed sine formula), so every arrange run has the
same before-state and results are comparable between sessions.

## Regenerating / resetting

The generator is idempotent: it deletes `/Game/Dev/BPArrangeTests/` and
rebuilds every asset identically (same topology, same scatter positions).
Re-run it between test sessions to reset any arrangements you made:

```
cd Content/Python
python3 generate_arrange_test_fixtures.py [--url http://127.0.0.1:8000/mcp]
```

Requirements: Unreal Editor 5.8 running with the **Unreal MCP**
(`ModelContextProtocol`) server started (console:
`ModelContextProtocol.StartServer`) and the **MCPClientToolset** /
**AllToolsets** plugins enabled. When the editor runs on the same machine,
use the default `http://127.0.0.1:8000/mcp`.

The generator drives the editor over MCP (create blueprint, write graph DSL,
create nodes/knots, build materials) — no engine-side python plugin needed.

## Known toolset limitations (affect only fixture cosmetics)

- `EdGraphNode_Comment` nodes in **Blueprint** graphs can't be created at a
  position by the toolset (Epic bug: `set_node_pos` missing on comment nodes),
  so comment-refit coverage lives in `M_AT_Comment` (material comments derive
  from the same `UEdGraphNode_Comment` base the arranger handles).
- The material comment box can't be resized/labelled via the toolset, so the
  framed expressions are placed inside its default-size box instead.

## DSL gotchas (learned the hard way, kept here for future edits)

- Toolset failures often come back as plain text inside a JSON-RPC *success*
  ("The node could not be created / ... does not exist"). The generator
  checks for these markers, so a broken form fails loudly instead of
  leaving an empty graph.
- Custom events must be declared `(event Custom|Name ...)`; plain
  `(event Name ...)` tries `AddEvent|Name` and fails.
- `(bind x 0.0)` is invalid — bind needs a node call, and bindings are
  immutable (no rebinding inside loops; use member-variable setters).
- `switch` needs a node output as its value, not a bare literal.
- Node type IDs shift between sessions (e.g. `|AddRerouteNode...` vs
  `||AddRerouteNode...`) — probe with `find_node_types` instead of
  hardcoding.
- Function graphs need their parameters registered via `add_function_param`
  *before* `write_graph_dsl` runs, otherwise the write silently fails and
  the graph stays empty.
