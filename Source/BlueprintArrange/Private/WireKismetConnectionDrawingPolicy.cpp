// Copyright (c) Blueprint Arrange contributors. Licensed under the MIT License.

#include "WireKismetConnectionDrawingPolicy.h"

FWireKismetConnectionDrawingPolicy::FWireKismetConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float InZoomFactor, const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements, UEdGraph* InGraphObj)
	: FKismetConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, InZoomFactor, InClippingRect, InDrawElements, InGraphObj)
{
}

FWireDrawContext FWireKismetConnectionDrawingPolicy::BuildDrawContext() const
{
	FWireDrawContext Context;
	Context.DrawElements = &DrawElementsList;
	Context.ArrowImage = ArrowImage;
	Context.MidpointImage = MidpointImage;
	Context.BubbleImage = BubbleImage;
	Context.MidpointRadius = MidpointRadius;
	Context.ZoomFactor = ZoomFactor;
	Context.WireLayerID = WireLayerID;
	Context.ArrowLayerID = ArrowLayerID;
	return Context;
}

void FWireKismetConnectionDrawingPolicy::DrawConnection(int32 LayerId, const FVector2f& Start, const FVector2f& End, const FConnectionParams& Params)
{
	// Preserve the base slice-line / connection-hover bookkeeping against the
	// approximated spline geometry (spec-approved v1 hit-testing trade-off).
	bool bSliceLineIntersectsSpline = false;
	CheckSplineConnectionOverlapWithCursor(AbsoluteMousePosition, Start, End, Params, /*out*/ SplineOverlapResult, /*out*/ &bSliceLineIntersectsSpline);

	FLinearColor WireColor = Params.WireColor;
	if (bSliceLineIntersectsSpline)
	{
		WireColor.A *= SliceDeemphasisAlphaMultiplier;
		ConnectionsIntersectingSliceLine.Emplace(Params.AssociatedPin1, Params.AssociatedPin2);
	}

	FWireDrawingHelpers::DrawShapedConnection(
		BuildDrawContext(),
		Shape,
		LayerId,
		Start,
		End,
		Params,
		WireColor
	);
}

void FWireKismetConnectionDrawingPolicy::DrawSplineWithArrow(const FVector2f& StartPoint, const FVector2f& EndPoint, const FConnectionParams& Params)
{
	DrawConnection(WireLayerID, StartPoint, EndPoint, Params);
}

void FWireKismetConnectionDrawingPolicy::DrawSplineWithArrow(const FGeometry& StartGeom, const FGeometry& EndGeom, const FConnectionParams& Params)
{
	FVector2f StartPoint;
	FVector2f EndPoint;
	FWireDrawingHelpers::GetWireEndPoints(StartGeom, EndGeom, ArrowRadius, StartPoint, EndPoint);

	DrawSplineWithArrow(StartPoint, EndPoint, Params);
}
