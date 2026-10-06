// Copyright (c) Blueprint Arrange contributors. Licensed under the MIT License.

#include "WireGraphIntegration.h"

#include "EdGraphSchema_K2.h"
#include "GraphEditor.h"
#include "MaterialGraph/MaterialGraphSchema.h"
#include "SGraphPanel.h"
#include "WireKismetConnectionDrawingPolicy.h"
#include "WireMaterialConnectionDrawingPolicy.h"

namespace
{
	// Shared factory instance handed to every supported panel. Stateless.
	const TSharedRef<FWireGraphNodeFactory> WireNodeFactory = MakeShared<FWireGraphNodeFactory>();

	void InstallOnPanel(const UEdGraph* Graph)
	{
		if (!Graph)
		{
			return;
		}

		const UEdGraphSchema* Schema = Graph->GetSchema();
		const bool bSupported = Schema
			&& (Schema->GetClass() == UEdGraphSchema_K2::StaticClass() || Schema->IsA(UMaterialGraphSchema::StaticClass()));
		if (!bSupported)
		{
			return;
		}

		TSharedPtr<SGraphEditor> GraphEditor = SGraphEditor::FindGraphEditorForGraph(Graph);
		if (!GraphEditor.IsValid())
		{
			UE_LOG(LogTemp, Log, TEXT("BlueprintArrange: InstallOnPanel - no graph editor found for %s"), *GetNameSafe(Graph));
			return;
		}

		if (SGraphPanel* Panel = GraphEditor->GetGraphPanel())
		{
			UE_LOG(LogTemp, Log, TEXT("BlueprintArrange: InstallOnPanel - installing on panel for %s"), *GetNameSafe(Graph));
			Panel->SetNodeFactory(WireNodeFactory);
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("BlueprintArrange: InstallOnPanel - graph editor has no panel"));
		}
	}
}

FConnectionDrawingPolicy* FWireGraphNodeFactory::CreateConnectionPolicy(const UEdGraphSchema* Schema, int32 InBackLayerID, int32 InFrontLayerID, float ZoomFactor, const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements, UEdGraph* InGraphObj)
{
	if (Schema != nullptr)
	{
		// Exact match: plain Blueprint event/function/macro graphs. K2-derived
		// schemas (animation graphs etc.) fall through to the stock chain.
		if (Schema->GetClass() == UEdGraphSchema_K2::StaticClass())
		{
			static bool bLoggedKismet = false;
			if (!bLoggedKismet)
			{
				bLoggedKismet = true;
				UE_LOG(LogTemp, Log, TEXT("BlueprintArrange: wire policy engaged (Kismet)"));
			}
			return new FWireKismetConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, ZoomFactor, InClippingRect, InDrawElements, InGraphObj);
		}

		if (Schema->IsA(UMaterialGraphSchema::StaticClass()))
		{
			static bool bLoggedMaterial = false;
			if (!bLoggedMaterial)
			{
				bLoggedMaterial = true;
				UE_LOG(LogTemp, Log, TEXT("BlueprintArrange: wire policy engaged (Material)"));
			}
			return new FWireMaterialConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, ZoomFactor, InClippingRect, InDrawElements, InGraphObj);
		}
	}

	// Unsupported schema: exactly the stock behavior.
	return FGraphNodeFactory::CreateConnectionPolicy(Schema, InBackLayerID, InFrontLayerID, ZoomFactor, InClippingRect, InDrawElements, InGraphObj);
}

TSharedPtr<SGraphPin> FWirePanelPinFactory::CreatePin(UEdGraphPin* Pin) const
{
	// Stock pin widgets; this factory is only the installation hook for
	// Blueprint (K2) graphs. Material graphs are handled via
	// WireGraphIntegration::OnAssetEditorOpened because MaterialEditor's own
	// pin factory claims material pins before this factory is consulted.
	if (Pin != nullptr)
	{
		if (const UEdGraphNode* Node = Pin->GetOwningNode())
		{
			if (const UEdGraph* Graph = Node->GetGraph())
			{
				InstallOnPanel(Graph);
			}
		}
	}
	return nullptr;
}

void WireGraphIntegration::OnAssetEditorOpened(UObject* Asset)
{
	UE_LOG(LogTemp, Log, TEXT("BlueprintArrange: OnAssetEditorOpened (%s)"), *GetNameSafe(Asset));
	// Material editors: the graph editor widget already exists when this
	// fires (UAssetEditorSubsystem broadcasts after OpenAssetEditor returns).
	if (UMaterial* Material = Cast<UMaterial>(Asset))
	{
		if (UEdGraph* MaterialGraph = Material->MaterialGraph)
		{
			InstallOnPanel(MaterialGraph);
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("BlueprintArrange: material has no MaterialGraph yet"));
		}
	}
	else if (UMaterialFunction* MaterialFunction = Cast<UMaterialFunction>(Asset))
	{
		if (UEdGraph* FunctionGraph = MaterialFunction->MaterialGraph)
		{
			InstallOnPanel(FunctionGraph);
		}
	}
}
