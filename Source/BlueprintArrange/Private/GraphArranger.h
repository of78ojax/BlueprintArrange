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
