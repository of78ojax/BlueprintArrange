// Copyright (c) Blueprint Arrange contributors. Licensed under the MIT License.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "GraphArranger.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_Knot.h"
#include "EdGraphNode_Comment.h"
#include "UObject/Package.h"

// No graph panel is open during tests, so the arranger uses its fallback
// sizes and the results are deterministic.
namespace BlueprintArrangeTests
{
	const FBlueprintArrangeLayoutSettings Settings;
	const int32 ColumnPitch = Settings.FallbackDefaultWidth + Settings.ColumnSpacing;
	// Two independently grid-snapped positions can each move by half a cell.
	constexpr int32 GridSizeTolerance = 16;

	struct FTestGraph
	{
		UEdGraph* Graph = nullptr;
		TArray<UEdGraphNode*> Nodes;

		FTestGraph()
		{
			Graph = NewObject<UEdGraph>(GetTransientPackage(), NAME_None, RF_Transient);
			Graph->Schema = UEdGraphSchema_K2::StaticClass();
		}

		UEdGraphNode* AddNode(int32 X, int32 Y, int32 NumExecIn = 1, int32 NumExecOut = 1, int32 NumDataIn = 0, int32 NumDataOut = 0)
		{
			UEdGraphNode* Node = NewObject<UEdGraphNode>(Graph, NAME_None, RF_Transient);
			Node->NodePosX = X;
			Node->NodePosY = Y;
			for (int32 i = 0; i < NumExecIn; ++i)
			{
				Node->CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Exec, *FString::Printf(TEXT("ExecIn%d"), i));
			}
			for (int32 i = 0; i < NumDataIn; ++i)
			{
				Node->CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Float, *FString::Printf(TEXT("DataIn%d"), i));
			}
			for (int32 i = 0; i < NumExecOut; ++i)
			{
				Node->CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, *FString::Printf(TEXT("ExecOut%d"), i));
			}
			for (int32 i = 0; i < NumDataOut; ++i)
			{
				Node->CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Float, *FString::Printf(TEXT("DataOut%d"), i));
			}
			Graph->AddNode(Node, false, false);
			Nodes.Add(Node);
			return Node;
		}

		UEdGraphNode* AddKnot(int32 X, int32 Y)
		{
			UK2Node_Knot* Knot = NewObject<UK2Node_Knot>(Graph, NAME_None, RF_Transient);
			Knot->NodePosX = X;
			Knot->NodePosY = Y;
			Knot->AllocateDefaultPins();
			Graph->AddNode(Knot, false, false);
			Nodes.Add(Knot);
			return Knot;
		}

		static UEdGraphPin* PinAt(UEdGraphNode* Node, EEdGraphPinDirection Dir, int32 Index)
		{
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin->Direction == Dir && Index-- == 0)
				{
					return Pin;
				}
			}
			return nullptr;
		}

		static void Link(UEdGraphNode* From, int32 OutIndex, UEdGraphNode* To, int32 InIndex)
		{
			PinAt(From, EGPD_Output, OutIndex)->MakeLinkTo(PinAt(To, EGPD_Input, InIndex));
		}

		int32 MinX() const
		{
			int32 Result = MAX_int32;
			for (const UEdGraphNode* Node : Nodes)
			{
				Result = FMath::Min(Result, Node->NodePosX);
			}
			return Result;
		}

		// Column index for fallback-sized nodes (all columns equally wide).
		int32 ColumnOf(const UEdGraphNode* Node) const
		{
			return FMath::RoundToInt(static_cast<float>(Node->NodePosX - MinX()) / ColumnPitch);
		}

		// Mirrors the arranger's fallback height estimate.
		static int32 FallbackHeight(const UEdGraphNode* Node)
		{
			int32 In = 0;
			int32 Out = 0;
			for (const UEdGraphPin* Pin : Node->Pins)
			{
				(Pin->Direction == EGPD_Input ? In : Out)++;
			}
			return FMath::Clamp(
				Settings.FallbackBaseHeight + Settings.FallbackPinHeight * FMath::Max(In, Out),
				Settings.FallbackMinHeight,
				Settings.FallbackMaxHeight);
		}

		bool AnyOverlap() const
		{
			for (int32 A = 0; A < Nodes.Num(); ++A)
			{
				for (int32 B = A + 1; B < Nodes.Num(); ++B)
				{
					const UEdGraphNode* NA = Nodes[A];
					const UEdGraphNode* NB = Nodes[B];
					const bool bOverlapX = NA->NodePosX < NB->NodePosX + Settings.FallbackDefaultWidth
						&& NB->NodePosX < NA->NodePosX + Settings.FallbackDefaultWidth;
					const bool bOverlapY = NA->NodePosY < NB->NodePosY + FallbackHeight(NB)
						&& NB->NodePosY < NA->NodePosY + FallbackHeight(NA);
					if (bOverlapX && bOverlapY)
					{
						return true;
					}
				}
			}
			return false;
		}

		TArray<FIntPoint> Positions() const
		{
			TArray<FIntPoint> Result;
			for (const UEdGraphNode* Node : Nodes)
			{
				Result.Emplace(Node->NodePosX, Node->NodePosY);
			}
			return Result;
		}
	};
}

