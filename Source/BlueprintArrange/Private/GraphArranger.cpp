// Copyright (c) Blueprint Arrange contributors. Licensed under the MIT License.

#include "GraphArranger.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphNode_Comment.h"
#include "GraphEditor.h"
#include "Layout/SlateRect.h"
#include "SGraphPanel.h"
#include "SGraphNode.h"
#include "SGraphPin.h"

namespace
{
	// ======================================================================
	//  Types & constants
	// ======================================================================

	constexpr int32 GridSize = 16;

	// Node size cache: resolves the real rendered size of each node from its
	// Slate widget when available, falling back to pin-based estimation.
	struct FNodeSize
	{
		int32 Width;
		int32 Height;
	};

	// Where a pin sits on its node. Inputs and outputs are drawn as separate
	// stacks, so the index is among visible pins of the same direction.
	struct FPinLayout
	{
		int32 SideIndex = 0;
		int32 SideCount = 1;
		/** Pin centre Y relative to the node's top. */
		float OffsetY = 0.f;
	};

	struct FArrangeEdge
	{
		int32 From;
		int32 To;
		/** Output pin on the source node. */
		FPinLayout SourcePin;
		/** Input pin on the target node. */
		FPinLayout TargetPin;
		/** True if this edge connects two exec pins. */
		bool bIsExec = false;
		/** True if this edge closes a cycle; ignored by ranking and placement. */
		bool bIsFeedback = false;
		/** True if the wire runs through one or more reroute nodes. */
		bool bViaKnot = false;
	};

	struct FNeighborLink
	{
		int32 NeighborNode;
		/** Pin centre Y on the neighbour, relative to the neighbour's top. */
		float NeighborPinY;
		/** Pin centre Y on the node being placed, relative to its own top. */
		float SelfPinY;
		/** The neighbour's pin among the visible pins on its side. */
		int32 NeighborSideIndex;
		int32 NeighborSideCount;
		bool bExec;
	};

	// A selected reroute node, placed relative to the real node that feeds it.
	struct FKnotPlacement
	{
		int32 SourceNode;
		float SourcePinY;
		/** Number of knots between the source node and this one. */
		int32 Depth;
	};

	// Exec edges are amplified by ExecMultiplier so they dominate crossing
	// reduction and coordinate assignment over data edges.
	constexpr float ExecMultiplier = 10.0f;
	float PinWeight(bool bExec)
	{
		return bExec ? ExecMultiplier : 1.0f;
	}

	// ======================================================================
	//  Small helpers
	// ======================================================================

	int32 SnapToGrid(double Value)
	{
		return FMath::RoundToInt(Value / GridSize) * GridSize;
	}

	bool IsKnot(const UEdGraphNode* Node)
	{
		int32 InputPinIndex = INDEX_NONE;
		int32 OutputPinIndex = INDEX_NONE;
		return Node->ShouldDrawNodeAsControlPointOnly(InputPinIndex, OutputPinIndex);
	}

	const UEdGraphPin* GetKnotOutputPin(const UEdGraphNode* Knot)
	{
		int32 InputPinIndex = INDEX_NONE;
		int32 OutputPinIndex = INDEX_NONE;
		if (Knot->ShouldDrawNodeAsControlPointOnly(InputPinIndex, OutputPinIndex) && Knot->Pins.IsValidIndex(OutputPinIndex))
		{
			return Knot->Pins[OutputPinIndex];
		}
		return nullptr;
	}

	bool IsPinVisible(const UEdGraphNode* Node, const UEdGraphPin* Pin)
	{
		return Pin
			&& !Pin->bHidden
			&& !(Pin->bAdvancedView && Node->AdvancedPinDisplay == ENodeAdvancedPins::Hidden);
	}

	// "exec" in both the K2 and the Material schema.
	bool IsExecPin(const UEdGraphPin* Pin)
	{
		return Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec;
	}

	const SGraphPanel* FindGraphPanel(const UEdGraph* Graph)
	{
		if (Graph)
		{
			if (TSharedPtr<SGraphEditor> GraphEditor = SGraphEditor::FindGraphEditorForGraph(Graph))
			{
				return GraphEditor->GetGraphPanel();
			}
		}
		return nullptr;
	}

	// Pin-based height fallback used when the node's Slate widget isn't
	// available (graph panel not open, node not yet ticked, etc.).
	int32 EstimateNodeHeight(const UEdGraphNode* Node, const FBlueprintArrangeLayoutSettings& Settings)
	{
		int32 InPinCount = 0;
		int32 OutPinCount = 0;
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (!IsPinVisible(Node, Pin))
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

	// Index/count among visible pins on the same side, plus the pin centre Y.
	// The real offset comes from the pin widget, which caches it during paint,
	// so it is zero for nodes that haven't been drawn yet; fall back to an
	// estimate in that case.
	FPinLayout GetPinLayout(
		const UEdGraphNode* Node,
		const UEdGraphPin* Pin,
		const SGraphPanel* GraphPanel,
		const FBlueprintArrangeLayoutSettings& Settings)
	{
		FPinLayout Layout;
		int32 Count = 0;
		for (const UEdGraphPin* P : Node->Pins)
		{
			if (!IsPinVisible(Node, P) || P->Direction != Pin->Direction)
			{
				continue;
			}
			if (P == Pin)
			{
				Layout.SideIndex = Count;
			}
			++Count;
		}
		Layout.SideCount = FMath::Max(1, Count);
		Layout.OffsetY = Settings.FallbackTitleHeight
			+ Layout.SideIndex * Settings.FallbackPinHeight
			+ Settings.FallbackPinHeight * 0.5f;

		if (GraphPanel)
		{
			if (TSharedPtr<SGraphNode> NodeWidget = GraphPanel->GetNodeWidgetFromGuid(Node->NodeGuid))
			{
				if (TSharedPtr<SGraphPin> PinWidget = NodeWidget->FindWidgetForPin(const_cast<UEdGraphPin*>(Pin)))
				{
					const FVector2f NodeOffset = PinWidget->GetNodeOffset();
					if (NodeOffset.Y > 0.f)
					{
						Layout.OffsetY = NodeOffset.Y + static_cast<float>(PinWidget->GetDesiredSize().Y) * 0.5f;
					}
				}
			}
		}
		return Layout;
	}

	// ======================================================================
	//  Phase 1: Node sizes
	// ======================================================================

	// Build a size lookup for all nodes. Tries SGraphPanel::GetBoundsForNode
	// first (exact rendered size), then falls back to pin-count estimation.
	// Returns node index -> size.
	TArray<FNodeSize> BuildNodeSizes(
		const TArray<UEdGraphNode*>& Nodes,
		const SGraphPanel* GraphPanel,
		const FBlueprintArrangeLayoutSettings& Settings)
	{
		TArray<FNodeSize> Sizes;
		Sizes.SetNum(Nodes.Num());

		for (int32 i = 0; i < Nodes.Num(); ++i)
		{
			const UEdGraphNode* Node = Nodes[i];

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
						continue;
					}
				}
			}

