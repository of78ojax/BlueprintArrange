// Copyright (c) Blueprint Arrange contributors. Licensed under the MIT License.

#pragma once

#include "EdGraphUtilities.h"
#include "NodeFactory.h"

/**
 * Per-graph-panel node factory installed on supported graph editors. The
 * panel uses its NodeFactory exclusively when drawing connections
 * (SGraphPanel::OnPaint), which cleanly outranks registered connection
 * factories (including MaterialEditor's) without touching private registry
 * state. Node and pin widgets delegate to the stock FNodeFactory, so widget
 * creation is unchanged.
 *
 * Connection dispatch: exact UEdGraphSchema_K2 -> Kismet-based wire policy,
 * UMaterialGraphSchema -> Material-based wire policy, anything else ->
 * Super:: (the full stock chain).
 */
class FWireGraphNodeFactory : public FGraphNodeFactory
{
public:
	virtual FConnectionDrawingPolicy* CreateConnectionPolicy(const UEdGraphSchema* Schema, int32 InBackLayerID, int32 InFrontLayerID, float ZoomFactor, const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements, UEdGraph* InGraphObj) override;
};

/**
 * Visual pin factory used as the installation hook for Blueprint graphs:
 * when a pin widget is created for a supported graph, the owning graph
 * editor is located and the FWireGraphNodeFactory installed on its panel
 * (idempotently). Pin widgets themselves are stock (CreatePin returns null).
 *
 * Note: this hook is not reached for Material graphs because MaterialEditor's
 * own registered pin factory claims material pins first; Material editors are
 * handled via UAssetEditorSubsystem::OnAssetEditorOpened instead (see
 * FWireGraphIntegration::OnAssetEditorOpened in the module).
 */
/**
 * Material editor hook: subscribed to
 * UAssetEditorSubsystem::OnAssetEditorOpened (which fires after the material
 * editor's graph widget exists). Installs the factory on the material graph's
 * panel.
 */
struct FWirePanelPinFactory : public FGraphPanelPinFactory
{
public:
	virtual TSharedPtr<class SGraphPin> CreatePin(class UEdGraphPin* Pin) const override;
};

namespace WireGraphIntegration
{
	void OnAssetEditorOpened(UObject* Asset);
}
