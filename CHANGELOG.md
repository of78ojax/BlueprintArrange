# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Material and Material Function graphs; positions are synced back to the material expressions.
- Separate "Arrange Selection" and "Arrange Graph" entries in the Organization section of the node context menu.
- Undo/redo support ("Arrange Nodes" transaction).

### Changed
- Reroute (knot) nodes no longer take a column; selected knots are placed right after their source node.
- Comment boxes are not laid out themselves; they are refitted around the nodes they framed before arranging (nested comments included).
- Each column is only as wide as its own widest node.
- Wires are aligned pin-to-pin using the real pin widget offsets when available.
- Ranking is linear in the graph size.

### Fixed
- Graphs with loops no longer explode into hundreds of columns (loop-back edges are ignored for layout).
- The menu entry no longer appears in read-only views or in unsupported graphs (Behavior Trees, state machines, ...).

## [1.0.0] - 2024-08-22

### Added
- Sugiyama-style layered auto-layout for Blueprint node graphs.
- Context-menu "Arrange" action (right-click on node or graph background).
- Longest-path ranking, barycenter crossing reduction, per-column coordinate assignment.
- Pin-index-weighted barycenter with exec-edge amplification.
- Selection-aware arranging (falls back to all nodes when nothing is selected).
- 16-unit grid snapping and bounding-box centering.

### Changed
- Coordinate assignment switched from uniform global tracks to per-column tight packing to eliminate large vertical gaps.
