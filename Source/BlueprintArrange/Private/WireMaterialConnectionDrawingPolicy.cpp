// Copyright (c) Blueprint Arrange contributors. Licensed under the MIT License.

#include "WireMaterialConnectionDrawingPolicy.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "MaterialGraph/MaterialGraph.h"
#include "MaterialGraph/MaterialGraphSchema.h"
#include "MaterialGraph/MaterialShaderValueTypeObject.h"
#include "MaterialEditor/SGraphSubstrateMaterial.h"
#include "MaterialShared.h"
#include "SGraphPin.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

class FSlateRect;
class SGraphPanel;

FWireMaterialConnectionDrawingPolicy::FWireMaterialConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float InZoomFactor, const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements, UEdGraph* InGraphObj)
	: FConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, InZoomFactor, InClippingRect, InDrawElements)
	, MaterialGraph(CastChecked<UMaterialGraph>(InGraphObj))
	, MaterialGraphSchema(CastChecked<UMaterialGraphSchema>(InGraphObj->GetSchema()))
{
	// Don't want to draw ending arrowheads
	ArrowImage = nullptr;
	ArrowRadius = FVector2f::ZeroVector;

	// Still need to be able to perceive the graph while dragging connectors, esp over comment boxes
	HoverDeemphasisDarkFraction = 0.4f;
}

FWireDrawContext FWireMaterialConnectionDrawingPolicy::BuildDrawContext() const
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

// Ported from FMaterialGraphConnectionDrawingPolicy::DetermineWiringStyle (UE 5.8),
// minus the knot-tangent flip which only affects spline tangents.
void FWireMaterialConnectionDrawingPolicy::DetermineWiringStyle(UEdGraphPin* OutputPin, UEdGraphPin* InputPin, /*inout*/ FConnectionParams& Params)
{
	Params.AssociatedPin1 = OutputPin;
	Params.AssociatedPin2 = InputPin;
	Params.WireColor = MaterialGraphSchema->ActivePinColor;

	if (Substrate::IsSubstrateEnabled() && (FSubstrateWidget::HasOutputSubstrateType(OutputPin) || FSubstrateWidget::HasInputSubstrateType(InputPin) || FSubstrateWidget::HasInputSubstrateType(OutputPin)))
	{
		Params.WireColor = FSubstrateWidget::GetConnectionColor();
	}

	bool bInactivePin = false;
	bool bExecPin = false;

	// Have to consider both pins as the input will be an 'output' when previewing a connection
	if (OutputPin)
	{
		if (!MaterialGraph->IsInputActive(OutputPin))
		{
			bInactivePin = true;
		}

		if (OutputPin->PinType.PinCategory == UMaterialGraphSchema::PC_Exec)
		{
			bExecPin = true;
		}

		UEdGraphNode* OutputNode = OutputPin->GetOwningNode();
		if (!OutputNode->IsNodeEnabled() || OutputNode->IsDisplayAsDisabledForced() || OutputNode->IsNodeUnrelated())
		{
			bInactivePin = true;
		}
	}
	if (InputPin)
	{
		if (!MaterialGraph->IsInputActive(InputPin))
		{
			bInactivePin = true;
		}

		if (InputPin->PinType.PinCategory == UMaterialGraphSchema::PC_Exec)
		{
			bExecPin = true;
		}

		UEdGraphNode* InputNode = InputPin->GetOwningNode();
		if (!InputNode->IsNodeEnabled() || InputNode->IsDisplayAsDisabledForced() || InputNode->IsNodeUnrelated())
		{
			bInactivePin = true;
		}
	}

	if (bInactivePin)
	{
		Params.WireColor = MaterialGraphSchema->InactivePinColor;
	}
	else if (bExecPin)
	{
		Params.WireColor = Settings->ExecutionPinTypeColor;
		Params.WireThickness = Settings->DefaultExecutionWireThickness;
	}
	else if (InputPin)
	{
		if (const UMaterialShaderValueTypeObject* ValueTypeObj = Cast<UMaterialShaderValueTypeObject>(InputPin->PinType.PinSubCategoryObject.Get()))
		{
			Params.WireColor = UMaterialGraphSchema::GetColorForConnectionType(ValueTypeObj->ValueType);
		}
	}

	const bool bDeemphasizeUnhoveredPins = HoveredPins.Num() > 0;
	if (bDeemphasizeUnhoveredPins)
	{
		ApplyHoverDeemphasis(OutputPin, InputPin, /*inout*/ Params.WireThickness, /*inout*/ Params.WireColor);
	}
}

