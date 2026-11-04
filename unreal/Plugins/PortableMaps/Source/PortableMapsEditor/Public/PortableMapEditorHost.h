#pragma once
#include "CoreMinimal.h"
class APortableMapAuthoringRoot;

/** Project integration for the shared authoring window. Empty callbacks use the standalone preview. */
struct FPortableMapEditorHost
{
    TFunction<bool()> HasAccess;
    TFunction<bool(const FString&,FString&)> Start;
    TFunction<bool(FString&)> StartEdited;
    TFunction<FString()> GetStatus;
    TFunction<void()> RegisterAuthoringPlay;
    TFunction<void()> Shutdown;
    TFunction<void(APortableMapAuthoringRoot*)> ConfigureReferences;
};
namespace PortableMapEditorHost
{
    PORTABLEMAPSEDITOR_API void Configure(FPortableMapEditorHost Host);
    PORTABLEMAPSEDITOR_API bool HasAccess();
    PORTABLEMAPSEDITOR_API bool Start(const FString& File,FString& Error);
    PORTABLEMAPSEDITOR_API bool StartEdited(FString& Error);
    PORTABLEMAPSEDITOR_API FString GetStatus();
    PORTABLEMAPSEDITOR_API void RegisterAuthoringPlay();
    PORTABLEMAPSEDITOR_API void Shutdown();
    PORTABLEMAPSEDITOR_API void ConfigureReferences(APortableMapAuthoringRoot* Root);
}
