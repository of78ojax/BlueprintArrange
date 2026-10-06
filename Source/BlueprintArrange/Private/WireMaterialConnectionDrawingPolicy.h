// Copyright (c) Blueprint Arrange contributors. Licensed under the MIT License.

#pragma once

#include "ConnectionDrawingPolicy.h"
#include "WireDrawingPolicyShared.h"
#include "Widgets/SToolTip.h"

class UMaterialGraph;
class UMaterialGraphSchema;

/**
 * Material graph connection drawing policy that renders wires through an
 * IWireShape (straight lines in v1).
 *
 * The stock FMaterialGraphConnectionDrawingPolicy lives in the private
 * directory of the MaterialEditor module, so its DetermineWiringStyle and
 * connection-tooltip logic are ported here (verified against 5.8); colors,
 * inactive/exec handling, substrate wiring and hover de-emphasis match the
 * stock editor.
 */
class FWireMaterialConnectionDrawingPolicy : public FConnectionDrawingPolicy
{
public:
	FWireMaterialConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float InZoomFactor, const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements, UEdGraph* InGraphObj);

	//~ FConnectionDrawingPolicy interface
	virtual void DetermineWiringStyle(UEdGraphPin* OutputPin, UEdGraphPin* InputPin, /*inout*/ FConnectionParams& Params) override;
	virtual TSharedPtr<IToolTip> GetConnectionToolTip(const SGraphPanel& GraphPanel, const FGraphSplineOverlapResult& OverlapData) const override;
	virtual void DrawConnection(int32 LayerId, const FVector2f& Start, const FVector2f& End, const FConnectionParams& Params) override;
	virtual void DrawSplineWithArrow(const FVector2f& StartPoint, const FVector2f& EndPoint, const FConnectionParams& Params) override;
	virtual void DrawSplineWithArrow(const FGeometry& StartGeom, const FGeometry& EndGeom, const FConnectionParams& Params) override;
	//~ End of FConnectionDrawingPolicy interface

private:
	FWireDrawContext BuildDrawContext() const;
	FText GetNodePinInfo(const TSharedPtr<SGraphPin>& PinWidget) const;

	UMaterialGraph* MaterialGraph;
	const UMaterialGraphSchema* MaterialGraphSchema;

	FLineWireShape Shape;
};
