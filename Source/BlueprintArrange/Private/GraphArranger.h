// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UEdGraph;
class UEdGraphNode;

/** Hardcoded layout tuning (no settings UI in v1). */
struct FBlueprintArrangeLayoutSettings
{
	int32 ColumnSpacing = 80;   // horizontal gap between a column's widest node and the next column
	int32 RowSpacing = 90;

	// --- Fallback node size estimation (used only when the real Slate widget
	//     size is unavailable, e.g. the graph panel isn't open / hasn't ticked).
	int32 FallbackBaseHeight = 56;   // base height before any pins
	int32 FallbackPinHeight = 24;    // height added per visible pin on the busier side
	int32 FallbackTitleHeight = 32;  // header height above the first pin row
	int32 FallbackMinHeight = 96;    // clamp so tiny nodes stay readable
	int32 FallbackMaxHeight = 512;   // clamp so huge nodes don't dominate
	int32 FallbackDefaultWidth = 200;
	int32 FallbackKnotSize = 16;     // reroute (knot) nodes
};

/**
 * Sugiyama-style layered auto-layout for the given nodes.
 *
 * Breaks cycles (loop-back wires are ignored for layout), runs longest-path
 * ranking with tightening, barycenter crossing reduction, and per-column
 * coordinate assignment that aims for straight wires. Reroute (knot) nodes
 * don't get columns of their own; selected knots are placed in the gap after
 * their source node. Positions are written directly to NodePosX/NodePosY,
 * snapped to the 16-unit grid, centered on the nodes' original bounding box.
 *
 * @param Nodes  The nodes to arrange: non-null, unique, no comment nodes.
 * @param Settings  Column/row spacing and fallback sizes.
 * @return Number of nodes whose position changed.
 */
int32 ArrangeNodes(const TArray<UEdGraphNode*>& Nodes, const FBlueprintArrangeLayoutSettings& Settings = {});
