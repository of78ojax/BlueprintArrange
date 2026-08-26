// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UEdGraph;
class UEdGraphNode;

/** Hardcoded layout tuning (no settings UI in v1). */
struct FBlueprintArrangeLayoutSettings
{
	int32 ColumnSpacing = 320;
	int32 RowSpacing = 90;

	// --- Fallback node size estimation (used only when the real Slate widget
	//     size is unavailable, e.g. the graph panel isn't open / hasn't ticked).
	int32 FallbackBaseHeight = 56;   // base height before any pins
	int32 FallbackPinHeight = 24;    // height added per visible pin on the busier side
	int32 FallbackMinHeight = 96;    // clamp so tiny nodes stay readable
	int32 FallbackMaxHeight = 512;   // clamp so huge nodes don't dominate
	int32 FallbackDefaultWidth = 200;
};

/**
 * Sugiyama-style layered auto-layout for the given nodes.
 *
 * Runs longest-path ranking, barycenter crossing reduction, and uniform-column
 * coordinate assignment. Positions are written directly to NodePosX/NodePosY,
 * snapped to the 16-unit grid, centered on the nodes' original bounding box.
 *
 * @param Nodes  The nodes to arrange (typically the editor selection).
 * @param Settings  Column/row spacing.
 * @return Number of nodes whose position changed.
 */
int32 ArrangeNodes(const TArray<UEdGraphNode*>& Nodes, const FBlueprintArrangeLayoutSettings& Settings = {});