			// 2. Fallback estimation.
			if (IsKnot(Node))
			{
				Sizes[i] = {Settings.FallbackKnotSize, Settings.FallbackKnotSize};
			}
			else
			{
				Sizes[i] = {Settings.FallbackDefaultWidth, EstimateNodeHeight(Node, Settings)};
			}
		}

		return Sizes;
	}

	// ======================================================================
	//  Phase 2: Edge collection
	// ======================================================================

	struct FEdgeCollection
	{
		TArray<FArrangeEdge> Edges;
		/** Selected knots reached from a real node, keyed by knot. */
		TMap<UEdGraphNode*, FKnotPlacement> Knots;
	};

	// Collect edges: output pin owner -> linked input pin owner. Reroute
	// chains (including knots outside the selection) are followed to the real
	// nodes behind them, so a knot never becomes a layout node of its own.
	FEdgeCollection BuildEdges(
		const TArray<UEdGraphNode*>& Nodes,
		const TMap<UEdGraphNode*, int32>& NodeToIndex,
		const TSet<UEdGraphNode*>& SelectedKnots,
		const SGraphPanel* GraphPanel,
		const FBlueprintArrangeLayoutSettings& Settings)
	{
		const int32 NumNodes = Nodes.Num();

		FEdgeCollection Result;

		struct FPendingLink
		{
			const UEdGraphPin* Pin;
			int32 KnotDepth;
		};
		TArray<FPendingLink> Pending;
		TSet<const UEdGraphNode*> VisitedKnots;

		for (int32 U = 0; U < NumNodes; ++U)
		{
			UEdGraphNode* NodeU = Nodes[U];
			for (const UEdGraphPin* Pin : NodeU->Pins)
			{
				if (!IsPinVisible(NodeU, Pin) || Pin->Direction != EGPD_Output || Pin->LinkedTo.IsEmpty())
				{
					continue;
				}

				const FPinLayout SourceLayout = GetPinLayout(NodeU, Pin, GraphPanel, Settings);

				Pending.Reset();
				VisitedKnots.Reset();
				for (const UEdGraphPin* LinkedTo : Pin->LinkedTo)
				{
					Pending.Add({LinkedTo, 0});
				}

				while (!Pending.IsEmpty())
				{
					const FPendingLink Link = Pending.Pop(EAllowShrinking::No);
					UEdGraphNode* TargetNode = Link.Pin ? Link.Pin->GetOwningNode() : nullptr;
					if (!TargetNode || TargetNode == NodeU)
					{
						continue;
					}

					if (IsKnot(TargetNode))
					{
						bool bAlreadyVisited = false;
						VisitedKnots.Add(TargetNode, &bAlreadyVisited);
						if (bAlreadyVisited)
						{
							continue;
						}
						if (SelectedKnots.Contains(TargetNode) && !Result.Knots.Contains(TargetNode))
						{
							Result.Knots.Add(TargetNode, {U, SourceLayout.OffsetY, Link.KnotDepth});
						}
						if (const UEdGraphPin* KnotOutput = GetKnotOutputPin(TargetNode))
						{
							for (const UEdGraphPin* LinkedTo : KnotOutput->LinkedTo)
							{
								Pending.Add({LinkedTo, Link.KnotDepth + 1});
							}
						}
						continue;
					}

					const int32* V = NodeToIndex.Find(TargetNode);
					if (!V)
					{
						continue;
					}

					FArrangeEdge& Edge = Result.Edges.AddDefaulted_GetRef();
					Edge.From = U;
					Edge.To = *V;
					Edge.SourcePin = SourceLayout;
					Edge.TargetPin = GetPinLayout(TargetNode, Link.Pin, GraphPanel, Settings);
					Edge.bIsExec = IsExecPin(Pin) && IsExecPin(Link.Pin);
					Edge.bViaKnot = Link.KnotDepth > 0;
				}
			}
		}

		return Result;
	}

	// "Empty" nodes: no incoming or outgoing edges within the selection.
	TSet<int32> FindEmptyNodes(const TArray<FArrangeEdge>& Edges, int32 NumNodes)
	{
		TBitArray<> Connected(false, NumNodes);
		for (const FArrangeEdge& E : Edges)
		{
			Connected[E.From] = true;
			Connected[E.To] = true;
		}
		TSet<int32> Empty;
		for (int32 i = 0; i < NumNodes; ++i)
		{
			if (!Connected[i])
			{
				Empty.Add(i);
			}
		}
		return Empty;
	}

	// ======================================================================
	//  Phase 2b: Feeder blocks
	// ======================================================================

	bool HasExecPins(const UEdGraphNode* Node)
	{
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (IsPinVisible(Node, Pin) && IsExecPin(Pin))
			{
				return true;
			}
		}
		return false;
	}

	// A leaf data node (getter, constant, parameter, ...) attached to the left
	// of the one node it feeds.
	struct FFeeder
	{
		int32 Node;
		/** Top of the feeder relative to the consumer's top. */
		int32 RelY = 0;
		/** Consumer input pin it lines up with; orders the stack. */
		int32 TargetSideIndex = 0;
		float SourcePinY = 0.f;
		float TargetPinY = 0.f;
	};

	// The layout sees a consumer and its feeders as one block, so their space
	// is reserved by ranking and column packing like any other node.
	struct FFeederBlocks
	{
		/** Real node index -> layout index, INDEX_NONE for feeders. */
		TArray<int32> LayoutIndexOf;
		/** Layout index -> real node index. */
		TArray<int32> RealIndexOf;
		/** Per real node: its feeders (empty unless it is a consumer). */
		TArray<TArray<FFeeder>> FeedersOf;
		/** Per layout node: block size and the consumer's offset inside it. */
		TArray<FNodeSize> BlockSizes;
		TArray<FIntPoint> ConsumerOffset;
		/** Edges between layout nodes, pin offsets relative to the block. */
		TArray<FArrangeEdge> Edges;
	};

	// A feeder has no inputs within the selection, no exec pins, and every
	// outgoing edge goes straight (no reroutes) into the same consumer.
	// Structural, so it covers Blueprint getters/literals and material
	// constants/parameters alike.
	FFeederBlocks BuildFeederBlocks(
		const TArray<FArrangeEdge>& Edges,
		const TMap<UEdGraphNode*, FKnotPlacement>& Knots,
		const TArray<UEdGraphNode*>& Nodes,
		const TArray<FNodeSize>& NodeSizes,
		const FBlueprintArrangeLayoutSettings& Settings)
	{
		const int32 NumNodes = Nodes.Num();

		TArray<int32> InCount;
		InCount.Init(0, NumNodes);
		TArray<int32> Consumer;
		Consumer.Init(INDEX_NONE, NumNodes);
		TBitArray<> Disqualified(false, NumNodes);
		// Knots are placed right after their source, which would be inside the block.
		for (const TPair<UEdGraphNode*, FKnotPlacement>& Knot : Knots)
		{
			Disqualified[Knot.Value.SourceNode] = true;
		}
		for (const FArrangeEdge& E : Edges)
		{
			++InCount[E.To];
			if (E.bIsExec || E.bViaKnot || (Consumer[E.From] != INDEX_NONE && Consumer[E.From] != E.To))
			{
				Disqualified[E.From] = true;
			}
			Consumer[E.From] = E.To;
		}

		FFeederBlocks Blocks;
		Blocks.FeedersOf.SetNum(NumNodes);
		Blocks.LayoutIndexOf.Init(INDEX_NONE, NumNodes);

		TBitArray<> IsFeeder(false, NumNodes);
		for (int32 n = 0; n < NumNodes; ++n)
		{
			// The consumer always has an input, so it can't be a feeder itself.
			IsFeeder[n] = InCount[n] == 0 && Consumer[n] != INDEX_NONE && !Disqualified[n] && !HasExecPins(Nodes[n]);
		}

		// Each feeder lines up with the topmost consumer pin it feeds.
		for (const FArrangeEdge& E : Edges)
		{
			if (!IsFeeder[E.From])
			{
				continue;
			}
			TArray<FFeeder>& Feeders = Blocks.FeedersOf[E.To];
			FFeeder* Existing = Feeders.FindByPredicate([&E](const FFeeder& F) { return F.Node == E.From; });
			if (!Existing)
			{
				Existing = &Feeders.AddDefaulted_GetRef();
				Existing->Node = E.From;
				Existing->TargetSideIndex = MAX_int32;
			}
			if (E.TargetPin.SideIndex < Existing->TargetSideIndex)
			{
				Existing->TargetSideIndex = E.TargetPin.SideIndex;
				Existing->SourcePinY = E.SourcePin.OffsetY;
				Existing->TargetPinY = E.TargetPin.OffsetY;
			}
		}

		for (int32 n = 0; n < NumNodes; ++n)
		{
			if (IsFeeder[n])
			{
				continue;
			}
			Blocks.LayoutIndexOf[n] = Blocks.RealIndexOf.Add(n);

			TArray<FFeeder>& Feeders = Blocks.FeedersOf[n];
			FNodeSize Block = NodeSizes[n];
			FIntPoint Offset(0, 0);
			if (!Feeders.IsEmpty())
			{
				Feeders.StableSort([](const FFeeder& A, const FFeeder& B) { return A.TargetSideIndex < B.TargetSideIndex; });

				// Stack top to bottom, each output pin level with its input pin
				// unless the feeder above is in the way.
				int32 Cursor = MIN_int32;
				int32 StackTop = MAX_int32;
				int32 StackBottom = MIN_int32;
				int32 MaxFeederWidth = 0;
				for (FFeeder& F : Feeders)
				{
					const FNodeSize& Size = NodeSizes[F.Node];
					F.RelY = FMath::Max(Cursor, FMath::RoundToInt(F.TargetPinY - F.SourcePinY));
					Cursor = F.RelY + Size.Height + Settings.DataRowSpacing;
					StackTop = FMath::Min(StackTop, F.RelY);
					StackBottom = FMath::Max(StackBottom, F.RelY + Size.Height);
					MaxFeederWidth = FMath::Max(MaxFeederWidth, Size.Width);
				}

				Offset.X = MaxFeederWidth + Settings.FeederSpacing;
				Offset.Y = FMath::Max(0, -StackTop);
				Block.Width = Offset.X + NodeSizes[n].Width;
				Block.Height = Offset.Y + FMath::Max(NodeSizes[n].Height, StackBottom);
			}
			Blocks.BlockSizes.Add(Block);
			Blocks.ConsumerOffset.Add(Offset);
		}

		for (const FArrangeEdge& E : Edges)
		{
			if (IsFeeder[E.From])
			{
				continue;
			}
			FArrangeEdge& LayoutEdge = Blocks.Edges.Add_GetRef(E);
			LayoutEdge.From = Blocks.LayoutIndexOf[E.From];
			LayoutEdge.To = Blocks.LayoutIndexOf[E.To];
			LayoutEdge.SourcePin.OffsetY += Blocks.ConsumerOffset[LayoutEdge.From].Y;
			LayoutEdge.TargetPin.OffsetY += Blocks.ConsumerOffset[LayoutEdge.To].Y;
		}

		return Blocks;
	}

	// ======================================================================
	//  Phase 3: Cycle breaking
	// ======================================================================

	// Iterative DFS; an edge into a node that is still on the stack closes a
	// cycle and is marked as feedback. Roots are visited sources-first, left
	// to right by the original position, so the edge that gets marked is the
	// one already drawn as the loop-back wire.
	void MarkFeedbackEdges(TArray<FArrangeEdge>& Edges, int32 NumNodes, const TArray<UEdGraphNode*>& Nodes)
	{
		TArray<TArray<int32>> OutEdges;
		OutEdges.SetNum(NumNodes);
		TArray<int32> InDegree;
		InDegree.Init(0, NumNodes);
		for (int32 EdgeIdx = 0; EdgeIdx < Edges.Num(); ++EdgeIdx)
		{
			OutEdges[Edges[EdgeIdx].From].Add(EdgeIdx);
			++InDegree[Edges[EdgeIdx].To];
		}

		TArray<int32> Roots;
		Roots.Reserve(NumNodes);
		for (int32 n = 0; n < NumNodes; ++n)
		{
			Roots.Add(n);
		}
		Roots.StableSort([&InDegree, &Nodes](int32 A, int32 B)
		{
			const bool bASource = InDegree[A] == 0;
			const bool bBSource = InDegree[B] == 0;
			if (bASource != bBSource)
			{
				return bASource;
			}
			if (Nodes[A]->NodePosX != Nodes[B]->NodePosX)
			{
				return Nodes[A]->NodePosX < Nodes[B]->NodePosX;
			}
			return Nodes[A]->NodePosY < Nodes[B]->NodePosY;
		});

		enum class EVisit : uint8 { White, Grey, Black };
		TArray<EVisit> Visit;
		Visit.Init(EVisit::White, NumNodes);

		// (node, index of the next out-edge to follow)
		TArray<TPair<int32, int32>> Stack;
		for (const int32 Root : Roots)
		{
			if (Visit[Root] != EVisit::White)
			{
				continue;
			}
			Visit[Root] = EVisit::Grey;
			Stack.Add({Root, 0});
			while (!Stack.IsEmpty())
			{
				const int32 Node = Stack.Last().Key;
				const int32 Cursor = Stack.Last().Value++;
				if (Cursor < OutEdges[Node].Num())
				{
					FArrangeEdge& Edge = Edges[OutEdges[Node][Cursor]];
					if (Visit[Edge.To] == EVisit::Grey)
					{
						Edge.bIsFeedback = true;
					}
					else if (Visit[Edge.To] == EVisit::White)
					{
						Visit[Edge.To] = EVisit::Grey;
						Stack.Add({Edge.To, 0});
					}
				}
				else
				{
					Visit[Node] = EVisit::Black;
					Stack.Pop(EAllowShrinking::No);
				}
			}
		}
	}

	// ======================================================================
	//  Phase 4: Adjacency (upstream / downstream with pin info)
	// ======================================================================

	struct FAdjacency
	{
		TArray<TArray<FNeighborLink>> Upstream;
		TArray<TArray<FNeighborLink>> Downstream;
	};

	// Feedback edges are left out, so everything after this sees a DAG.
	FAdjacency BuildAdjacency(const TArray<FArrangeEdge>& EdgeList, int32 NumNodes)
	{
		FAdjacency Adj;
		Adj.Upstream.SetNum(NumNodes);
		Adj.Downstream.SetNum(NumNodes);
		for (const FArrangeEdge& E : EdgeList)
		{
			if (E.bIsFeedback)
			{
				continue;
			}
			Adj.Downstream[E.From].Add({
				E.To, E.TargetPin.OffsetY, E.SourcePin.OffsetY,
				E.TargetPin.SideIndex, E.TargetPin.SideCount, E.bIsExec});
			Adj.Upstream[E.To].Add({
				E.From, E.SourcePin.OffsetY, E.TargetPin.OffsetY,
				E.SourcePin.SideIndex, E.SourcePin.SideCount, E.bIsExec});
		}
		return Adj;
	}

	// ======================================================================
	//  Phase 5: Ranking (longest-path + tightening)
	// ======================================================================

	struct FRanking
	{
		TArray<int32> Rank;
		int32 MaxRank;
	};

	// Forward: Rank[consumer] >= Rank[source] + 1, in topological order.
	// Tightening, in reverse topological order: pull each node as close to
	// its consumers as possible so data-input nodes land just before the node
	// that uses them instead of column 0. The consumers are already final and
	// a node never rises above min(consumer) - 1, so the source constraints
	// still hold. O(N + E).
	FRanking ComputeRanking(int32 NumNodes, const FAdjacency& Adj)
	{
		TArray<int32> InDegree;
		InDegree.SetNum(NumNodes);
		TArray<int32> TopoOrder;
		TopoOrder.Reserve(NumNodes);
		for (int32 n = 0; n < NumNodes; ++n)
		{
			InDegree[n] = Adj.Upstream[n].Num();
			if (InDegree[n] == 0)
			{
				TopoOrder.Add(n);
			}
		}
		for (int32 Head = 0; Head < TopoOrder.Num(); ++Head)
		{
			for (const FNeighborLink& Link : Adj.Downstream[TopoOrder[Head]])
			{
				if (--InDegree[Link.NeighborNode] == 0)
				{
					TopoOrder.Add(Link.NeighborNode);
				}
			}
		}
		if (!ensureMsgf(TopoOrder.Num() == NumNodes, TEXT("BlueprintArrange: graph still has a cycle after feedback edges were removed")))
		{
			for (int32 n = 0; n < NumNodes; ++n)
			{
				if (InDegree[n] > 0)
				{
					TopoOrder.Add(n);
				}
			}
		}

		TArray<int32> Rank;
		Rank.Init(0, NumNodes);

		for (const int32 n : TopoOrder)
		{
			for (const FNeighborLink& Link : Adj.Downstream[n])
			{
				Rank[Link.NeighborNode] = FMath::Max(Rank[Link.NeighborNode], Rank[n] + 1);
			}
		}

		for (int32 i = TopoOrder.Num() - 1; i >= 0; --i)
		{
			const int32 n = TopoOrder[i];
			if (Adj.Downstream[n].IsEmpty())
			{
				continue; // sink, nothing to pull toward
			}
			int32 MinConsumerRank = MAX_int32;
			for (const FNeighborLink& Link : Adj.Downstream[n])
			{
				MinConsumerRank = FMath::Min(MinConsumerRank, Rank[Link.NeighborNode]);
			}
			Rank[n] = FMath::Max(Rank[n], MinConsumerRank - 1);
		}

		int32 MaxRank = 0;
		for (const int32 r : Rank)
		{
			MaxRank = FMath::Max(MaxRank, r);
		}

		return {MoveTemp(Rank), MaxRank};
	}

	// ======================================================================
	//  Phase 6: Crossing reduction (pin-aware barycenter)
	// ======================================================================

	// Build the initial within-rank column layout from the ranking.
	// Connected nodes first (by ascending Y), empty nodes last (by ascending Y).
	TArray<TArray<int32>> BuildRanksFromRanking(
		const TArray<int32>& Rank,
		int32 MaxRank,
		int32 NumNodes,
		const TSet<int32>& EmptyNodeIndices,
		const TArray<UEdGraphNode*>& Nodes)
	{
		TArray<TArray<int32>> Ranks;
		Ranks.SetNum(MaxRank + 1);
		for (int32 i = 0; i < NumNodes; ++i)
		{
			Ranks[Rank[i]].Add(i);
		}
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
		return Ranks;
	}

	// Barycenter crossing reduction, alternating down/up sweeps. Exec edges
	// weighted higher. The neighbour's pin adds a fraction in [0, 1) so pin
	// order breaks ties within a neighbour's slot but never outweighs the
	// neighbours' own order. Empty nodes are excluded from reordering (stay
	// at end of rank).
	void ReduceCrossings(
		TArray<TArray<int32>>& Ranks,
		int32 MaxRank,
		int32 NumNodes,
		const TSet<int32>& EmptyNodeIndices,
		const FAdjacency& Adj)
	{
		TArray<int32> OrderOf;
		OrderOf.SetNum(NumNodes);
		auto RebuildOrders = [&Ranks, &OrderOf](int32 r)
		{
			for (int32 Pos = 0; Pos < Ranks[r].Num(); ++Pos)
			{
				OrderOf[Ranks[r][Pos]] = Pos;
			}
		};
		for (int32 r = 0; r <= MaxRank; ++r)
		{
			RebuildOrders(r);
		}

		constexpr int32 NumSweeps = 4;
		for (int32 Sweep = 0; Sweep < NumSweeps; ++Sweep)
		{
			const bool bDownward = (Sweep % 2) == 0;
			for (int32 ri = 0; ri <= MaxRank; ++ri)
			{
				const int32 r = bDownward ? ri : MaxRank - ri;

				TArray<TPair<float, int32>> Keys;
				Keys.Reserve(Ranks[r].Num());
				for (const int32 n : Ranks[r])
				{
					const TArray<FNeighborLink>& Neighbors = bDownward ? Adj.Upstream[n] : Adj.Downstream[n];
					// Empty nodes and nodes without neighbours keep their position.
					if (EmptyNodeIndices.Contains(n) || Neighbors.IsEmpty())
					{
						Keys.Add({static_cast<float>(OrderOf[n]), n});
						continue;
					}
					float WeightSum = 0.f;
					float WeightedOrder = 0.f;
					for (const FNeighborLink& Link : Neighbors)
					{
						const float W = PinWeight(Link.bExec);
						const float PinFraction = static_cast<float>(Link.NeighborSideIndex) / FMath::Max(1, Link.NeighborSideCount);
						WeightedOrder += W * (static_cast<float>(OrderOf[Link.NeighborNode]) + PinFraction);
						WeightSum += W;
					}
					Keys.Add({WeightedOrder / WeightSum, n});
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

				for (int32 Pos = 0; Pos < Keys.Num(); ++Pos)
				{
					Ranks[r][Pos] = Keys[Pos].Value;
				}
				RebuildOrders(r);
			}
		}
	}

	// ======================================================================
	//  Phase 7: Coordinate assignment
	// ======================================================================

	struct FCoordinates
	{
		TArray<int32> NewX;
		TArray<int32> NewY;
	};

	// Barycenter coordinate assignment. Alternates down (rank 0 -> MaxRank,
	// pulling each column toward its upstream neighbours) and up (MaxRank -> 0,
	// pulling toward downstream neighbours) sweeps. On every sweep a column's
	// nodes first get a desired Y that lines their pin up with the neighbour's
	// pin (weighted average over all links), then the column is packed
	// top-to-bottom preserving the crossing-reduction order while following
	// that desired Y (never overlapping). Each column is as wide as its widest
	// node and nodes are right-aligned within it. Two data-only nodes stacked
	// directly on top of each other use the smaller DataRowSpacing. Empty
	// nodes are excluded from the relaxation.
	FCoordinates AssignCoordinates(
		const TArray<TArray<int32>>& Ranks,
		int32 MaxRank,
		const TArray<FNodeSize>& NodeSizes,
		const TBitArray<>& IsDataNode,
		const TSet<int32>& EmptyNodeIndices,
		const FAdjacency& Adj,
		const FBlueprintArrangeLayoutSettings& Settings)
	{
		const int32 NumNodes = NodeSizes.Num();

		FCoordinates Coords;
		Coords.NewX.Init(0, NumNodes);
		Coords.NewY.Init(0, NumNodes);

		TArray<float> DesiredY;
		DesiredY.Init(0.f, NumNodes);

		TArray<int32> ColumnWidth;
		ColumnWidth.Init(0, MaxRank + 1);
		for (int32 r = 0; r <= MaxRank; ++r)
		{
			for (const int32 n : Ranks[r])
			{
				ColumnWidth[r] = FMath::Max(ColumnWidth[r], NodeSizes[n].Width);
			}
		}
		TArray<int32> ColumnX;
		ColumnX.Init(0, MaxRank + 1);
		for (int32 r = 1; r <= MaxRank; ++r)
		{
			ColumnX[r] = ColumnX[r - 1] + ColumnWidth[r - 1] + Settings.ColumnSpacing;
		}
		for (int32 r = 0; r <= MaxRank; ++r)
		{
			for (const int32 n : Ranks[r])
			{
				Coords.NewX[n] = ColumnX[r] + ColumnWidth[r] - NodeSizes[n].Width;
			}
		}

		constexpr int32 NumCoordSweeps = 8;
		for (int32 Sweep = 0; Sweep < NumCoordSweeps; ++Sweep)
		{
			const bool bDownward = (Sweep % 2) == 0;
			for (int32 ri = 0; ri <= MaxRank; ++ri)
			{
				const int32 r = bDownward ? ri : MaxRank - ri;

				// Desired Y per node from the already-placed opposite column(s).
				for (const int32 n : Ranks[r])
				{
					const TArray<FNeighborLink>& Neighbors = bDownward ? Adj.Upstream[n] : Adj.Downstream[n];
					if (EmptyNodeIndices.Contains(n) || Neighbors.IsEmpty())
					{
						DesiredY[n] = static_cast<float>(Coords.NewY[n]);
						continue;
					}
					float WeightSum = 0.f;
					float WeightedY = 0.f;
					for (const FNeighborLink& Link : Neighbors)
					{
						// A straight wire needs both pin centres at the same Y.
						const float W = PinWeight(Link.bExec);
						WeightedY += W * (Coords.NewY[Link.NeighborNode] + Link.NeighborPinY - Link.SelfPinY);
						WeightSum += W;
					}
					DesiredY[n] = WeightedY / WeightSum;
				}

				// Pack this column preserving order while following the desired Y.
				// Not clamped to 0: a node may need to sit above its neighbour.
				// Connected nodes and empty nodes (separate band) each keep a
				// running bottom edge and the node that produced it.
				int32 Bottom = MIN_int32, Above = INDEX_NONE;
				int32 EmptyBottom = 0, EmptyAbove = INDEX_NONE;
				auto MinTop = [&IsDataNode, &Settings](int32 PrevBottom, int32 Prev, int32 n) -> int32
				{
					if (Prev == INDEX_NONE)
					{
						return PrevBottom;
					}
					const bool bCompact = IsDataNode[Prev] && IsDataNode[n];
					return PrevBottom + (bCompact ? Settings.DataRowSpacing : Settings.RowSpacing);
				};
				for (const int32 n : Ranks[r])
				{
					const int32 NodeH = NodeSizes[n].Height;
					if (EmptyNodeIndices.Contains(n))
					{
						Coords.NewY[n] = MinTop(EmptyBottom, EmptyAbove, n);
						EmptyBottom = Coords.NewY[n] + NodeH;
						EmptyAbove = n;
					}
					else
					{
						Coords.NewY[n] = FMath::Max(MinTop(Bottom, Above, n), FMath::RoundToInt(DesiredY[n]));
						Bottom = Coords.NewY[n] + NodeH;
						Above = n;
					}
				}
			}
		}

		// Re-base connected nodes so the topmost one sits at Y = 0.
		int32 MinConnectedY = MAX_int32;
		for (int32 n = 0; n < NumNodes; ++n)
		{
			if (!EmptyNodeIndices.Contains(n))
			{
				MinConnectedY = FMath::Min(MinConnectedY, Coords.NewY[n]);
			}
		}
		if (MinConnectedY != MAX_int32)
		{
			for (int32 n = 0; n < NumNodes; ++n)
			{
				if (!EmptyNodeIndices.Contains(n))
				{
					Coords.NewY[n] -= MinConnectedY;
				}
			}
		}

		return Coords;
	}

	// Drop the empty band below the tallest connected column so empty nodes
	// never overlap connected ones.
	void PlaceEmptyNodes(
		const TArray<FNodeSize>& NodeSizes,
		const TSet<int32>& EmptyNodeIndices,
		TArray<int32>& NewY,
		const FBlueprintArrangeLayoutSettings& Settings)
	{
		int32 ConnectedBottom = 0;
		for (int32 i = 0; i < NodeSizes.Num(); ++i)
		{
			if (!EmptyNodeIndices.Contains(i))
			{
				ConnectedBottom = FMath::Max(ConnectedBottom, NewY[i] + NodeSizes[i].Height + Settings.RowSpacing);
			}
		}
		for (const int32 i : EmptyNodeIndices)
		{
			NewY[i] += ConnectedBottom;
		}
	}

	// ======================================================================
	//  Phase 8: Apply final positions
	// ======================================================================

	// Snap to 16-unit grid, center on the nodes' original bounding box,
	// and write back to NodePosX/NodePosY. Returns number of moved nodes.
	int32 ApplyPositions(
		const TArray<UEdGraphNode*>& Nodes,
		const TArray<int32>& NewX,
		const TArray<int32>& NewY)
	{
		const int32 NumNodes = Nodes.Num();

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

		const int32 OffsetX = SnapToGrid(Center(OldMinX, OldMaxX) - Center(NewMinX, NewMaxX));
		const int32 OffsetY = SnapToGrid(Center(OldMinY, OldMaxY) - Center(NewMinY, NewMaxY));

		int32 NumMoved = 0;
		for (int32 i = 0; i < NumNodes; ++i)
		{
			const int32 FinalX = SnapToGrid(NewX[i] + OffsetX);
			const int32 FinalY = SnapToGrid(NewY[i] + OffsetY);
			if (Nodes[i]->NodePosX != FinalX || Nodes[i]->NodePosY != FinalY)
			{
				++NumMoved;
			}
			Nodes[i]->NodePosX = FinalX;
			Nodes[i]->NodePosY = FinalY;
		}

		return NumMoved;
	}

	// Place selected knots in the gap after their source node, level with the
	// source pin. X is grid-snapped; Y is not, so the wire stays straight.
	int32 PlaceKnots(
		const TMap<UEdGraphNode*, FKnotPlacement>& Knots,
		const TArray<UEdGraphNode*>& Nodes,
		const TArray<FNodeSize>& NodeSizes,
		const TMap<UEdGraphNode*, FNodeSize>& KnotSizes,
		const FBlueprintArrangeLayoutSettings& Settings)
	{
		int32 NumMoved = 0;
		for (const TPair<UEdGraphNode*, FKnotPlacement>& Pair : Knots)
		{
			UEdGraphNode* Knot = Pair.Key;
			const FKnotPlacement& Placement = Pair.Value;
			const UEdGraphNode* Source = Nodes[Placement.SourceNode];
			const FNodeSize& KnotSize = KnotSizes.FindChecked(Knot);

			const int32 FinalX = SnapToGrid(
				Source->NodePosX + NodeSizes[Placement.SourceNode].Width
				+ Settings.ColumnSpacing / 2 + GridSize * Placement.Depth);
			const int32 FinalY = FMath::RoundToInt(Source->NodePosY + Placement.SourcePinY - KnotSize.Height * 0.5f);

			if (Knot->NodePosX != FinalX || Knot->NodePosY != FinalY)
			{
				++NumMoved;
			}
			Knot->NodePosX = FinalX;
			Knot->NodePosY = FinalY;
		}
		return NumMoved;
	}

	// Place feeders in the left part of their consumer's block, right-aligned
	// against the consumer. X is grid-snapped; Y is not, so the wire stays
	// straight.
	int32 PlaceFeeders(
		const FFeederBlocks& Blocks,
		const TArray<UEdGraphNode*>& Nodes,
		const TArray<FNodeSize>& NodeSizes,
		const FBlueprintArrangeLayoutSettings& Settings)
	{
		int32 NumMoved = 0;
		for (int32 c = 0; c < Nodes.Num(); ++c)
		{
			const UEdGraphNode* Consumer = Nodes[c];
			for (const FFeeder& F : Blocks.FeedersOf[c])
			{
				UEdGraphNode* Feeder = Nodes[F.Node];
				const int32 FinalX = SnapToGrid(Consumer->NodePosX - Settings.FeederSpacing - NodeSizes[F.Node].Width);
				const int32 FinalY = Consumer->NodePosY + F.RelY;
				if (Feeder->NodePosX != FinalX || Feeder->NodePosY != FinalY)
				{
					++NumMoved;
				}
				Feeder->NodePosX = FinalX;
				Feeder->NodePosY = FinalY;
			}
		}
		return NumMoved;
	}
} // namespace

