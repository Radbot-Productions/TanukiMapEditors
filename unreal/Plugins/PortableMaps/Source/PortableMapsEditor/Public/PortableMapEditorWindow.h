#pragma once
#include "CoreMinimal.h"
PORTABLEMAPSEDITOR_API void RegisterPortableMapEditorWindow();
PORTABLEMAPSEDITOR_API void UnregisterPortableMapEditorWindow();
PORTABLEMAPSEDITOR_API bool OpenPortableMapEditorFile(const FString& File);
PORTABLEMAPSEDITOR_API void OpenPortableMapEditorWindow();

enum class EPortableMapEditorActor : uint8
{
    Boundary, PlayerStart, PointLight, SpotLight, DirectionalLight
};
PORTABLEMAPSEDITOR_API bool CanAddPortableMapEditorActor(EPortableMapEditorActor Kind, FText& Reason);
PORTABLEMAPSEDITOR_API bool AddPortableMapEditorActor(EPortableMapEditorActor Kind, FText& Reason);
PORTABLEMAPSEDITOR_API void RegisterPortableMapEditorToolbar();
PORTABLEMAPSEDITOR_API void UnregisterPortableMapEditorToolbar();
