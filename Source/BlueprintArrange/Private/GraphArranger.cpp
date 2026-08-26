// Copyright Epic Games, Inc. All Rights Reserved.

#include "GraphArranger.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "GraphEditor.h"
#include "SGraphPanel.h"

namespace
{
	// Pin-based height fallback used when the node's Slate widget isn't
	// available (graph panel not open, node not yet ticked, etc.).
	int32 EstimateNodeHeight(const UEdGraphNode* Node, const FBlueprintArrangeLayoutSettings& Settings)
	{
		int32 InPinCount = 0;
		int32 OutPinCount = 0;
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->bHidden)
			{
				continue;
			}
			if (Pin->Direction == EGPD_Input)
			{
				++InPinCount;
			}
			else
			{
				++OutPinCount;
			}
		}
		return FMath::Clamp(
			Settings.FallbackBaseHeight + Settings.FallbackPinHeight * FMath::Max(InPinCount, OutPinCount),
			Settings.FallbackMinHeight,
			Settings.FallbackMaxHeight);
	}

	// Node size cache: resolves the real rendered size of each node from its
	// Slate widget when available, falling back to pin-based estimation.
	struct FNodeSize
	{
		int32 Width;
		int32 Height;
	};

	// Build a size lookup for all nodes. Tries SGraphPanel::GetBoundsForNode
	// first (exact rendered size), then falls back to pin-count estimation.
	// Returns node index -> size.
	TArray<FNodeSize> BuildNodeSizes(
		const TArray<UEdGraphNode*>& Nodes,
		const UEdGraph* Graph,
		const FBlueprintArrangeLayoutSettings& Settings)
	{
		const int32 NumNodes = Nodes.Num();
		TArray<FNodeSize> Sizes;
		Sizes.SetNum(NumNodes);

		// Try to grab the graph panel hosting these nodes.
		TSharedPtr<SGraphEditor> GraphEditor;
		SGraphPanel* GraphPanel = nullptr;
		if (Graph)
		{
			GraphEditor = SGraphEditor::FindGraphEditorForGraph(Graph);
			if (GraphEditor)
			{
				GraphPanel = GraphEditor->GetGraphPanel();
			}
		}

		for (int32 i = 0; i < NumNodes; ++i)
		{
			const UEdGraphNode* Node = Nodes[i];
			if (!Node)
			{
				Sizes[i] = {Settings.FallbackDefaultWidth, Settings.FallbackMinHeight};
				continue;
			}

			bool bGotRealSize = false;

			// 1. SGraphPanel::GetBoundsForNode — exact rendered bounds.
			if (GraphPanel)
			{
				FVector2f MinCorner(0, 0);
				FVector2f MaxCorner(0, 0);
				if (GraphPanel->GetBoundsForNode(Node, MinCorner, MaxCorner, 0.f))
				{
					const int32 W = FMath::TruncToInt(MaxCorner.X - MinCorner.X);
					const int32 H = FMath::TruncToInt(MaxCorner.Y - MinCorner.Y);
					if (W > 0 && H > 0)
					{
						Sizes[i] = {W, H};
						bGotRealSize = true;
					}
				}
			}

			// 2. Fallback to pin-count estimation.
			if (!bGotRealSize)
			{
				Sizes[i] = {Settings.FallbackDefaultWidth, EstimateNodeHeight(Node, Settings)};
			}
		}

		return Sizes;
	}

	struct FArrangeEdge
	{
		int32 From;
		int32 To;
		/** Index of the output pin on the source node (visual top-to-bottom order). */
		int32 SourcePinIndex;
		/** Index of the input pin on the target node (visual top-to-bottom order). */
		int32 TargetPinIndex;
		/** True if this edge connects two exec pins (PinCategory == PC_Exec). */
		bool bIsExec;
	};

	struct FNeighborLink
	{
		int32 NeighborNode;
		/** Pin index on the node being ordered (input pin for downward, output pin for upward). */
		int32 PinIndex;
		bool bExec;
	};

	// Exec edges are amplified by ExecMultiplier so they dominate crossing
	// reduction over data edges. Pin ordering is handled separately via a
	// pin-index offset added to the barycenter (see PinOrderScale / PinPixelScale).
	constexpr float ExecMultiplier = 10.0f;
	auto PinWeight = [](bool bExec) -> float
	{
		return bExec ? ExecMultiplier : 1.0f;
	};

	// Pin-index offset added to the barycenter so neighbours connected through
	// lower pins (visually higher on the node) are pulled toward the top of the
	// column. This survives the weight normalisation (unlike a pure weight
	// multiplier, which cancels out for single-neighbour nodes). PinIndex is the
	// visible (non-hidden) pin index, or INDEX_NONE for untracked.
	constexpr float PinOrderScale = 0.75f; // ordering space: order slots per pin step
	constexpr float PinPixelScale = 24.0f;  // coordinate space: approx visual pin spacing

	// Visible (non-hidden) pin index in the node's Pins array, which matches the
	// visual top-to-bottom render order.
	int32 VisiblePinIndex(const UEdGraphNode* Node, const UEdGraphPin* Pin)
	{
		if (!Node || !Pin)
		{
			return INDEX_NONE;
		}
		int32 VisibleIdx = 0;
		for (const UEdGraphPin* P : Node->Pins)
		{
			if (!P || P->bHidden)
			{
				continue;
			}
			if (P == Pin)
			{
				return VisibleIdx;
			}
			++VisibleIdx;
		}
		return INDEX_NONE;
	}
} // namespace