// ==========================================================================
//  Public entry point — thin orchestrator
// ==========================================================================

int32 ArrangeNodes(const TArray<UEdGraphNode*>& Nodes, const FBlueprintArrangeLayoutSettings& Settings)
{
	// 1. Split into real nodes (laid out) and knots (placed afterwards).
	TArray<UEdGraphNode*> RealNodes;
	TArray<UEdGraphNode*> KnotNodes;
	TSet<UEdGraphNode*> SelectedKnots;
	for (UEdGraphNode* Node : Nodes)
	{
		check(Node);
		if (IsKnot(Node))
		{
			KnotNodes.Add(Node);
			SelectedKnots.Add(Node);
		}
		else
		{
			RealNodes.Add(Node);
		}
	}

	const int32 NumNodes = RealNodes.Num();
	if (NumNodes <= 1)
	{
		return 0;
	}

	TMap<UEdGraphNode*, int32> NodeToIndex;
	NodeToIndex.Reserve(NumNodes);
	for (int32 i = 0; i < NumNodes; ++i)
	{
		NodeToIndex.Add(RealNodes[i], i);
	}

	// 2. Resolve real node sizes from the Slate widgets (with fallback).
	const SGraphPanel* GraphPanel = FindGraphPanel(RealNodes[0]->GetGraph());
	const TArray<FNodeSize> NodeSizes = BuildNodeSizes(RealNodes, GraphPanel, Settings);

	// 3. Collect edges (through reroute chains).
	const FEdgeCollection EdgeCol = BuildEdges(RealNodes, NodeToIndex, SelectedKnots, GraphPanel, Settings);

	// 4. Fold leaf data nodes into blocks with their consumer. Everything up
	//    to step 9 works on these blocks ("layout nodes").
	FFeederBlocks Blocks = BuildFeederBlocks(EdgeCol.Edges, EdgeCol.Knots, RealNodes, NodeSizes, Settings);
	const int32 NumLayout = Blocks.RealIndexOf.Num();
	TArray<UEdGraphNode*> LayoutNodes;
	TBitArray<> IsDataNode(false, NumLayout);
	for (int32 l = 0; l < NumLayout; ++l)
	{
		LayoutNodes.Add(RealNodes[Blocks.RealIndexOf[l]]);
		IsDataNode[l] = !HasExecPins(LayoutNodes[l]);
	}
	const TSet<int32> EmptyNodeIndices = FindEmptyNodes(Blocks.Edges, NumLayout);

	// 5. Break cycles, then build the DAG adjacency with pin info.
	MarkFeedbackEdges(Blocks.Edges, NumLayout, LayoutNodes);
	const FAdjacency Adj = BuildAdjacency(Blocks.Edges, NumLayout);

	// 6. Ranking: longest-path forward + reverse tightening.
	const FRanking Ranking = ComputeRanking(NumLayout, Adj);

	// 7. Crossing reduction: pin-aware barycenter sweeps.
	TArray<TArray<int32>> Ranks = BuildRanksFromRanking(
		Ranking.Rank, Ranking.MaxRank, NumLayout, EmptyNodeIndices, LayoutNodes);
	ReduceCrossings(Ranks, Ranking.MaxRank, NumLayout, EmptyNodeIndices, Adj);

	// 8. Coordinate assignment: pin-aligned Y + per-column right-aligned X.
	FCoordinates Coords = AssignCoordinates(
		Ranks, Ranking.MaxRank, Blocks.BlockSizes, IsDataNode, EmptyNodeIndices, Adj, Settings);

	// 9. Drop empty nodes into a separate band below connected ones.
	PlaceEmptyNodes(Blocks.BlockSizes, EmptyNodeIndices, Coords.NewY, Settings);

	// 10. Block position -> consumer position, then snap to grid, center on
	//     the original bounding box and write back. Feeders follow their consumer.
	for (int32 l = 0; l < NumLayout; ++l)
	{
		Coords.NewX[l] += Blocks.ConsumerOffset[l].X;
		Coords.NewY[l] += Blocks.ConsumerOffset[l].Y;
	}
	int32 NumMoved = ApplyPositions(LayoutNodes, Coords.NewX, Coords.NewY);
	NumMoved += PlaceFeeders(Blocks, RealNodes, NodeSizes, Settings);

	// 11. Put selected knots next to the node that feeds them.
	const TArray<FNodeSize> KnotSizeList = BuildNodeSizes(KnotNodes, GraphPanel, Settings);
	TMap<UEdGraphNode*, FNodeSize> KnotSizes;
	for (int32 i = 0; i < KnotNodes.Num(); ++i)
	{
		KnotSizes.Add(KnotNodes[i], KnotSizeList[i]);
	}
	NumMoved += PlaceKnots(EdgeCol.Knots, RealNodes, NodeSizes, KnotSizes, Settings);

	return NumMoved;
}

