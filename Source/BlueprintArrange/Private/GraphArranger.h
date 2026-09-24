// Copyright (c) Blueprint Arrange contributors. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"

class UEdGraph;
class UEdGraphNode;

/** Hardcoded layout tuning (no settings UI in v1). */
struct FBlueprintArrangeLayoutSettings
{
	int32 ColumnSpacing = 80;   // horizontal gap between a column's widest node and the next column
	int32 RowSpacing = 90;
	int32 DataRowSpacing = 24;  // vertical gap between two stacked data-only (no exec pin) nodes
	int32 FeederSpacing = 48;   // horizontal gap between a feeder (getter, constant, ...) and its consumer

	// --- Fallback node size estimation (used only when the real Slate widget
	//     size is unavailable, e.g. the graph panel isn't open / hasn't ticked).
	int32 FallbackBaseHeight = 56;   // base height before any pins
	int32 FallbackPinHeight = 24;    // height added per visible pin on the busier side
	int32 FallbackTitleHeight = 32;  // header height above the first pin row
	int32 FallbackMinHeight = 96;    // clamp so tiny nodes stay readable
	int32 FallbackMaxHeight = 512;   // clamp so huge nodes don't dominate
	int32 FallbackDefaultWidth = 200;
	int32 FallbackKnotSize = 16;     // reroute (knot) nodes

	int32 CommentPadding = 50;       // margin around a refitted comment's contents (same as "Create Comment")
};

class UEdGraphNode_Comment;

/** A comment box and what it framed before arranging. */
struct FCommentFrame
{
	UEdGraphNode_Comment* Comment = nullptr;
	/** Nodes and nested comments that were fully inside the box. */
	TArray<UEdGraphNode*> Contents;
	/** Contents' positions at capture time, to detect whether anything moved. */
	TArray<FIntPoint> OriginalPositions;
	/** Contents' sizes at capture time (nested comments are re-read when refitting). */
	TArray<FIntPoint> Sizes;
};

/**
 * Record which nodes each comment in the graph frames. Call before ArrangeNodes.
 * Frames are sorted smallest first, so nested comments are refitted before
 * the comments around them.
 */
TArray<FCommentFrame> CaptureCommentFrames(const UEdGraph* Graph, const FBlueprintArrangeLayoutSettings& Settings = {});

/**
 * Resize each comment whose contents moved so it frames them again. Calls
 * Modify() on the comments it changes, so run it inside the transaction.
 * @return Number of comments changed.
 */
int32 RefitCommentFrames(const TArray<FCommentFrame>& Frames, const FBlueprintArrangeLayoutSettings& Settings = {});

/**
 * Sugiyama-style layered auto-layout for the given nodes.
 *
 * Breaks cycles (loop-back wires are ignored for layout), runs longest-path
 * ranking with tightening, barycenter crossing reduction, and per-column
 * coordinate assignment that aims for straight wires. Reroute (knot) nodes
 * don't get columns of their own; selected knots are placed in the gap after
 * their source node. Leaf data nodes that feed a single node (getters,
 * constants, parameters) are attached to the left of that node instead of
 * taking a column slot; the pair is laid out as one block. Positions are written directly to NodePosX/NodePosY,
 * snapped to the 16-unit grid, centered on the nodes' original bounding box.
 *
 * @param Nodes  The nodes to arrange: non-null, unique, no comment nodes.
 * @param Settings  Column/row spacing and fallback sizes.
 * @return Number of nodes whose position changed.
 */
int32 ArrangeNodes(const TArray<UEdGraphNode*>& Nodes, const FBlueprintArrangeLayoutSettings& Settings = {});
