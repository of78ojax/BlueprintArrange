// Copyright (c) Blueprint Arrange contributors. Licensed under the MIT License.

#include "WireDrawingPolicyShared.h"

#include "Rendering/DrawElements.h"

namespace
{
	// Same pin-fudge compensation the stock GetSplineEndPoints applies so the
	// shaped wire connects exactly where the spline did.
	void GetSplineEndPointsLocal(const FGeometry& StartGeom, const FGeometry& EndGeom, const FVector2f& InArrowRadius, FVector2f& OutStartPoint, FVector2f& OutEndPoint)
	{
		//@TODO: These values should be pushed into the Slate style, they are compensating for a bit of
		// empty space inside of the pin brush images.
		const float StartFudgeX = 4.0f;
		const float EndFudgeX = 4.0f;
		OutStartPoint = FGeometryHelper::VerticalMiddleRightOf(StartGeom) - FVector2f(StartFudgeX, 0.0f);
		OutEndPoint = FGeometryHelper::VerticalMiddleLeftOf(EndGeom) - FVector2f(InArrowRadius.X - EndFudgeX, 0);
	}
}

void FWireDrawingHelpers::GetWireEndPoints(
	const FGeometry& StartGeom,
	const FGeometry& EndGeom,
	const FVector2f& ArrowRadius,
	FVector2f& OutStart,
	FVector2f& OutEnd)
{
	GetSplineEndPointsLocal(StartGeom, EndGeom, ArrowRadius, OutStart, OutEnd);
}

void FWireDrawingHelpers::DrawShapedConnection(
	const FWireDrawContext& Context,
	const IWireShape& Shape,
	int32 LayerId,
	const FVector2f& Start,
	const FVector2f& End,
	const FConnectionParams& Params,
	const FLinearColor& WireColor)
{
	const TArray<FVector2f> Path = Shape.BuildPath(Start, End);

	// Line body
	FSlateDrawElement::MakeLines(
		*Context.DrawElements,
		LayerId,
		FPaintGeometry(),
		Path,
		ESlateDrawEffect::None,
		WireColor,
		true,
		Params.WireThickness
	);

	// Exec-flow bubbles (Kismet pulse): spaced along the path via the shape.
	if (Params.bDrawBubbles && Context.DrawElements && Context.BubbleImage != nullptr)
	{
		const float BubbleSpacing = 64.f * Context.ZoomFactor;
		const float BubbleSpeed = 192.f * Context.ZoomFactor;
		const FVector2f BubbleSize = Context.BubbleImage->ImageSize * Context.ZoomFactor * 0.2f * Params.WireThickness;

		float TotalLength = 0.0f;
		for (int32 i = 0; i < Path.Num() - 1; ++i)
		{
			TotalLength += (Path[i + 1] - Path[i]).Size();
		}

		const float Time = static_cast<float>(FPlatformTime::Seconds() - GStartTime);
		const float BubbleOffset = FMath::Fmod(Time * BubbleSpeed, BubbleSpacing);
		const int32 NumBubbles = FMath::CeilToInt(TotalLength / BubbleSpacing);
		for (int32 i = 0; i < NumBubbles; ++i)
		{
			const float Distance = ((float)i * BubbleSpacing) + BubbleOffset;
			if (Distance < TotalLength)
			{
				const float Alpha = (TotalLength > KINDA_SMALL_NUMBER) ? (Distance / TotalLength) : 0.0f;
				FVector2f BubblePos;
				FVector2f BubbleTangent;
				Shape.PointAt(Path, Alpha, BubblePos, BubbleTangent);
				BubblePos -= (BubbleSize * 0.5f);

				FSlateDrawElement::MakeBox(
					*Context.DrawElements,
					LayerId,
					FPaintGeometry(BubblePos, BubbleSize, Context.ZoomFactor),
					Context.BubbleImage,
					ESlateDrawEffect::None,
					WireColor
				);
			}
		}
	}

	// Midpoint direction arrow (Kismet "midpoint arrows" setting): oriented
	// along the path's local tangent at the middle.
	if (Context.MidpointImage != nullptr)
	{
		FVector2f Midpoint;
		FVector2f MidpointTangent;
		Shape.PointAt(Path, 0.5f, Midpoint, MidpointTangent);

		const FVector2f MidpointDrawPos = Midpoint - Context.MidpointRadius;
		const float AngleInRadians = MidpointTangent.IsNearlyZero() ? 0.0f : FMath::Atan2(MidpointTangent.Y, MidpointTangent.X);

		FSlateDrawElement::MakeRotatedBox(
			*Context.DrawElements,
			LayerId,
			FPaintGeometry(MidpointDrawPos, Context.MidpointImage->ImageSize * Context.ZoomFactor, Context.ZoomFactor),
			Context.MidpointImage,
			ESlateDrawEffect::None,
			AngleInRadians,
			TOptional<FVector2f>(),
			FSlateDrawElement::RelativeToElement,
			WireColor
		);
	}

	// End arrow: only when the policy carries an ArrowImage (neither stock
	// editor policy does; kept for future shapes per the spec).
	if (Context.ArrowImage != nullptr)
	{
		FVector2f EndPos;
		FVector2f EndTangent;
		Shape.PointAt(Path, 1.0f, EndPos, EndTangent);

		const FVector2f ArrowSize = Context.ArrowImage->ImageSize * Context.ZoomFactor;
		const float AngleInRadians = EndTangent.IsNearlyZero() ? 0.0f : FMath::Atan2(EndTangent.Y, EndTangent.X);

		// Center the arrow on the end point, rotated toward the path tangent.
		FSlateDrawElement::MakeRotatedBox(
			*Context.DrawElements,
			Context.ArrowLayerID,
			FPaintGeometry(EndPos - ArrowSize * 0.5f, ArrowSize, Context.ZoomFactor),
			Context.ArrowImage,
			ESlateDrawEffect::None,
			AngleInRadians,
			TOptional<FVector2f>(),
			FSlateDrawElement::RelativeToElement,
			WireColor
		);
	}
}
