// Copyright (c) Blueprint Arrange contributors. Licensed under the MIT License.

using UnrealBuildTool;

public class BlueprintArrange : ModuleRules
{
	public BlueprintArrange(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject" });

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Engine",
			"UnrealEd",
			"BlueprintGraph",
			"Slate",
			"SlateCore",
			"GraphEditor",
		});
	}
}
