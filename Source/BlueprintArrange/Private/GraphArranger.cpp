// Copyright Epic Games, Inc. All Rights Reserved.

#include "GraphArranger.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"

namespace
{
	// Widget-free height estimate: base height plus a fixed amount per pin on the busier side.
	int32 EstimateNodeHeight(const UEdGraphNode* Node)
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
		return FMath::Clamp(56 + 24 * FMath::Max(InPinCount, OutPinCount), 96, 512);
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

	// Weight for a pin index: lower index (visually higher) → larger weight so the
	// neighbor is pulled toward the top. Exec edges are amplified by ExecMultiplier.
	// PinIndex is the visible (non-hidden) pin index, or INDEX_NONE for untracked.
	constexpr float ExecMultiplier = 10.0f;
	auto PinWeight = [](int32 PinIndex, bool bExec) -> float
	{
		if (PinIndex < 0)
		{
			return 1.0f; // untracked / fallback
		}
		// +1 so pin 0 still has positive weight; invert so lower index = higher weight.
		float W = 1.0f / static_cast<float>(PinIndex + 1);
		return bExec ? W * ExecMultiplier : W;
	};

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
	// ---------------------------------------------------------------------
	TArray<int32> Rank;
	Rank.Init(0, NumNodes);
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
					const float W = PinWeight(Link.PinIndex, Link.bExec);
					WeightedOrder += W * static_cast<float>(OrderOf[Link.NeighborNode]);
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
	// 5. Coordinates: per-column tight packing. Each rank (column) stacks its
	//    nodes top-to-bottom using each node's own estimated height, so columns
	//    with only short nodes no longer inherit the tallest node's height from
	//    other columns. This removes the large vertical gaps the uniform-track
	//    scheme produced. Empty nodes are placed in a separate bottom band.
	//    Snapped to the 16-unit grid, centered on original bounding box.
	// ---------------------------------------------------------------------
	TArray<int32> NewX;
	TArray<int32> NewY;
	NewX.Init(0, NumNodes);
	NewY.Init(0, NumNodes);

	int32 ConnectedBottom = 0; // tallest connected column (for the empty band).

	for (int32 r = 0; r <= MaxRank; ++r)
	{
		const int32 ColumnX = r * Settings.ColumnSpacing;
		int32 CursorY = 0;        // running Y for connected nodes in this column.
		int32 EmptyCursorY = 0;   // running Y for empty nodes in this column.

		for (int32 n : Ranks[r])
		{
			NewX[n] = ColumnX;
			const int32 NodeH = EstimateNodeHeight(Nodes[n]);
			if (EmptyNodeIndices.Contains(n))
			{
				NewY[n] = EmptyCursorY;
				EmptyCursorY += NodeH + Settings.RowSpacing;
			}
			else
			{
				NewY[n] = CursorY;
				CursorY += NodeH + Settings.RowSpacing;
			}
		}

		// The empty band must sit below the tallest connected column so empty
		// nodes never overlap connected ones. Track the max connected bottom.
		ConnectedBottom = FMath::Max(ConnectedBottom, CursorY);
	}

	// Drop the empty band below the tallest connected column.
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
