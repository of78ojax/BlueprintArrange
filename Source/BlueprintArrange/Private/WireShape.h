// Copyright (c) Blueprint Arrange contributors. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Math/Vector2D.h"

/**
 * Wire shape strategy: maps a connection's endpoints to a drawable path.
 *
 * Drawing policies consume only BuildPath/PointAt, so additional styles
 * (Manhattan/circuit elbows, the stock spline) can be added later without
 * touching the policies or editor registration.
 */
class IWireShape
{
public:
	virtual ~IWireShape() = default;

	/** Points of the wire path from Start to End, in draw-space. Always contains at least two points. */
	virtual TArray<FVector2f> BuildPath(const FVector2f& Start, const FVector2f& End) const = 0;

	/**
	 * Position and unit tangent of the path at normalized arc-length Alpha in [0,1].
	 * Alpha is proportional to accumulated segment length, so 0 = Start, 1 = End.
	 * The tangent at Alpha=1 points along the final segment (used to orient the
	 * direction indicator of multi-segment shapes).
	 */
	virtual void PointAt(const TArray<FVector2f>& PathPoints, float Alpha, FVector2f& OutPosition, FVector2f& OutTangent) const = 0;
};

/** Straight-line wire: a two-point path with linear evaluation. */
class FLineWireShape : public IWireShape
{
public:
	virtual TArray<FVector2f> BuildPath(const FVector2f& Start, const FVector2f& End) const override;

	virtual void PointAt(const TArray<FVector2f>& PathPoints, float Alpha, FVector2f& OutPosition, FVector2f& OutTangent) const override;
};
