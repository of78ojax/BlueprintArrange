# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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