// ==========================================================================
//  Comment boxes
// ==========================================================================

namespace
{
	FIntPoint CommentSize(const UEdGraphNode* Comment)
	{
		return {Comment->NodeWidth, Comment->NodeHeight};
	}
}

TArray<FCommentFrame> CaptureCommentFrames(const UEdGraph* Graph, const FBlueprintArrangeLayoutSettings& Settings)
{
	TArray<FCommentFrame> Frames;
	if (!Graph)
	{
		return Frames;
	}

	TArray<UEdGraphNode*> Candidates;
	TArray<UEdGraphNode_Comment*> Comments;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node)
		{
			continue;
		}
		Candidates.Add(Node);
		if (UEdGraphNode_Comment* Comment = Cast<UEdGraphNode_Comment>(Node))
		{
			Comments.Add(Comment);
		}
	}
	if (Comments.IsEmpty())
	{
		return Frames;
	}

	// Comments store their own size; everything else uses the widget size.
	const TArray<FNodeSize> NodeSizes = BuildNodeSizes(Candidates, FindGraphPanel(Graph), Settings);
	TArray<FIntPoint> Sizes;
	Sizes.SetNum(Candidates.Num());
	for (int32 i = 0; i < Candidates.Num(); ++i)
	{
		Sizes[i] = Candidates[i]->IsA<UEdGraphNode_Comment>()
			? CommentSize(Candidates[i])
			: FIntPoint(NodeSizes[i].Width, NodeSizes[i].Height);
	}

	for (UEdGraphNode_Comment* Comment : Comments)
	{
		const FSlateRect CommentRect(
			Comment->NodePosX, Comment->NodePosY,
			Comment->NodePosX + Comment->NodeWidth, Comment->NodePosY + Comment->NodeHeight);

		FCommentFrame Frame;
		Frame.Comment = Comment;
		for (int32 i = 0; i < Candidates.Num(); ++i)
		{
			UEdGraphNode* Node = Candidates[i];
			if (Node == Comment)
			{
				continue;
			}
			// Same rule the editor uses to decide what moves with a comment.
			const FSlateRect NodeRect(
				Node->NodePosX, Node->NodePosY,
				Node->NodePosX + Sizes[i].X, Node->NodePosY + Sizes[i].Y);
			if (FSlateRect::IsRectangleContained(CommentRect, NodeRect))
			{
				Frame.Contents.Add(Node);
				Frame.OriginalPositions.Emplace(Node->NodePosX, Node->NodePosY);
				Frame.Sizes.Add(Sizes[i]);
			}
		}
		if (!Frame.Contents.IsEmpty())
		{
			Frames.Add(MoveTemp(Frame));
		}
	}

	// Smallest first: nested comments are refitted before their parents.
	Frames.StableSort([](const FCommentFrame& A, const FCommentFrame& B)
	{
		return static_cast<int64>(A.Comment->NodeWidth) * A.Comment->NodeHeight
			< static_cast<int64>(B.Comment->NodeWidth) * B.Comment->NodeHeight;
	});

	return Frames;
}

