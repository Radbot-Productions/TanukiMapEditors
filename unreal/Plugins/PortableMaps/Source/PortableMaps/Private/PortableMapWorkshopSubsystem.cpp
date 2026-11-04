#include "PortableMapWorkshopSubsystem.h"

UPortableMapWorkshopSubsystem::~UPortableMapWorkshopSubsystem() = default;
void UPortableMapWorkshopSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{ Super::Initialize(Collection); Client=MakeUnique<FPortableMapWorkshopClient>(); }
void UPortableMapWorkshopSubsystem::Deinitialize()
{ Client.Reset(); Super::Deinitialize(); }
void UPortableMapWorkshopSubsystem::Tick(float) { if (Client) Client->Tick(); }
bool UPortableMapWorkshopSubsystem::IsTickable() const { return !IsTemplate() && IsPending(); }
TStatId UPortableMapWorkshopSubsystem::GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(PortableMapWorkshop,STATGROUP_Tickables); }
bool UPortableMapWorkshopSubsystem::CanBegin(FString& Reason) const
{ if (Client) return Client->CanBegin(Reason); Reason=TEXT("Workshop subsystem is not initialized"); return false; }
void UPortableMapWorkshopSubsystem::SetAccessQuery(TFunction<bool()> Query) {if(Client) Client->SetAccessQuery(MoveTemp(Query));}
bool UPortableMapWorkshopSubsystem::Upload(const FString& File,const FString& Title,const FString& Preview,uint64 Item,FString& Reason)
{ return CanBegin(Reason) && Client->Upload(File,Title,Preview,Item,Reason); }
bool UPortableMapWorkshopSubsystem::Download(uint64 Item,const FString& Hash,FPortableMapDownloadComplete Complete,FString& Reason,UObject* Owner,TFunction<bool()> Current)
{ return CanBegin(Reason) && Client->Download(Item,Hash,MoveTemp(Complete),Reason,Owner,MoveTemp(Current)); }
void UPortableMapWorkshopSubsystem::CancelDownload(UObject* Owner) { if (Client) Client->CancelDownload(Owner); }
void UPortableMapWorkshopSubsystem::ReconcileDownloadContext(UObject* Owner) { if (Client) Client->ReconcileDownloadContext(Owner); }
TArray<uint64> UPortableMapWorkshopSubsystem::GetSubscribed(FString& Reason) const
{ return CanBegin(Reason) ? Client->GetSubscribed(Reason) : TArray<uint64>(); }
bool UPortableMapWorkshopSubsystem::IsPending() const { return Client && Client->IsPending(); }
bool UPortableMapWorkshopSubsystem::BrowseSubscribed(FPortableMapBrowseComplete Complete,FString& Reason,UObject* Owner,TFunction<bool()> Current)
{ return CanBegin(Reason) && Client->BrowseSubscribed(MoveTemp(Complete),Reason,Owner,MoveTemp(Current)); }
uint64 UPortableMapWorkshopSubsystem::GetItemId() const { return Client ? Client->GetItemId() : 0; }
FString UPortableMapWorkshopSubsystem::GetStatus() const { return Client ? Client->GetStatus() : FString(); }