int32 ArrangeNodes(const TArray<UEdGraphNode*>& Nodes, const FBlueprintArrangeLayoutSettings& Settings)
{
	const int32 NumNodes = Nodes.Num();
	if (NumNodes <= 1)
	{
		return 0;
	}

	// ---------------------------------------------------------------------
	// 1. Node -> index map.
	// ---------------------------------------------------------------------
	TMap<UEdGraphNode*, int32> NodeToIndex;
	NodeToIndex.Reserve(NumNodes);
	for (int32 i = 0; i < NumNodes; ++i)
	{
		if (Nodes[i])
		{
			NodeToIndex.Add(Nodes[i], i);
		}
	}

	// ---------------------------------------------------------------------
	// 1b. Resolve real node sizes from the Slate widgets (with fallback).
	// ---------------------------------------------------------------------
	const UEdGraph* HostGraph = Nodes[0] ? Nodes[0]->GetGraph() : nullptr;
	const TArray<FNodeSize> NodeSizes = BuildNodeSizes(Nodes, HostGraph, Settings);

	// ---------------------------------------------------------------------
	// 2. Edges: output pin owner -> linked input pin owner.
	//    Carries pin indices + exec flag. Parallel edges between the same node
	//    pair but different pins are preserved (dedup on pin-level key).
	// ---------------------------------------------------------------------
	TArray<FArrangeEdge> EdgeList;
	TSet<uint64> UniqueEdges;

	const UEdGraphSchema_K2* K2Schema = nullptr;
	if (const UEdGraph* Graph = Nodes[0] ? Nodes[0]->GetGraph() : nullptr)
	{
		K2Schema = Cast<UEdGraphSchema_K2>(Graph->GetSchema());
	}
	auto IsExecPin = [K2Schema](const UEdGraphPin* Pin) -> bool
	{
		return K2Schema && Pin && Pin->PinType.PinCategory == K2Schema->PC_Exec;
	};

	for (int32 U = 0; U < NumNodes; ++U)
	{
		UEdGraphNode* NodeU = Nodes[U];
		if (!NodeU)
		{
			continue;
		}
		for (const UEdGraphPin* Pin : NodeU->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Output || Pin->bHidden)
			{
				continue;
			}
			for (const UEdGraphPin* LinkedTo : Pin->LinkedTo)
			{
				const UEdGraphNode* LinkedNode = LinkedTo ? LinkedTo->GetOwningNode() : nullptr;
				if (!LinkedNode || LinkedNode == NodeU)
				{
					continue;
				}
				const int32* V = NodeToIndex.Find(LinkedNode);
				if (!V)
				{
					continue;
				}

				const int32 SrcPinIdx = VisiblePinIndex(NodeU, Pin);
				const int32 DstPinIdx = VisiblePinIndex(LinkedNode, LinkedTo);
				const bool bExec = IsExecPin(Pin) && IsExecPin(LinkedTo);

				// Dedup on (From, To, SourcePin, TargetPin) so parallel edges to
				// different pins are preserved. +1 avoids INDEX_NONE (-1) collision.
				const uint64 Key = (static_cast<uint64>(U) << 40)
					| (static_cast<uint64>(*V) << 20)
					| (static_cast<uint64>(SrcPinIdx + 1) << 10)
					| static_cast<uint64>(DstPinIdx + 1);
				bool bAlready = false;
				UniqueEdges.Add(Key, &bAlready);
				if (!bAlready)
				{
					EdgeList.Add({U, *V, SrcPinIdx, DstPinIdx, bExec});
				}
			}
		}
	}

	// Identify "empty" nodes: no incoming or outgoing edges within the selection.
	TSet<int32> EmptyNodeIndices;
	{
		TSet<int32> ConnectedIndices;
		for (const FArrangeEdge& E : EdgeList)
		{
			ConnectedIndices.Add(E.From);
			ConnectedIndices.Add(E.To);
		}
		for (int32 i = 0; i < NumNodes; ++i)
		{
			if (!ConnectedIndices.Contains(i))
			{
				EmptyNodeIndices.Add(i);
			}
		}
	}

	// ---------------------------------------------------------------------
	// 3. Ranking: longest-path via iterative relaxation (cycle-safe).
	//    Forward pass pushes consumers right; reverse pass (tightening)
	//    pulls sources toward their consumers so data-input nodes land
	//    just before the node that uses them instead of column 0.
	// ---------------------------------------------------------------------
	TArray<int32> Rank;
	Rank.Init(0, NumNodes);

	// Forward relaxation: Rank[consumer] >= Rank[source] + 1.
	for (int32 Pass = 0; Pass < NumNodes; ++Pass)
	{
		bool bChanged = false;
		for (const FArrangeEdge& E : EdgeList)
		{
			if (Rank[E.To] < Rank[E.From] + 1)
			{
				Rank[E.To] = Rank[E.From] + 1;
				bChanged = true;
			}
		}
		if (!bChanged)
		{
			break;
		}
	}

	// Reverse relaxation (tightening): pull each node as close to its
	// consumers as possible.  Rank[n] is raised toward min(consumer)-1
	// but never above it, and never below max(source)+1.  Nodes on the
	// critical path are already tight and won't move; only "loose" nodes
	// (e.g. data-only sources left at rank 0 by the forward pass) get
	// pulled right to sit just before the node that consumes them.
	for (int32 Pass = 0; Pass < NumNodes; ++Pass)
	{
		bool bChanged = false;
		for (int32 n = 0; n < NumNodes; ++n)
		{
			if (EmptyNodeIndices.Contains(n))
			{
				continue;
			}

			int32 MinConsumerRank = MAX_int32;
			for (const FArrangeEdge& E : EdgeList)
			{
				if (E.From == n)
				{
					MinConsumerRank = FMath::Min(MinConsumerRank, Rank[E.To]);
				}
			}
			if (MinConsumerRank == MAX_int32)
			{
				continue; // no outgoing edges — sink, nothing to pull
			}

			int32 MaxSourceRank = -1;
			for (const FArrangeEdge& E : EdgeList)
			{
				if (E.To == n)
				{
					MaxSourceRank = FMath::Max(MaxSourceRank, Rank[E.From]);
				}
			}

			const int32 NewRank = FMath::Max(MinConsumerRank - 1, MaxSourceRank + 1);
			if (NewRank > Rank[n])
			{
				Rank[n] = NewRank;
				bChanged = true;
			}
		}
		if (!bChanged)
		{
			break;
		}
	}

	int32 MaxRank = 0;
	for (int32 r : Rank)
	{
		MaxRank = FMath::Max(MaxRank, r);
	}

	// ---------------------------------------------------------------------
	// 4. Ordering: pin-index-weighted barycenter crossing reduction,
	//    alternating down/up sweeps. Exec edges weighted higher.
	//    Empty nodes are excluded from reordering (stay at end of rank).
	// ---------------------------------------------------------------------
	TArray<TArray<int32>> Ranks;
	Ranks.SetNum(MaxRank + 1);
	for (int32 i = 0; i < NumNodes; ++i)
	{
		Ranks[Rank[i]].Add(i);
	}
	// Initial within-rank order: connected nodes first (by ascending Y),
	// empty nodes last (by ascending Y).
	for (TArray<int32>& SameRank : Ranks)
	{
		SameRank.StableSort([&Nodes, &EmptyNodeIndices](int32 A, int32 B)
		{
			const bool bAEmpty = EmptyNodeIndices.Contains(A);
			const bool bBEmpty = EmptyNodeIndices.Contains(B);
			if (bAEmpty != bBEmpty)
			{
				return !bAEmpty; // connected first, empty last
			}
			return Nodes[A]->NodePosY < Nodes[B]->NodePosY;
		});
	}

	TMap<int32, int32> OrderOf;
	auto RebuildOrders = [&Ranks, &OrderOf]()
	{
		OrderOf.Reset();
		for (const TArray<int32>& SameRank : Ranks)
		{
			for (int32 Pos = 0; Pos < SameRank.Num(); ++Pos)
			{
				OrderOf.Add(SameRank[Pos], Pos);
			}
		}
	};
	RebuildOrders();

	// Build edge-aware adjacency with pin info.
	TArray<TArray<FNeighborLink>> Upstream;
	TArray<TArray<FNeighborLink>> Downstream;
	Upstream.SetNum(NumNodes);
	Downstream.SetNum(NumNodes);
	for (const FArrangeEdge& E : EdgeList)
	{
		// Downstream[E.From]: the target's input pin index matters for ordering E.From's column.
		Downstream[E.From].Add({E.To, E.TargetPinIndex, E.bIsExec});
		// Upstream[E.To]: the source's output pin index matters for ordering E.To's column.
		Upstream[E.To].Add({E.From, E.SourcePinIndex, E.bIsExec});
	}

	constexpr int32 NumSweeps = 4;
	for (int32 Sweep = 0; Sweep < NumSweeps; ++Sweep)
	{
		const bool bDownward = (Sweep % 2) == 0;
		for (int32 ri = 0; ri <= MaxRank; ++ri)
		{
			const int32 r = bDownward ? ri : MaxRank - ri;

			// Pin-index-weighted barycenter: each neighbor's order is weighted by
			// PinWeight(PinIndex, bExec). Lower pin index → higher weight →
			// neighbor pulled toward the top. Exec edges amplified.
			TArray<TPair<float, int32>> Keys;
			Keys.Reserve(Ranks[r].Num());
			for (int32 n : Ranks[r])
			{
				// Empty nodes keep their position; don't participate in barycenter.
				if (EmptyNodeIndices.Contains(n))
				{
					Keys.Add({static_cast<float>(OrderOf[n]), n});
					continue;
				}
				const TArray<FNeighborLink>& Neighbors = bDownward ? Upstream[n] : Downstream[n];
				if (Neighbors.Num() == 0)
				{
					Keys.Add({static_cast<float>(OrderOf[n]), n});
					continue;
				}
				float WeightSum = 0.f;
				float WeightedOrder = 0.f;
				for (const FNeighborLink& Link : Neighbors)
				{
					const float W = PinWeight(Link.bExec);
					const int32 PinIdx = FMath::Max(0, Link.PinIndex);
					WeightedOrder += W * (static_cast<float>(OrderOf[Link.NeighborNode]) + PinIdx * PinOrderScale);
					WeightSum += W;
				}
				Keys.Add({WeightSum > 0.f ? WeightedOrder / WeightSum : static_cast<float>(OrderOf[n]), n});
			}

			// Deterministic total order: key first, prior order breaks ties.
			Keys.Sort([&OrderOf](const TPair<float, int32>& A, const TPair<float, int32>& B)
			{
				if (A.Key != B.Key)
				{
					return A.Key < B.Key;
				}
				return OrderOf[A.Value] < OrderOf[B.Value];
			});

			TArray<int32> NewOrder;
			NewOrder.Reserve(Keys.Num());
			for (const TPair<float, int32>& K : Keys)
			{
				NewOrder.Add(K.Value);
			}
			Ranks[r] = MoveTemp(NewOrder);
			RebuildOrders();
		}
	}

	// ---------------------------------------------------------------------
	// 5. Coordinates: barycenter coordinate assignment. We alternate down
	//    (rank 0 -> MaxRank, pulling each column toward its upstream
	//    neighbours) and up (MaxRank -> 0, pulling toward downstream
	//    neighbours) sweeps. On every sweep a column's nodes first get a
	//    desired Y = pin-weighted average of the already-placed neighbours'
	//    actual Y, then the column is packed top-to-bottom preserving the
	//    Phase-4 order while following that desired Y (never overlapping).
	//    Because neighbours can sit several ranks away, edges that skip
	//    intermediate columns still align their endpoints, so long edges no
	//    longer cut through nodes in between. Empty nodes are excluded from
	//    the relaxation and dropped into a separate bottom band afterwards.
	//    Final positions are snapped to the 16-unit grid and centered on the
	//    nodes' original bounding box.
	// ---------------------------------------------------------------------
	TArray<int32> NewX;
	TArray<int32> NewY;
	NewX.Init(0, NumNodes);
	NewY.Init(0, NumNodes);

	TArray<float> DesiredY;
	DesiredY.Init(0.f, NumNodes);

	// Effective column pitch: base spacing plus the widest node in the whole
	// selection, so right-aligned columns never overlap regardless of node
	// widths. The gap between adjacent columns is at least ColumnSpacing.
	int32 MaxNodeWidth = 0;
	for (const FNodeSize& S : NodeSizes)
	{
		MaxNodeWidth = FMath::Max(MaxNodeWidth, S.Width);
	}
	const int32 ColumnPitch = Settings.ColumnSpacing + MaxNodeWidth;

	constexpr int32 NumCoordSweeps = 8;
	for (int32 Sweep = 0; Sweep < NumCoordSweeps; ++Sweep)
	{
		const bool bDownward = (Sweep % 2) == 0;
		for (int32 ri = 0; ri <= MaxRank; ++ri)
		{
			const int32 r = bDownward ? ri : MaxRank - ri;
			const int32 ColumnX = r * ColumnPitch;

			// Desired Y per node from the already-placed opposite column(s).
			// Neighbours may be several ranks away (edges that skip columns),
			// but they are always placed earlier in this sweep, so their Y is
			// current.
			for (int32 n : Ranks[r])
			{
				if (EmptyNodeIndices.Contains(n))
				{
					DesiredY[n] = static_cast<float>(NewY[n]);
					continue;
				}
				const TArray<FNeighborLink>& Neighbors = bDownward ? Upstream[n] : Downstream[n];
				if (Neighbors.Num() == 0)
				{
					DesiredY[n] = static_cast<float>(NewY[n]);
					continue;
				}
				float WeightSum = 0.f;
				float WeightedY = 0.f;
				for (const FNeighborLink& Link : Neighbors)
				{
					const float W = PinWeight(Link.bExec);
					const int32 PinIdx = FMath::Max(0, Link.PinIndex);
					// Offset in pixel space so the node sits opposite its source pin:
					// upstream (downward sweep) → positive offset (lower pin = lower Y),
					// downstream (upward sweep) → also positive (lower output pin = lower Y).
					const float PinOffset = bDownward ? (PinIdx * PinPixelScale) : (PinIdx * PinPixelScale);
					WeightedY += W * (static_cast<float>(NewY[Link.NeighborNode]) + PinOffset);
					WeightSum += W;
				}
				DesiredY[n] = WeightSum > 0.f ? WeightedY / WeightSum : static_cast<float>(NewY[n]);
			}

			// Pack this column preserving the Phase-4 order while following
			// the desired Y. The monotonic cursor guarantees no overlap and
			// keeps the pin-aware within-column order intact; the desired Y
			// aligns the column with its connected neighbours across columns.
			// Nodes are right-aligned within the column: the widest node's
			// right edge defines the column edge, narrower nodes are shifted
			// right so all right edges line up (cleaner wire entry points).
			int32 MaxWidthInColumn = 0;
			for (int32 n : Ranks[r])
			{
				MaxWidthInColumn = FMath::Max(MaxWidthInColumn, NodeSizes[n].Width);
			}

			int32 CursorY = 0;      // running Y for connected nodes (monotonic).
			int32 EmptyCursorY = 0; // running Y for empty nodes (separate band).
			for (int32 n : Ranks[r])
			{
				NewX[n] = ColumnX + MaxWidthInColumn - NodeSizes[n].Width;
				const int32 NodeH = NodeSizes[n].Height;
				if (EmptyNodeIndices.Contains(n))
				{
					NewY[n] = EmptyCursorY;
					EmptyCursorY += NodeH + Settings.RowSpacing;
				}
				else
				{
					const int32 Desired = FMath::Max(0, FMath::RoundToInt(DesiredY[n]));
					CursorY = FMath::Max(CursorY, Desired);
					NewY[n] = CursorY;
					CursorY += NodeH + Settings.RowSpacing;
				}
			}
		}
	}

	// Drop the empty band below the tallest connected column so empty nodes
	// never overlap connected ones.
	int32 ConnectedBottom = 0;
	for (int32 r = 0; r <= MaxRank; ++r)
	{
		int32 ColumnBottom = 0;
		for (int32 n : Ranks[r])
		{
			if (EmptyNodeIndices.Contains(n))
			{
				continue;
			}
			ColumnBottom = FMath::Max(ColumnBottom, NewY[n] + NodeSizes[n].Height + Settings.RowSpacing);
		}
		ConnectedBottom = FMath::Max(ConnectedBottom, ColumnBottom);
	}
	for (int32 i = 0; i < NumNodes; ++i)
	{
		if (EmptyNodeIndices.Contains(i))
		{
			NewY[i] += ConnectedBottom;
		}
	}

	auto Center = [](int32 Min, int32 Max) -> double
	{
		return (static_cast<double>(Min) + Max) / 2.0;
	};

	int32 OldMinX = MAX_int32, OldMinY = MAX_int32, OldMaxX = MIN_int32, OldMaxY = MIN_int32;
	int32 NewMinX = MAX_int32, NewMinY = MAX_int32, NewMaxX = MIN_int32, NewMaxY = MIN_int32;
	for (int32 i = 0; i < NumNodes; ++i)
	{
		OldMinX = FMath::Min(OldMinX, Nodes[i]->NodePosX);
		OldMaxX = FMath::Max(OldMaxX, Nodes[i]->NodePosX);
		OldMinY = FMath::Min(OldMinY, Nodes[i]->NodePosY);
		OldMaxY = FMath::Max(OldMaxY, Nodes[i]->NodePosY);

		NewMinX = FMath::Min(NewMinX, NewX[i]);
		NewMaxX = FMath::Max(NewMaxX, NewX[i]);
		NewMinY = FMath::Min(NewMinY, NewY[i]);
		NewMaxY = FMath::Max(NewMaxY, NewY[i]);
	}

	constexpr int32 GridSize = 16;
	const int32 OffsetX = FMath::GridSnap(static_cast<float>(Center(OldMinX, OldMaxX) - Center(NewMinX, NewMaxX)), static_cast<float>(GridSize));
	const int32 OffsetY = FMath::GridSnap(static_cast<float>(Center(OldMinY, OldMaxY) - Center(NewMinY, NewMaxY)), static_cast<float>(GridSize));

	int32 NumMoved = 0;
	for (int32 i = 0; i < NumNodes; ++i)
	{
		const int32 FinalX = FMath::GridSnap(NewX[i] + OffsetX, GridSize);
		const int32 FinalY = FMath::GridSnap(NewY[i] + OffsetY, GridSize);
		if (Nodes[i]->NodePosX != FinalX || Nodes[i]->NodePosY != FinalY)
		{
			++NumMoved;
		}
		Nodes[i]->NodePosX = FinalX;
		Nodes[i]->NodePosY = FinalY;
	}

	return NumMoved;
}
