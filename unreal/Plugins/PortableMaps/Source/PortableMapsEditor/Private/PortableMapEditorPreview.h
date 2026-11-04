#pragma once
#include "CoreMinimal.h"
namespace PortableMapEditorPreview
{
    bool Start(const FString& File,FString& Error);
    bool StartEdited(FString& Error);
    FString GetStatus();
    void RegisterAuthoringPlay();
    void Shutdown();
}
