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

	// Nodes the arranger is allowed to move.
	bool IsArrangeable(const UEdGraphNode* Node)
	{
		return Node != nullptr;
	}

	// The selection of the graph editor currently showing this graph, deduplicated.
	TArray<UEdGraphNode*> GatherSelectedNodes(const UEdGraph* Graph)
	{
		TSet<UEdGraphNode*> Unique;
		TArray<UEdGraphNode*> Result;
		if (TSharedPtr<SGraphEditor> GraphEditor = SGraphEditor::FindGraphEditorForGraph(Graph))
		{
			for (UObject* SelectedObject : GraphEditor->GetSelectedNodes())
			{
				UEdGraphNode* Node = Cast<UEdGraphNode>(SelectedObject);
				bool bAlreadyInSet = false;
				if (IsArrangeable(Node) && Node->GetGraph() == Graph)
				{
					Unique.Add(Node, &bAlreadyInSet);
					if (!bAlreadyInSet)
					{
						Result.Add(Node);
					}
				}
			}
		}
		return Result;
	}

	// Every movable node in the graph, deduplicated.
	TArray<UEdGraphNode*> GatherAllNodes(const UEdGraph* Graph)
	{
		TSet<UEdGraphNode*> Unique;
		TArray<UEdGraphNode*> Result;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			bool bAlreadyInSet = false;
			if (IsArrangeable(Node))
			{
				Unique.Add(Node, &bAlreadyInSet);
				if (!bAlreadyInSet)
				{
					Result.Add(Node);
				}
			}
		}
		return Result;
	}

	void RunArrange(UEdGraph* Graph, const TArray<UEdGraphNode*>& Nodes)
	{
		if (!Graph || Nodes.Num() < 2)
		{
			return;
		}

		// Modify() only records undo state while a transaction is open.
		FScopedTransaction Transaction(LOCTEXT("ArrangeNodes", "Arrange Nodes"));

		for (UEdGraphNode* Node : Nodes)
		{
			Node->Modify();
		}

		if (ArrangeNodes(Nodes) == 0)
		{
			Transaction.Cancel();
			return;
		}

		Graph->NotifyGraphChanged();
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

	const TWeakObjectPtr<UEdGraph> WeakGraph(const_cast<UEdGraph*>(Graph));

	// "EdGraphSchemaOrganization" is shared by the Blueprint and Material
	// schemas (unlike "EdGraphSchemaNodeActions", which is K2 only).
	Extender->AddMenuExtension(
		"EdGraphSchemaOrganization",
		EExtensionHook::After,
		CommandList,
		FMenuExtensionDelegate::CreateLambda(
			[WeakGraph](FMenuBuilder& MenuBuilder)
			{
				// Right-clicking always leaves the clicked node selected, so the
				// selection can't be used to decide between "selection" and
				// "whole graph". Offer both explicitly.
				MenuBuilder.AddMenuEntry(
					LOCTEXT("ArrangeSelection", "Arrange Selection"),
					LOCTEXT("ArrangeSelectionTooltip", "Auto-layout the selected nodes"),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateLambda([WeakGraph]()
						{
							if (UEdGraph* G = WeakGraph.Get())
							{
								RunArrange(G, GatherSelectedNodes(G));
							}
						}),
						FCanExecuteAction::CreateLambda([WeakGraph]()
						{
							const UEdGraph* G = WeakGraph.Get();
							return G && GatherSelectedNodes(G).Num() >= 2;
						})
					)
				);
				MenuBuilder.AddMenuEntry(
					LOCTEXT("ArrangeGraph", "Arrange Graph"),
					LOCTEXT("ArrangeGraphTooltip", "Auto-layout every node in this graph"),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateLambda([WeakGraph]()
						{
							if (UEdGraph* G = WeakGraph.Get())
							{
								RunArrange(G, GatherAllNodes(G));
							}
						})
					)
				);
			}
		)
	);

	return Extender;
}

#undef LOCTEXT_NAMESPACE