int32 RefitCommentFrames(const TArray<FCommentFrame>& Frames, const FBlueprintArrangeLayoutSettings& Settings)
{
	int32 NumChanged = 0;
	for (const FCommentFrame& Frame : Frames)
	{
		bool bAnyMoved = false;
		for (int32 i = 0; i < Frame.Contents.Num() && !bAnyMoved; ++i)
		{
			const UEdGraphNode* Node = Frame.Contents[i];
			bAnyMoved = FIntPoint(Node->NodePosX, Node->NodePosY) != Frame.OriginalPositions[i]
				|| (Node->IsA<UEdGraphNode_Comment>() && CommentSize(Node) != Frame.Sizes[i]);
		}
		if (!bAnyMoved)
		{
			continue;
		}

		FIntPoint Min(MAX_int32, MAX_int32);
		FIntPoint Max(MIN_int32, MIN_int32);
		for (int32 i = 0; i < Frame.Contents.Num(); ++i)
		{
			const UEdGraphNode* Node = Frame.Contents[i];
			const FIntPoint Size = Node->IsA<UEdGraphNode_Comment>() ? CommentSize(Node) : Frame.Sizes[i];
			Min = Min.ComponentMin({Node->NodePosX, Node->NodePosY});
			Max = Max.ComponentMax({Node->NodePosX + Size.X, Node->NodePosY + Size.Y});
		}

		const float Padding = static_cast<float>(Settings.CommentPadding);
		Frame.Comment->Modify();
		Frame.Comment->SetBounds(FSlateRect(Min.X - Padding, Min.Y - Padding, Max.X + Padding, Max.Y + Padding));
		++NumChanged;
	}
	return NumChanged;
}