// Ported from FMaterialGraphConnectionDrawingPolicy (UE 5.8).
TSharedPtr<IToolTip> FWireMaterialConnectionDrawingPolicy::GetConnectionToolTip(const SGraphPanel& GraphPanel, const FGraphSplineOverlapResult& OverlapData) const
{
	TSharedPtr<SGraphPin> Pin1Widget;
	TSharedPtr<SGraphPin> Pin2Widget;
	OverlapData.GetPinWidgets(GraphPanel, Pin1Widget, Pin2Widget);

	if (!Pin1Widget || !Pin2Widget)
	{
		return FConnectionDrawingPolicy::GetConnectionToolTip(GraphPanel, OverlapData);
	}

	const FText LeftText = FText::Format(NSLOCTEXT("Unreal", "PinConnectionTooltipLeft", "<< {0}"), GetNodePinInfo(Pin1Widget));
	const FText RightText = FText::Format(NSLOCTEXT("Unreal", "PinConnectionTooltipRight", "{0} >>"), GetNodePinInfo(Pin2Widget));

	return SNew(SToolTip)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.HAlign(HAlign_Left)
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 5.0f)
			[
				SNew(STextBlock)
					.Margin(FMargin(0.0f, 0.0f, 4.0f, 0.0f))
					.Justification(ETextJustify::Left)
					.Text(LeftText)
			]
			+ SVerticalBox::Slot()
			.HAlign(HAlign_Right)
			.AutoHeight()
			.Padding(0.0f, 5.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
					.Margin(FMargin(4.0f, 0.0f, 0.0f, 0.0f))
					.Justification(ETextJustify::Right)
					.Text(RightText)
			]
		];
}

// Ported from FMaterialGraphConnectionDrawingPolicy (UE 5.8).
FText FWireMaterialConnectionDrawingPolicy::GetNodePinInfo(const TSharedPtr<SGraphPin>& PinWidget) const
{
	const UEdGraphPin* PinObj = PinWidget->GetPinObj();
	const UEdGraphNode* EdNode = PinObj->GetOwningNode();

	FString NodeTitle = EdNode->GetNodeTitle(ENodeTitleType::ListView).ToString();
	NodeTitle.RemoveFromStart(TEXT("Material Expression "));

	if (EdNode->GetCanRenameNode())
	{
		FText NodeEditableName = EdNode->GetNodeTitle(ENodeTitleType::EditableTitle);
		NodeTitle = NodeTitle + TEXT(" (") + NodeEditableName.ToString() + TEXT(")");
	}
	const FText PinName = PinObj->GetDisplayName().IsEmptyOrWhitespace() ? FText::FromName(PinObj->PinName) : PinObj->GetDisplayName();

	return FText::Format(NSLOCTEXT("Unreal", "PinConnectionTooltipPartial", "{0}\r\n{1}"), FText::FromString(NodeTitle), /*NodeType,*/ PinName);
}

void FWireMaterialConnectionDrawingPolicy::DrawConnection(int32 LayerId, const FVector2f& Start, const FVector2f& End, const FConnectionParams& Params)
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

void FWireMaterialConnectionDrawingPolicy::DrawSplineWithArrow(const FVector2f& StartPoint, const FVector2f& EndPoint, const FConnectionParams& Params)
{
	DrawConnection(WireLayerID, StartPoint, EndPoint, Params);
}

void FWireMaterialConnectionDrawingPolicy::DrawSplineWithArrow(const FGeometry& StartGeom, const FGeometry& EndGeom, const FConnectionParams& Params)
{
	FVector2f StartPoint;
	FVector2f EndPoint;
	FWireDrawingHelpers::GetWireEndPoints(StartGeom, EndGeom, ArrowRadius, StartPoint, EndPoint);

	DrawSplineWithArrow(StartPoint, EndPoint, Params);
}
