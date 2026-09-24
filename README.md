# Blueprint Arrange

One-click Sugiyama-style auto-layout for Unreal Engine Blueprint graphs.

Adds **Arrange Selection** and **Arrange Graph** actions to the node context menu of Blueprint and Material graphs (right-click on a node, under *Organization*). They run a layered layout algorithm — cycle breaking, longest-path ranking, barycenter crossing reduction, and per-column coordinate assignment — and reposition the selected nodes or every node in the graph.

### Notice

Keep in mind this repos is mainly vibe coded cause I wanted the functionality but didn't find a good free solution for this

## Features

- **Sugiyama-style layered layout**: nodes are ranked into columns by longest-path, then ordered within each column by pin-index-weighted barycenter to reduce edge crossings.
- **Per-column tight packing**: each column is packed to its own node heights instead of a single global track height, keeping the graph compact without large vertical gaps.
- **Exec-edge awareness**: exec pins are weighted more heavily than data pins so execution flow drives the vertical ordering.
- **Loop-safe**: loop-back wires are detected and ignored for layout, so loops don't stretch the graph.
- **Reroute-aware**: reroute (knot) nodes don't get a column of their own; selected knots are placed right after the node that feeds them.
- **Straight wires**: nodes are aligned pin-to-pin, using the real pin positions when the graph is open.
- **Selection or whole graph**: *Arrange Selection* (needs at least 2 selected nodes) or *Arrange Graph*.
- **Undoable**: one "Arrange Nodes" transaction, Ctrl+Z restores the previous layout.
- **Blueprints and Materials**: Blueprint event graphs, functions, macros, AnimGraphs, Materials and Material Functions.
- **Grid-snapped**: all output positions are snapped to the 16-unit grid and centred on the original bounding box.

## Known limitations

- Comment boxes are resized to frame the same nodes as before. If those nodes end up far apart, the refitted box can also cover nodes that weren't in it.
- Arranging a selection can overlap surrounding, unselected nodes.
- Pin positions are only exact for nodes that have been drawn; nodes that were never on screen use estimated sizes, so results can differ slightly after scrolling around.

## Installation

1. Clone or download this repository into your project's `Plugins/` folder:
   ```
   <YourProject>/Plugins/BlueprintArrange/
   ```
2. Regenerate project files and rebuild the project (or launch the editor and let Live Coding compile the plugin).
3. Enable the plugin in **Edit → Plugins** if it is not already enabled (it is enabled by default when placed in the project's `Plugins/` folder).

## Usage

1. Open a Blueprint or Material graph.
2. Right-click a node and choose **Arrange Graph**, or select the nodes you want, right-click one of them and choose **Arrange Selection**.

## Layout tuning

Spacing is defined in `Source/BlueprintArrange/Private/GraphArranger.h`:

```cpp
struct FBlueprintArrangeLayoutSettings
{
    int32 ColumnSpacing = 80;   // gap between a column's widest node and the next column
    int32 RowSpacing = 90;      // vertical gap between nodes within a column
    // ... plus fallback sizes used when a node's widget hasn't been drawn yet
};
```

Adjust these values and rebuild to change the spacing.

## Supported engine versions

Unreal Engine 5.8.

## License

MIT. See [LICENSE](LICENSE).
