#pragma once
#include "CoreMinimal.h"

struct FPortableWorkshopOperation;
using FPortableMapDownloadComplete = TFunction<void(bool,const FString&,const FString&)>;
struct PORTABLEMAPS_API FPortableMapWorkshopItem
{
    uint64 Id=0;
    FString Title,PreviewUrl,Hash;
    bool bOwnedByCurrentUser=false;
};
using FPortableMapBrowseComplete = TFunction<void(bool,const TArray<FPortableMapWorkshopItem>&,const FString&)>;

/** Shared UGC operation owner. Its host owns the Steam session and callback pump. */
class PORTABLEMAPS_API FPortableMapWorkshopClient
{
public:
    FPortableMapWorkshopClient();
    ~FPortableMapWorkshopClient();
    void Tick();
    bool CanBegin(FString& Reason) const;
    void SetAccessQuery(TFunction<bool()> Query) {AccessQuery=MoveTemp(Query);}
    bool Upload(const FString& File,const FString& Title,const FString& Preview,uint64 ExistingItem,FString& Reason);
    bool Download(uint64 Item,const FString& ExpectedHash,FPortableMapDownloadComplete Complete,FString& Reason,
        UObject* RequestOwner=nullptr,TFunction<bool()> IsContextCurrent={});
    void CancelDownload(UObject* Owner);
    void ReconcileDownloadContext(UObject* Owner);
    TArray<uint64> GetSubscribed(FString& Reason) const;
    bool BrowseSubscribed(FPortableMapBrowseComplete Complete,FString& Reason,UObject* Owner=nullptr,TFunction<bool()> Current={});
    bool IsPending() const;
    bool WasSuccessful() const { return bSuccessful; }
    uint64 GetItemId() const;
    bool IsOwnedByCurrentUser() const;
    FString GetHash() const;
    FString GetInstalledFile() const;
    FString GetStatus() const { return Status; }
private:
    TUniquePtr<FPortableWorkshopOperation> Operation;
    FString Status;
    bool bSuccessful=false;
    TFunction<bool()> AccessQuery;
    void Finish(bool Success,const FString& Result,bool PreserveStaging=false);
};
