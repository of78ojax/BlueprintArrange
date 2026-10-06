// Copyright (c) Blueprint Arrange contributors. Licensed under the MIT License.

#pragma once

#include "BlueprintConnectionDrawingPolicy.h"
#include "WireDrawingPolicyShared.h"

/**
 * Blueprint (K2) connection drawing policy that renders wires through an
 * IWireShape (straight lines in v1). Everything except the spline draw
 * primitives is stock FKismetConnectionDrawingPolicy: colors, exec-flow pulse
 * envelope and bubbles, hover de-emphasis, culled-node geometry, reroutes.
 */
class FWireKismetConnectionDrawingPolicy : public FKismetConnectionDrawingPolicy
{
public:
	FWireKismetConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float InZoomFactor, const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements, UEdGraph* InGraphObj);

	//~ FConnectionDrawingPolicy interface (FVector2f overloads; the deprecated
	// FVector2D variants route through these in the base class)
	virtual void DrawConnection(int32 LayerId, const FVector2f& Start, const FVector2f& End, const FConnectionParams& Params) override;
	virtual void DrawSplineWithArrow(const FVector2f& StartPoint, const FVector2f& EndPoint, const FConnectionParams& Params) override;
	virtual void DrawSplineWithArrow(const FGeometry& StartGeom, const FGeometry& EndGeom, const FConnectionParams& Params) override;
	//~ End of FConnectionDrawingPolicy interface

private:
	FWireDrawContext BuildDrawContext() const;

	FLineWireShape Shape;
};
