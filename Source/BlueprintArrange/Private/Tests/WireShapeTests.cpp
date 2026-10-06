// Copyright (c) Blueprint Arrange contributors. Licensed under the MIT License.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "WireShape.h"

namespace BlueprintArrangeWireShapeTests
{
	bool TestPointAt(const IWireShape& Shape, const TArray<FVector2f>& Path, float Alpha, const FVector2f& Expected, const FVector2f& ExpectedTangent)
	{
		FVector2f Position;
		FVector2f Tangent;
		Shape.PointAt(Path, Alpha, Position, Tangent);
		return Position.Equals(Expected, KINDA_SMALL_NUMBER) && Tangent.Equals(ExpectedTangent, KINDA_SMALL_NUMBER);
	}
}

using namespace BlueprintArrangeWireShapeTests;

// 2-point path: BuildPath returns {Start, End}; PointAt(0)=Start, PointAt(1)=End, PointAt(0.5)=midpoint.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWireShapeLinePathTest, "BlueprintArrange.WireShape.LinePath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWireShapeLinePathTest::RunTest(const FString& Parameters)
{
	FLineWireShape Shape;

	const FVector2f Start(10.0f, 30.0f);
	const FVector2f End(200.0f, 90.0f);

	const TArray<FVector2f> Path = Shape.BuildPath(Start, End);
	TestEqual(TEXT("Line path has exactly two points"), Path.Num(), 2);
	if (Path.Num() == 2)
	{
		TestTrue(TEXT("Path[0] is Start"), Path[0].Equals(Start));
		TestTrue(TEXT("Path[1] is End"), Path[1].Equals(End));
	}

	TestTrue(TEXT("PointAt(0) is Start with unit tangent toward End"),
		TestPointAt(Shape, Path, 0.0f, Start, FVector2f(190.0f, 60.0f).GetSafeNormal()));
	TestTrue(TEXT("PointAt(1) is End with unit tangent toward End"),
		TestPointAt(Shape, Path, 1.0f, End, FVector2f(190.0f, 60.0f).GetSafeNormal()));
	TestTrue(TEXT("PointAt(0.5) is midpoint"),
		TestPointAt(Shape, Path, 0.5f, FVector2f(105.0f, 60.0f), FVector2f(190.0f, 60.0f).GetSafeNormal()));

	// Alpha clamping: out-of-range values clamp onto the path.
	TestTrue(TEXT("PointAt(-1) clamps to Start"), TestPointAt(Shape, Path, -1.0f, Start, FVector2f(190.0f, 60.0f).GetSafeNormal()));
	TestTrue(TEXT("PointAt(2) clamps to End"), TestPointAt(Shape, Path, 2.0f, End, FVector2f(190.0f, 60.0f).GetSafeNormal()));

	// Degenerate zero-length path stays stable.
	const TArray<FVector2f> ZeroPath = Shape.BuildPath(FVector2f(5.0f, 5.0f), FVector2f(5.0f, 5.0f));
	FVector2f DegeneratePos;
	FVector2f DegenerateTangent;
	Shape.PointAt(ZeroPath, 0.5f, DegeneratePos, DegenerateTangent);
	TestTrue(TEXT("Zero-length path evaluates without error"), DegeneratePos.Equals(FVector2f(5.0f, 5.0f)));

	return true;
}

// Multi-segment generality: arc-length-proportional evaluation on a synthetic 3-point path.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWireShapePolylineTest, "BlueprintArrange.WireShape.PolylineEvaluation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWireShapePolylineTest::RunTest(const FString& Parameters)
{
	FLineWireShape Shape;

	// L-shaped synthetic path: 3 units right, then 4 units down (total length 7).
	const TArray<FVector2f> Path = {
		FVector2f(0.0f, 0.0f),
		FVector2f(3.0f, 0.0f),
		FVector2f(3.0f, 4.0f),
	};

	// Segment-length-proportional: half the total length lands exactly on the corner.
	TestTrue(TEXT("PointAt(3/7) is the corner between segments"),
		TestPointAt(Shape, Path, 3.0f / 7.0f, FVector2f(3.0f, 0.0f), FVector2f(0.0f, 1.0f)));
	TestTrue(TEXT("PointAt(0.5) is at half of total arc length"),
		TestPointAt(Shape, Path, 0.5f, FVector2f(3.0f, 0.5f), FVector2f(0.0f, 1.0f)));
	TestTrue(TEXT("PointAt(1) is the path end with tangent along the final segment"),
		TestPointAt(Shape, Path, 1.0f, FVector2f(3.0f, 4.0f), FVector2f(0.0f, 1.0f)));
	TestTrue(TEXT("PointAt(1/14) is on the first segment"),
		TestPointAt(Shape, Path, 1.0f / 14.0f, FVector2f(0.5f, 0.0f), FVector2f(1.0f, 0.0f)));

	return true;
}

#endif
