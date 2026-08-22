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
	bool bIsPin)
{
	TSharedRef<FExtender> Extender = MakeShared<FExtender>();

	if (!Node)
	{
		return Extender;
	}

	Extender->AddMenuExtension(
		"EdGraphSchemaNodeActions",
		EExtensionHook::After,
		CommandList,
		FMenuExtensionDelegate::CreateLambda(
			[this, Graph, Node](FMenuBuilder& MenuBuilder)
			{
				MenuBuilder.AddMenuEntry(
					FText::FromString("Arrange"),
					FText::FromString("Arrange Blueprint nodes"),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateLambda([this, Graph, Node]()
						{
							ArrangeCurrentGraph(Graph,Node);
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

	for (UEdGraphNode* curNode : SelectedNodes)
	{
		if (curNode)
		{
			curNode->Modify();
		}
	}

	ArrangeNodes(SelectedNodes);

	const_cast<UEdGraph*>(Graph)->NotifyGraphChanged();
}
