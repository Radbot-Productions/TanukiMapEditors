#pragma once
#include "CoreMinimal.h"
namespace PortableMapGamePlayer
{
PORTABLEMAPSEDITOR_API bool Start(const FString& File,FString& Error);
PORTABLEMAPSEDITOR_API bool StartEdited(FString& Error);
PORTABLEMAPSEDITOR_API FString GetStatus();
PORTABLEMAPSEDITOR_API void Shutdown();
}
