# Blueprint Arrange

One-click Sugiyama-style auto-layout for Unreal Engine Blueprint graphs.

Adds an **Arrange** action to the Blueprint graph context menu (right-click on a node or the graph background). It runs a layered layout algorithm — longest-path ranking, barycenter crossing reduction, and per-column coordinate assignment — and repositions the selected nodes (or all nodes, if nothing is selected).

## Features

- **Sugiyama-style layered layout**: nodes are ranked into columns by longest-path, then ordered within each column by pin-index-weighted barycenter to reduce edge crossings.
- **Per-column tight packing**: each column is packed to its own node heights instead of a single global track height, keeping the graph compact without large vertical gaps.
- **Exec-edge awareness**: exec pins are weighted more heavily than data pins so execution flow drives the vertical ordering.
- **Selection-aware**: arranges only the selected nodes; falls back to all nodes in the graph when nothing is selected.
- **Grid-snapped**: all output positions are snapped to the 16-unit grid and centered on the original bounding box.

## Installation

1. Clone or download this repository into your project's `Plugins/` folder:
   ```
   <YourProject>/Plugins/BlueprintArrange/
   ```
2. Regenerate project files and rebuild the project (or launch the editor and let Live Coding compile the plugin).
3. Enable the plugin in **Edit → Plugins** if it is not already enabled (it is enabled by default when placed in the project's `Plugins/` folder).

## Usage

1. Open any Blueprint graph.
2. Select the nodes you want to arrange (or leave the selection empty to arrange everything).
3. Right-click and choose **Arrange** from the context menu.

## Layout tuning

Spacing is defined in `Source/BlueprintArrange/Private/GraphArranger.h`:

```cpp
struct FBlueprintArrangeLayoutSettings
{
    int32 ColumnSpacing = 320;  // horizontal gap between columns
    int32 RowSpacing = 90;      // vertical gap between nodes within a column
};
```

Adjust these values and rebuild to change the spacing.

## Supported engine versions

Unreal Engine 5.8.

## License

MIT. See [LICENSE](LICENSE).
