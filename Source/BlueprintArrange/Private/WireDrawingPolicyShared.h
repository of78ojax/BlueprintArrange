// Copyright (c) Blueprint Arrange contributors. Licensed under the MIT License.

#pragma once

#include "ConnectionDrawingPolicy.h"
#include "WireShape.h"

/**
 * Shared geometry code for the two wire drawing policies. Each concrete
 * policy supplies its engine base class and passes the base's protected draw
 * state (draw elements, layers, arrow/midpoint images, zoom) into
 * FWireDrawingHelpers via FWireDrawContext, since those members are protected
 * in FConnectionDrawingPolicy.
 */
struct FWireDrawContext
{
	FSlateWindowElementList* DrawElements = nullptr;
	const FSlateBrush* ArrowImage = nullptr;
	const FSlateBrush* MidpointImage = nullptr;
	const FSlateBrush* BubbleImage = nullptr;
	FVector2f MidpointRadius = FVector2f::ZeroVector;
	float ZoomFactor = 1.0f;
	int32 WireLayerID = 0;
	int32 ArrowLayerID = 0;
};

class FWireDrawingHelpers
{
public:
	// Draws the wire as the shape's polyline: line body, exec-flow bubbles,
	// optional midpoint arrow, and an end arrow if the policy carries an
	// ArrowImage. The caller (policy) handles slice-line/hover bookkeeping
	// before calling.
	static void DrawShapedConnection(
		const FWireDrawContext& Context,
		const IWireShape& Shape,
		int32 LayerId,
		const FVector2f& Start,
		const FVector2f& End,
		const FConnectionParams& Params,
		const FLinearColor& WireColor);

	// Computes the same draw-space endpoints the stock DrawSplineWithArrow
	// uses for a pair of pin geometries (pin fudge factors included).
	static void GetWireEndPoints(
		const FGeometry& StartGeom,
		const FGeometry& EndGeom,
		const FVector2f& ArrowRadius,
		FVector2f& OutStart,
		FVector2f& OutEnd);
};