using namespace BlueprintArrangeTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBlueprintArrangeLinearChainTest, "BlueprintArrange.LinearChain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBlueprintArrangeLinearChainTest::RunTest(const FString& Parameters)
{
	FTestGraph G;
	// Scrambled start positions; chain order is by index.
	const int32 StartX[] = {900, 0, 1500, 300, 600};
	for (int32 i = 0; i < 5; ++i)
	{
		G.AddNode(StartX[i], i * 37);
	}
	for (int32 i = 0; i < 4; ++i)
	{
		FTestGraph::Link(G.Nodes[i], 0, G.Nodes[i + 1], 0);
	}

	ArrangeNodes(G.Nodes, Settings);

	for (int32 i = 0; i < 5; ++i)
	{
		TestEqual(FString::Printf(TEXT("Column of node %d"), i), G.ColumnOf(G.Nodes[i]), i);
	}
	// Single-pin exec chain: every wire is straight.
	for (int32 i = 1; i < 5; ++i)
	{
		TestEqual(FString::Printf(TEXT("Y of node %d"), i), G.Nodes[i]->NodePosY, G.Nodes[0]->NodePosY);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBlueprintArrangeLoopTest, "BlueprintArrange.ExecLoop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBlueprintArrangeLoopTest::RunTest(const FString& Parameters)
{
	FTestGraph G;
	UEdGraphNode* A = G.AddNode(0, 0);
	UEdGraphNode* B = G.AddNode(300, 0);
	UEdGraphNode* C = G.AddNode(600, 0);
	FTestGraph::Link(A, 0, B, 0);
	FTestGraph::Link(B, 0, C, 0);
	FTestGraph::Link(C, 0, A, 0);

	ArrangeNodes(G.Nodes, Settings);

	TestEqual(TEXT("Column of A"), G.ColumnOf(A), 0);
	TestEqual(TEXT("Column of B"), G.ColumnOf(B), 1);
	TestEqual(TEXT("Column of C"), G.ColumnOf(C), 2);
	TestFalse(TEXT("Nodes overlap"), G.AnyOverlap());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBlueprintArrangeTighteningTest, "BlueprintArrange.DataSourceTightening",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBlueprintArrangeTighteningTest::RunTest(const FString& Parameters)
{
	FTestGraph G;
	UEdGraphNode* E0 = G.AddNode(0, 0);
	UEdGraphNode* E1 = G.AddNode(300, 0);
	UEdGraphNode* E2 = G.AddNode(600, 0, 1, 1, 1, 0);
	UEdGraphNode* E3 = G.AddNode(900, 0, 1, 1, 1, 0);
	// Two consumers, so this is a normal layout node, not a feeder.
	UEdGraphNode* Data = G.AddNode(0, 300, 0, 0, 0, 1);
	FTestGraph::Link(E0, 0, E1, 0);
	FTestGraph::Link(E1, 0, E2, 0);
	FTestGraph::Link(E2, 0, E3, 0);
	FTestGraph::Link(Data, 0, E2, 1);
	FTestGraph::Link(Data, 0, E3, 1);

	ArrangeNodes(G.Nodes, Settings);

	TestEqual(TEXT("Column of E2"), G.ColumnOf(E2), 2);
	TestEqual(TEXT("Data source sits right before its first consumer"), G.ColumnOf(Data), 1);
	TestFalse(TEXT("Nodes overlap"), G.AnyOverlap());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBlueprintArrangeRandomDagTest, "BlueprintArrange.RandomDagNoOverlap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBlueprintArrangeRandomDagTest::RunTest(const FString& Parameters)
{
	FRandomStream Random(1234);
	FTestGraph G;
	constexpr int32 NumNodes = 50;
	constexpr int32 NumDataPins = 3;
	for (int32 i = 0; i < NumNodes; ++i)
	{
		G.AddNode(Random.RandRange(-2000, 2000), Random.RandRange(-2000, 2000), 1, 1, NumDataPins, NumDataPins);
	}
	// Edges only go from lower to higher index, so the graph is a DAG.
	for (int32 i = 0; i < NumNodes * 2; ++i)
	{
		const int32 From = Random.RandRange(0, NumNodes - 2);
		const int32 To = Random.RandRange(From + 1, NumNodes - 1);
		const int32 Pin = Random.RandRange(0, NumDataPins - 1);
		FTestGraph::Link(G.Nodes[From], 1 + Pin, G.Nodes[To], 1 + Random.RandRange(0, NumDataPins - 1));
	}

	ArrangeNodes(G.Nodes, Settings);

	TestFalse(TEXT("Nodes overlap"), G.AnyOverlap());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBlueprintArrangeIdempotentTest, "BlueprintArrange.Idempotent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBlueprintArrangeIdempotentTest::RunTest(const FString& Parameters)
{
	FRandomStream Random(42);
	FTestGraph G;
	constexpr int32 NumNodes = 20;
	for (int32 i = 0; i < NumNodes; ++i)
	{
		G.AddNode(Random.RandRange(-1000, 1000), Random.RandRange(-1000, 1000), 1, 1, 2, 2);
	}
	for (int32 i = 0; i < NumNodes - 1; ++i)
	{
		FTestGraph::Link(G.Nodes[i], 0, G.Nodes[i + 1], 0);
		const int32 To = Random.RandRange(i + 1, NumNodes - 1);
		FTestGraph::Link(G.Nodes[i], 1, G.Nodes[To], 1 + Random.RandRange(0, 1));
	}

	ArrangeNodes(G.Nodes, Settings);
	const TArray<FIntPoint> First = G.Positions();
	const int32 NumMovedSecond = ArrangeNodes(G.Nodes, Settings);

	TestEqual(TEXT("Nodes moved by second arrange"), NumMovedSecond, 0);
	TestTrue(TEXT("Positions unchanged"), First == G.Positions());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBlueprintArrangeKnotTest, "BlueprintArrange.KnotChain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBlueprintArrangeKnotTest::RunTest(const FString& Parameters)
{
	FTestGraph G;
	UEdGraphNode* A = G.AddNode(0, 0);
	UEdGraphNode* K1 = G.AddKnot(400, 0);
	UEdGraphNode* K2 = G.AddKnot(800, 0);
	UEdGraphNode* B = G.AddNode(1200, 0);
	FTestGraph::Link(A, 0, K1, 0);
	FTestGraph::Link(K1, 0, K2, 0);
	FTestGraph::Link(K2, 0, B, 0);

	ArrangeNodes(G.Nodes, Settings);

	// Grid snapping moves each node by at most half a grid cell.
	TestTrue(TEXT("B is one column after A"), FMath::Abs(B->NodePosX - A->NodePosX - ColumnPitch) <= 8);
	for (const UEdGraphNode* Knot : {K1, K2})
	{
		TestTrue(TEXT("Knot sits in the gap after A"),
			Knot->NodePosX >= A->NodePosX + Settings.FallbackDefaultWidth && Knot->NodePosX < B->NodePosX);
	}
	TestTrue(TEXT("Knots are ordered along the chain"), K1->NodePosX < K2->NodePosX);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBlueprintArrangeFeederTest, "BlueprintArrange.FeederBlock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBlueprintArrangeFeederTest::RunTest(const FString& Parameters)
{
	FTestGraph G;
	UEdGraphNode* A = G.AddNode(0, 0);
	UEdGraphNode* C = G.AddNode(300, 0, 1, 1, 3, 0);
	UEdGraphNode* B = G.AddNode(600, 0);
	FTestGraph::Link(A, 0, C, 0);
	FTestGraph::Link(C, 0, B, 0);
	TArray<UEdGraphNode*> Getters;
	for (int32 i = 0; i < 3; ++i)
	{
		// Getter-like: one data output, nothing else.
		Getters.Add(G.AddNode(-500, 400 * i, 0, 0, 0, 1));
		FTestGraph::Link(Getters[i], 0, C, 1 + i);
	}

	ArrangeNodes(G.Nodes, Settings);

	TestFalse(TEXT("Nodes overlap"), G.AnyOverlap());
	TestEqual(TEXT("Exec chain stays straight (A)"), A->NodePosY, C->NodePosY);
	TestEqual(TEXT("Exec chain stays straight (B)"), B->NodePosY, C->NodePosY);
	for (int32 i = 0; i < 3; ++i)
	{
		const int32 Gap = C->NodePosX - (Getters[i]->NodePosX + Settings.FallbackDefaultWidth);
		TestTrue(FString::Printf(TEXT("Getter %d sits right before C (gap %d)"), i, Gap),
			FMath::Abs(Gap - Settings.FeederSpacing) <= 8);
		TestTrue(FString::Printf(TEXT("Getter %d is right of A"), i),
			Getters[i]->NodePosX >= A->NodePosX + Settings.FallbackDefaultWidth);
	}
	// Fallback pin centres: getter output at title + half a row, C's first
	// data input one row lower (below the exec input).
	TestEqual(TEXT("First getter lines up with its pin"), Getters[0]->NodePosY, C->NodePosY + Settings.FallbackPinHeight);
	const int32 GetterH = FTestGraph::FallbackHeight(Getters[0]);
	for (int32 i = 1; i < 3; ++i)
	{
		TestEqual(FString::Printf(TEXT("Getter %d stacks tightly"), i),
			Getters[i]->NodePosY, Getters[i - 1]->NodePosY + GetterH + Settings.DataRowSpacing);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBlueprintArrangeDataSpacingTest, "BlueprintArrange.DataRowSpacing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBlueprintArrangeDataSpacingTest::RunTest(const FString& Parameters)
{
	FTestGraph G;
	// S feeds two pure nodes, so none of them is a feeder.
	UEdGraphNode* S = G.AddNode(0, 0, 0, 0, 0, 1);
	UEdGraphNode* P1 = G.AddNode(300, 0, 0, 0, 1, 1);
	UEdGraphNode* P2 = G.AddNode(300, 300, 0, 0, 1, 1);
	UEdGraphNode* T = G.AddNode(600, 0, 1, 1, 2, 0);
	FTestGraph::Link(S, 0, P1, 0);
	FTestGraph::Link(S, 0, P2, 0);
	FTestGraph::Link(P1, 0, T, 1);
	FTestGraph::Link(P2, 0, T, 2);

	ArrangeNodes(G.Nodes, Settings);

	TestEqual(TEXT("P1 and P2 share a column"), P1->NodePosX, P2->NodePosX);
	const UEdGraphNode* Upper = P1->NodePosY < P2->NodePosY ? P1 : P2;
	const UEdGraphNode* Lower = Upper == P1 ? P2 : P1;
	const int32 Gap = Lower->NodePosY - (Upper->NodePosY + FTestGraph::FallbackHeight(Upper));
	TestTrue(FString::Printf(TEXT("Data nodes use the compact gap (gap %d)"), Gap),
		FMath::Abs(Gap - Settings.DataRowSpacing) <= GridSizeTolerance);
	TestFalse(TEXT("Nodes overlap"), G.AnyOverlap());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBlueprintArrangeCommentTest, "BlueprintArrange.CommentRefit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBlueprintArrangeCommentTest::RunTest(const FString& Parameters)
{
	FTestGraph G;
	UEdGraphNode* A = G.AddNode(0, 0);
	UEdGraphNode* B = G.AddNode(1000, 800);
	UEdGraphNode* C = G.AddNode(2000, 0);
	FTestGraph::Link(A, 0, B, 0);
	FTestGraph::Link(B, 0, C, 0);

	// Outer comment frames A and B, inner comment frames only B.
	auto AddComment = [&G](int32 X, int32 Y, int32 W, int32 H)
	{
		UEdGraphNode_Comment* Comment = NewObject<UEdGraphNode_Comment>(G.Graph, NAME_None, RF_Transient);
		Comment->NodePosX = X;
		Comment->NodePosY = Y;
		Comment->NodeWidth = W;
		Comment->NodeHeight = H;
		G.Graph->AddNode(Comment, false, false);
		return Comment;
	};
	UEdGraphNode_Comment* Outer = AddComment(-100, -100, 1500, 1200);
	UEdGraphNode_Comment* Inner = AddComment(950, 750, 300, 300);

	auto Contains = [](const UEdGraphNode* Comment, const UEdGraphNode* Node, int32 W, int32 H)
	{
		return Node->NodePosX >= Comment->NodePosX && Node->NodePosY >= Comment->NodePosY
			&& Node->NodePosX + W <= Comment->NodePosX + Comment->NodeWidth
			&& Node->NodePosY + H <= Comment->NodePosY + Comment->NodeHeight;
	};

	const TArray<FCommentFrame> Frames = CaptureCommentFrames(G.Graph, Settings);
	TestEqual(TEXT("Captured frames"), Frames.Num(), 2);

	ArrangeNodes(G.Nodes, Settings);
	TestEqual(TEXT("Refitted comments"), RefitCommentFrames(Frames, Settings), 2);

	const int32 W = Settings.FallbackDefaultWidth;
	TestTrue(TEXT("Inner frames B"), Contains(Inner, B, W, FTestGraph::FallbackHeight(B)));
	TestFalse(TEXT("Inner doesn't frame A"), Contains(Inner, A, W, FTestGraph::FallbackHeight(A)));
	TestTrue(TEXT("Outer frames A"), Contains(Outer, A, W, FTestGraph::FallbackHeight(A)));
	TestTrue(TEXT("Outer frames Inner"), Contains(Outer, Inner, Inner->NodeWidth, Inner->NodeHeight));
	TestFalse(TEXT("Outer doesn't frame C"), Contains(Outer, C, W, FTestGraph::FallbackHeight(C)));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
