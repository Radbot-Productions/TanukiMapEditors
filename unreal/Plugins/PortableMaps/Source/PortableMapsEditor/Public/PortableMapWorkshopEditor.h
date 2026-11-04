#pragma once
#include "CoreMinimal.h"
struct FPortableMapWorkshopEditorResult
{
    bool Success=false;
    bool Owned=false;
    FString Message,ItemId,Hash,File;
    TArray<FString> Items,Titles,OwnedItems,OwnedTitles;
};
namespace PortableMapWorkshopEditor
{
PORTABLEMAPSEDITOR_API bool Start(const FString& Action,const FString& File,const FString& Title,const FString& Preview,const FString& Item,
    TFunction<void(const FPortableMapWorkshopEditorResult&)> Complete,FString& OutReason);
PORTABLEMAPSEDITOR_API bool IsPending();
PORTABLEMAPSEDITOR_API FString GetStatus();
PORTABLEMAPSEDITOR_API bool Connect(FString& OutReason);
PORTABLEMAPSEDITOR_API bool IsConnected();
PORTABLEMAPSEDITOR_API FString GetAccount();
PORTABLEMAPSEDITOR_API FString GetRememberedItem(const FString& ContentId);
PORTABLEMAPSEDITOR_API bool RememberOwnedMap(const FString& File);
PORTABLEMAPSEDITOR_API void Poll();
PORTABLEMAPSEDITOR_API void Shutdown();
}
