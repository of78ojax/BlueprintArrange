// Copyright (c) Blueprint Arrange contributors. Licensed under the MIT License.

#include "WireShape.h"

namespace
{
	// Arc-length-proportional evaluation shared by all polyline shapes.
	void EvaluatePolyline(const TArray<FVector2f>& PathPoints, float Alpha, FVector2f& OutPosition, FVector2f& OutTangent)
	{
		OutPosition = FVector2f::ZeroVector;
		OutTangent = FVector2f(1.0f, 0.0f);

		const int32 NumPoints = PathPoints.Num();
		if (NumPoints == 0)
		{
			return;
		}
		if (NumPoints == 1)
		{
			OutPosition = PathPoints[0];
			return;
		}

		// Clamp to the path.
		Alpha = FMath::Clamp(Alpha, 0.0f, 1.0f);

		const int32 LastPointIndex = NumPoints - 1;
		const int32 LastSegmentIndex = LastPointIndex - 1;

		float TotalLength = 0.0f;
		for (int32 i = 0; i <= LastSegmentIndex; ++i)
		{
			TotalLength += (PathPoints[i + 1] - PathPoints[i]).Size();
		}

		if (TotalLength <= KINDA_SMALL_NUMBER)
		{
			// Degenerate path (all points coincide): evaluate on the last segment
			// so the tangent still points toward the end.
			OutPosition = FMath::Lerp(PathPoints[LastSegmentIndex], PathPoints[LastPointIndex], Alpha);
			OutTangent = FMath::Lerp(PathPoints[LastSegmentIndex], PathPoints[LastPointIndex], 0.5f + 0.5f * Alpha);
			if (!OutTangent.IsNearlyZero())
			{
				OutTangent.Normalize();
			}
			return;
		}

		const float TargetDistance = Alpha * TotalLength;

		float Accumulated = 0.0f;
		for (int32 i = 0; i <= LastSegmentIndex; ++i)
		{
			const FVector2f& P0 = PathPoints[i];
			const FVector2f& P1 = PathPoints[i + 1];
			const float SegmentLength = (P1 - P0).Size();

			const bool bIsLastSegment = (i == LastSegmentIndex);
			// Strict >: at an exact interior segment boundary the evaluation
			// falls through to the next segment, so the tangent points along
			// the outgoing segment (matches arrow/orientation expectations).
			const bool bWithinSegment = (Accumulated + SegmentLength > TargetDistance) || bIsLastSegment;
			if (bWithinSegment)
			{
				const float LocalAlpha = (SegmentLength > KINDA_SMALL_NUMBER)
					? FMath::Clamp((TargetDistance - Accumulated) / SegmentLength, 0.0f, 1.0f)
					: 0.0f;

				OutPosition = FMath::Lerp(P0, P1, LocalAlpha);

				OutTangent = P1 - P0;
				if (!OutTangent.IsNearlyZero())
				{
					OutTangent.Normalize();
				}
				return;
			}

			Accumulated += SegmentLength;
		}
	}
}

TArray<FVector2f> FLineWireShape::BuildPath(const FVector2f& Start, const FVector2f& End) const
{
	TArray<FVector2f> Path;
	Path.Reserve(2);
	Path.Add(Start);
	Path.Add(End);
	return Path;
}

void FLineWireShape::PointAt(const TArray<FVector2f>& PathPoints, float Alpha, FVector2f& OutPosition, FVector2f& OutTangent) const
{
	EvaluatePolyline(PathPoints, Alpha, OutPosition, OutTangent);
}
