// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintArrangeModule.h"


#include "ToolMenus.h"
#include "BlueprintEditor.h"
#include "GraphEditorModule.h"
#include "GraphEditor.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "SNodePanel.h"
#include "GraphArranger.h"
#include "ScopedTransaction.h"
#include "EdGraphSchema_K2.h"
#include "MaterialGraph/MaterialGraphSchema.h"

#define LOCTEXT_NAMESPACE "BlueprintArrange"

namespace
{
	// Only graphs that flow left to right and store positions in NodePosX/Y.
	bool IsSupportedGraph(const UEdGraph* Graph)
	{
		const UEdGraphSchema* Schema = Graph ? Graph->GetSchema() : nullptr;
		return Schema && (Schema->IsA<UEdGraphSchema_K2>() || Schema->IsA<UMaterialGraphSchema>());
	}
}

IMPLEMENT_MODULE(FBlueprintArrangeModule, BlueprintArrange)

void FBlueprintArrangeModule::StartupModule()
{
	FGraphEditorModule& GraphEditorModule =
		FModuleManager::LoadModuleChecked<FGraphEditorModule>(
			"GraphEditor"
		);
	GraphEditorModule.GetAllGraphEditorContextMenuExtender().Add(
		FGraphEditorModule::FGraphEditorMenuExtender_SelectedNode::CreateRaw(
			this,
			&FBlueprintArrangeModule::OnExtendGraphMenu
		)
	);
}

void FBlueprintArrangeModule::ShutdownModule()
{
	FGraphEditorModule& GraphEditorModule =
		FModuleManager::LoadModuleChecked<FGraphEditorModule>(
			"GraphEditor"
		);
	GraphEditorModule.GetAllGraphEditorContextMenuExtender().RemoveAll(
		[this](const FGraphEditorModule::FGraphEditorMenuExtender_SelectedNode& Extender)
		{
			return Extender.IsBoundToObject(this);
		}
	);
}


TSharedRef<FExtender> FBlueprintArrangeModule::OnExtendGraphMenu(
	TSharedRef<FUICommandList> CommandList,
	const UEdGraph* Graph,
	const UEdGraphNode* Node,
	const UEdGraphPin* Pin,
	bool bIsReadOnly)
{
	TSharedRef<FExtender> Extender = MakeShared<FExtender>();

	if (!Node || bIsReadOnly || !IsSupportedGraph(Graph))
	{
		return Extender;
	}

	const TWeakObjectPtr<const UEdGraph> WeakGraph(Graph);
	const TWeakObjectPtr<const UEdGraphNode> WeakNode(Node);

	// "EdGraphSchemaOrganization" is shared by the Blueprint and Material
	// schemas (unlike "EdGraphSchemaNodeActions", which is K2 only).
	Extender->AddMenuExtension(
		"EdGraphSchemaOrganization",
		EExtensionHook::After,
		CommandList,
		FMenuExtensionDelegate::CreateLambda(
			[this, WeakGraph, WeakNode](FMenuBuilder& MenuBuilder)
			{
				MenuBuilder.AddMenuEntry(
					FText::FromString("Arrange"),
					FText::FromString("Arrange Blueprint nodes"),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateLambda([this, WeakGraph, WeakNode]()
						{
							if (WeakGraph.IsValid())
							{
								ArrangeCurrentGraph(WeakGraph.Get(), WeakNode.Get());
							}
						})
					)
				);
			}
		)
	);

	return Extender;
}

void FBlueprintArrangeModule::ArrangeCurrentGraph(const UEdGraph* Graph, const UEdGraphNode* Node)
{
	// debug log for testing
	UE_LOG(LogTemp, Warning, TEXT("ArrangeCurrentGraph called"));

	if (!GEditor)
	{
		return;
	}

	// Get the SGraphEditor widget that is currently viewing this graph,
	// then read its selection set. If none is open / nothing selected,
	// fall back to arranging all nodes in the graph.
	TArray<UEdGraphNode*> SelectedNodes;

	if (TSharedPtr<SGraphEditor> GraphEditor = SGraphEditor::FindGraphEditorForGraph(Graph))
	{
		const FGraphPanelSelectionSet& Selection = GraphEditor->GetSelectedNodes();
		SelectedNodes.Reserve(Selection.Num());
		for (UObject* SelectedObject : Selection)
		{
			if (UEdGraphNode* SelectedNode = Cast<UEdGraphNode>(SelectedObject))
			{
				SelectedNodes.Add(SelectedNode);
			}
		}
	}

	// Fall back to every node in the graph when there is no selection.
	if (SelectedNodes.IsEmpty())
	{
		for (UEdGraphNode* curNode : Graph->Nodes)
		{
			if (curNode)
			{
				SelectedNodes.Add(curNode);
			}
		}
	}

	// Modify() only records undo state while a transaction is open.
	FScopedTransaction Transaction(LOCTEXT("ArrangeNodes", "Arrange Nodes"));

	for (UEdGraphNode* curNode : SelectedNodes)
	{
		if (curNode)
		{
			curNode->Modify();
		}
	}

	if (ArrangeNodes(SelectedNodes) == 0)
	{
		Transaction.Cancel();
		return;
	}

	const_cast<UEdGraph*>(Graph)->NotifyGraphChanged();
}

#undef LOCTEXT_NAMESPACE
