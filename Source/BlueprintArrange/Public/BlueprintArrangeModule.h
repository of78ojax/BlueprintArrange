// Copyright (c) Blueprint Arrange contributors. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FUICommandList;
class UEdGraph;
class UEdGraphNode;
class UEdGraphPin;
class FExtender;

class FBlueprintArrangeModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	TSharedRef<FExtender> OnExtendGraphMenu(
		TSharedRef<FUICommandList> CommandList,
		const UEdGraph* Graph,
		const UEdGraphNode* Node,
		const UEdGraphPin* Pin,
		bool bIsReadOnly
	);


};
