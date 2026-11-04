#pragma once
#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "PortableMapWorkshopClient.h"
#include "PortableMapWorkshopSubsystem.generated.h"

/** The consumer app owns its Workshop. Visibility is configured in Steamworks, never widened by this plugin. */
UCLASS(Config=Game, DefaultConfig)
class PORTABLEMAPS_API UPortableMapWorkshopSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    UPROPERTY(Config, EditAnywhere, Category="Workshop") int64 ConsumerAppId = 0;
    UPROPERTY(Config, EditAnywhere, Category="Workshop") bool bDeveloperOnlyWorkshopVerified = false;
};

/** Uses the online subsystem's existing Steam client and callback pump; never initializes or shuts Steam down. */
UCLASS()
class PORTABLEMAPS_API UPortableMapWorkshopSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
    GENERATED_BODY()
public:
    virtual ~UPortableMapWorkshopSubsystem() override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;
    virtual bool IsTickable() const override;
    virtual TStatId GetStatId() const override;
    bool CanBegin(FString& OutReason) const;
    void SetAccessQuery(TFunction<bool()> Query);
    bool Upload(const FString& File, const FString& Title, const FString& PreviewPng, uint64 ExistingItem, FString& OutReason);
    bool Download(uint64 Item, const FString& ExpectedHash, FPortableMapDownloadComplete Complete, FString& OutReason,
        UObject* RequestOwner = nullptr, TFunction<bool()> IsContextCurrent = {});
    void CancelDownload(UObject* RequestOwner);
    void ReconcileDownloadContext(UObject* RequestOwner);
    TArray<uint64> GetSubscribed(FString& OutReason) const;
    bool BrowseSubscribed(FPortableMapBrowseComplete Complete,FString& Reason,UObject* Owner=nullptr,TFunction<bool()> Current={});
    bool IsPending() const;
    uint64 GetItemId() const;
    FString GetStatus() const;
private:
    TUniquePtr<FPortableMapWorkshopClient> Client;
};
