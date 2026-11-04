#include "PortableMapEditorWindow.h"
#include "PortableMapEditorHost.h"
#include "PortableMapGamePlayer.h"
#include "PortableMapWorkshopEditor.h"
#include "Interfaces/IPluginManager.h"

#include "AdvancedPreviewScene.h"
#include "DesktopPlatformModule.h"
#include "EditorViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "IDesktopPlatform.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "ImageUtils.h"
#include "Slate/SceneViewport.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "UObject/StrongObjectPtr.h"
#include "RenderingThread.h"
#include "Misc/ScopeExit.h"
#include "PortableMapScene.h"
#include "PortableMapAuthoring.h"
#include "PortableMapLighting.h"
#include "Components/LightComponent.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "PrimitiveDrawInterface.h"
#include "SEditorViewport.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SScrollBox.h"
#include "GameFramework/PlayerStart.h"
#include "EngineUtils.h"
#include "ScopedTransaction.h"
#include "PropertyEditorModule.h"
#include "IDetailCustomization.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "TanukiPortableMapViewer"

namespace TanukiPortableViewer
{
const FName TabId(TEXT("TanukiPortableMapViewer"));

class FViewportClient final : public FEditorViewportClient
{
public:
    using FEditorViewportClient::FEditorViewportClient;
    TWeakObjectPtr<APortableMapScene> Map;

    virtual FLinearColor GetBackgroundColor() const override { return FLinearColor(0.11f, 0.15f, 0.20f); }
    virtual void Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI) override
    {
        FEditorViewportClient::Draw(View, PDI);
        if (!Map.IsValid()) return;
        for (const FPortableMapMarker& Marker : Map->GetMarkers())
        {
            const FVector Center = Marker.Transform.GetLocation();
            const FVector Forward = Marker.Transform.GetRotation().GetForwardVector();
            const FVector Right = Marker.Transform.GetRotation().GetRightVector();
            const FLinearColor Color(0.1f, 1.f, 0.7f);
            // Capsule-sized wire box and facing arrow; markers are preview overlays only.
            for (int32 Axis = 0; Axis < 3; ++Axis)
                for (int32 A : {-1, 1}) for (int32 B : {-1, 1})
                {
                    FVector Start(40, 40, 96), End(40, 40, 96);
                    Start[(Axis + 1) % 3] *= A; End[(Axis + 1) % 3] *= A;
                    Start[(Axis + 2) % 3] *= B; End[(Axis + 2) % 3] *= B;
                    Start[Axis] *= -1;
                    PDI->DrawLine(Center + Start, Center + End, Color, SDPG_World, 2.f);
                }
            const FVector Tip = Center + Forward * 150;
            PDI->DrawLine(Center, Tip, Color, SDPG_World, 3.f);
            PDI->DrawLine(Tip, Tip - Forward * 35 + Right * 25, Color, SDPG_World, 3.f);
            PDI->DrawLine(Tip, Tip - Forward * 35 - Right * 25, Color, SDPG_World, 3.f);
        }
    }
};

class SMapViewport final : public SEditorViewport
{
public:
    SLATE_BEGIN_ARGS(SMapViewport) {} SLATE_END_ARGS()
    void Construct(const FArguments&)
    {
        Scene = MakeShared<FAdvancedPreviewScene>(FPreviewScene::ConstructionValues());
        Scene->SetFloorVisibility(false, true);
        Scene->SetEnvironmentVisibility(false, true);
        SEditorViewport::Construct(SEditorViewport::FArguments());
    }
    virtual ~SMapViewport() override
    {
        if (Map.IsValid()) { Map->Clear(); Map->Destroy(); }
        if (Client.IsValid()) Client->Viewport = nullptr;
    }
    bool Load(const FString& Filename, FString& Error)
    {
        if (!Map.IsValid())
        {
            FActorSpawnParameters Params;
            Params.ObjectFlags = RF_Transient;
            Map = Scene->GetWorld()->SpawnActor<APortableMapScene>(Params);
            Client->Map = Map;
        }
        if (!Map.IsValid()) { Error = TEXT("Could not create preview scene actor"); return false; }
        if (!Map->LoadFile(Filename, Error)) return false;
        Map->SetContentActive(true);
        Frame();
        return true;
    }
    void Frame(double OverrideAspect=0)
    {
        if (!Map.IsValid() || !Map->IsLoaded()) return;
        const FBox Bounds = Map->GetComponentsBoundingBox(true);
        if (Bounds.IsValid)
        {
            // Fit all eight bounds corners in camera space. A new dock tab may
            // not have a sized viewport yet, so use a conservative initial aspect.
            const FIntPoint Size = Client->Viewport ? Client->Viewport->GetSizeXY() : FIntPoint::ZeroValue;
            const double Aspect = OverrideAspect>0 ? OverrideAspect : Size.X > 0 && Size.Y > 0 ? double(Size.X) / Size.Y : 2.0;
            const double TanHorizontal = FMath::Tan(FMath::DegreesToRadians(Client->ViewFOV * .5));
            const double TanVertical = TanHorizontal / Aspect;
            const FQuat Rotation = Client->GetViewRotation().Quaternion();
            double Distance = 100.;
            for (int32 Corner = 0; Corner < 8; ++Corner)
            {
                const FVector Offset = FVector(Corner & 1 ? Bounds.Max.X : Bounds.Min.X,
                    Corner & 2 ? Bounds.Max.Y : Bounds.Min.Y, Corner & 4 ? Bounds.Max.Z : Bounds.Min.Z) - Bounds.GetCenter();
                const double Depth = FVector::DotProduct(Offset, Rotation.GetForwardVector());
                Distance = FMath::Max(Distance, FMath::Abs(FVector::DotProduct(Offset, Rotation.GetRightVector())) / TanHorizontal - Depth);
                Distance = FMath::Max(Distance, FMath::Abs(FVector::DotProduct(Offset, Rotation.GetUpVector())) / TanVertical - Depth);
            }
            Distance *= 1.12;
            Client->SetViewLocation(Bounds.GetCenter() - Client->GetViewRotation().Vector() * Distance);
            Client->SetLookAtLocation(Bounds.GetCenter());
        }
        Invalidate();
    }
    APortableMapScene* GetMap() const { return Map.Get(); }
    UWorld* GetPreviewWorld() const { return Scene->GetWorld(); }
    bool CaptureThumbnail(FString& File,FString& Error)
    {
        if (!Map.IsValid() || !Map->IsLoaded() || !Client->Viewport) { Error=TEXT("Load a map before generating its preview"); return false; }
        Frame(1.6);
        TStrongObjectPtr<UTextureRenderTarget2D> Target(NewObject<UTextureRenderTarget2D>(GetTransientPackage()));
        Target->InitCustomFormat(640,400,PF_B8G8R8A8,false);Target->TargetGamma=2.2f;Target->UpdateResourceImmediate(true);
        FActorSpawnParameters Params;Params.ObjectFlags=RF_Transient;
        auto* CameraActor=Scene->GetWorld()->SpawnActor<AActor>(Params);
        if(!CameraActor){Error=TEXT("Cannot create preview capture");return false;}
        ON_SCOPE_EXIT {CameraActor->Destroy();};
        auto* Capture=NewObject<USceneCaptureComponent2D>(CameraActor);CameraActor->SetRootComponent(Capture);CameraActor->AddInstanceComponent(Capture);
        Capture->bCaptureEveryFrame=false;Capture->bCaptureOnMovement=false;Capture->TextureTarget=Target.Get();
        Capture->CaptureSource=ESceneCaptureSource::SCS_FinalColorLDR;Capture->FOVAngle=Client->ViewFOV;
        Capture->RegisterComponent();CameraActor->SetActorLocationAndRotation(Client->GetViewLocation(),Client->GetViewRotation());
        Capture->CaptureScene();FlushRenderingCommands();
        const FIntPoint Size(640,400);TArray<FColor> Pixels;
        auto* Resource=Target->GameThread_GetRenderTargetResource();
        if(!Resource || !Resource->ReadPixels(Pixels) || Pixels.Num()!=Size.X*Size.Y)
        {Error=TEXT("Cannot capture the framed map scene");return false;}
        const double Scale=FMath::Min(640.0/Size.X,400.0/Size.Y);
        const int32 Width=FMath::Max(1,FMath::RoundToInt(Size.X*Scale)),Height=FMath::Max(1,FMath::RoundToInt(Size.Y*Scale));
        TArray<FColor> Reduced; FImageUtils::ImageResize(Size.X,Size.Y,Pixels,Width,Height,Reduced,false);
        for (FColor& Pixel : Reduced) Pixel.A=255;
        TArray64<uint8> Png; FImageUtils::PNGCompressImageArray(Width,Height,Reduced,Png);
        if(File.IsEmpty())File=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("PortableMaps/Thumbnails")/(FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT(".png")));
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(File),true);
        if (Png.IsEmpty() || Png.Num()>=1024*1024 || !FFileHelper::SaveArrayToFile(Png,*File))
        { Error=TEXT("Cannot write a Workshop preview smaller than 1 MiB"); return false; }
        UE_LOG(LogTemp,Display,TEXT("PORTABLE_MAP_THUMBNAIL file=%s width=%d height=%d"),*File,Width,Height);
        return true;
    }
protected:
    virtual TSharedPtr<SWidget> BuildViewportToolbar() override { return nullptr; }
    virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override
    {
        Client = MakeShared<FViewportClient>(nullptr, Scene.Get(), SharedThis(this));
        Client->SetViewMode(VMI_Lit);
        Client->SetRealtime(true);
        Client->EngineShowFlags.SetGrid(false);
        Client->EngineShowFlags.SetSelectionOutline(false);
        Client->SetViewLocation(FVector(2800, -2800, 2200));
        Client->SetViewRotation(FRotator(-27, 135, 0));
        return Client.ToSharedRef();
    }
private:
    TSharedPtr<FAdvancedPreviewScene> Scene;
    TSharedPtr<FViewportClient> Client;
    TWeakObjectPtr<APortableMapScene> Map;
};

class SViewer final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SViewer) {} SLATE_END_ARGS()
    virtual void Tick(const FGeometry& Geometry,double Time,float Delta) override
    { SCompoundWidget::Tick(Geometry,Time,Delta); PortableMapWorkshopEditor::Poll(); }
    void Construct(const FArguments&)
    {
        Viewport=SNew(SMapViewport); // Used only to generate automatic Workshop thumbnails.
        Status=FText::GetEmpty();
        auto Button=[](FText Text,TFunction<FReply()> Action)->TSharedRef<SWidget>
        {return SNew(SButton).Text(Text).OnClicked_Lambda(MoveTemp(Action));};
        auto NewPage=SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight().Padding(8)[Button(LOCTEXT("CreateEmpty","Create Empty Map"),[this]{CreateEmpty();return FReply::Handled();})]
            +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SComboButton).ButtonContent()[SNew(STextBlock).Text(LOCTEXT("StartSample","Start From Sample"))]
                .OnGetMenuContent_Lambda([this]{FMenuBuilder Menu(true,nullptr);
                    Menu.AddMenuEntry(LOCTEXT("RoomSample","Room"),FText::GetEmpty(),FSlateIcon(),FUIAction(FExecuteAction::CreateLambda([this]{OpenSample(TEXT("Room"));})));
                    Menu.AddMenuEntry(LOCTEXT("CourtyardSample","Courtyard"),FText::GetEmpty(),FSlateIcon(),FUIAction(FExecuteAction::CreateLambda([this]{OpenSample(TEXT("Courtyard"));})));
                    return Menu.MakeWidget();})];
        auto OpenPage=SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(STextBlock).Text(LOCTEXT("SteamWorkshop","Steam Workshop"))]
            +SVerticalBox::Slot().AutoHeight().Padding(8)[Button(LOCTEXT("RefreshMaps","Refresh Subscribed Maps"),[this]{WorkshopAction(TEXT("list"));return FReply::Handled();})]
            +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SComboButton).ButtonContent()[SNew(STextBlock).Text(LOCTEXT("SelectWorkshopMap","Choose Workshop Map"))]
                .OnGetMenuContent_Lambda([this]{FMenuBuilder Menu(true,nullptr);for(int32 I=0;I<SubscribedItems.Num();++I){FString Id=SubscribedItems[I];
                    Menu.AddMenuEntry(FText::AsCultureInvariant(SubscribedTitles.IsValidIndex(I)?SubscribedTitles[I]:Id),FText::GetEmpty(),FSlateIcon(),FUIAction(FExecuteAction::CreateLambda([this,Id]{WorkshopItem=Id;WorkshopAction(TEXT("edit"));})));}return Menu.MakeWidget();})]
            +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SEditableTextBox).HintText(LOCTEXT("AdvancedItemId","Workshop item ID (optional)")).Text_Lambda([this]{return FText::AsCultureInvariant(WorkshopItem);}).OnTextChanged_Lambda([this](const FText& T){WorkshopItem=T.ToString();})]
            +SVerticalBox::Slot().AutoHeight().Padding(8)[Button(LOCTEXT("OpenById","Open by ID"),[this]{WorkshopAction(TEXT("edit"));return FReply::Handled();})]
            +SVerticalBox::Slot().AutoHeight().Padding(8)[Button(LOCTEXT("OpenMapFile","Open Map File"),[this]{BrowseForEditing();return FReply::Handled();})];
        auto ExportPage=SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(STextBlock).Text_Lambda([]{auto* R=CurrentRoot();return R?FText::AsCultureInvariant(R->GetActorLabel()):LOCTEXT("SelectRoot","Open or create a map first.");})]
            +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SEditableTextBox).HintText(LOCTEXT("WorkshopMapTitle","Title")).Text_Lambda([this]{auto* R=CurrentRoot();return FText::AsCultureInvariant(WorkshopTitle.IsEmpty()&&R?R->GetActorLabel():WorkshopTitle);}).OnTextChanged_Lambda([this](const FText& T){WorkshopTitle=T.ToString();})]
            +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SCheckBox).IsChecked_Lambda([this]{return bCreateWorkshopItem?ECheckBoxState::Unchecked:ECheckBoxState::Checked;}).OnCheckStateChanged_Lambda([this](ECheckBoxState C){bCreateWorkshopItem=C!=ECheckBoxState::Checked;})[SNew(STextBlock).Text(LOCTEXT("UpdateExistingItem","Update Existing Workshop Item"))]]
            +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SCheckBox).IsChecked_Lambda([this]{return bCreateWorkshopItem?ECheckBoxState::Checked:ECheckBoxState::Unchecked;}).OnCheckStateChanged_Lambda([this](ECheckBoxState C){bCreateWorkshopItem=C==ECheckBoxState::Checked;})[SNew(STextBlock).Text(LOCTEXT("CreateNewItem","Create New Workshop Item"))]]
            +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SComboButton).Visibility_Lambda([this]{return bCreateWorkshopItem?EVisibility::Collapsed:EVisibility::Visible;}).ButtonContent()[SNew(STextBlock).Text(LOCTEXT("ChooseOwnedItem","Choose My Workshop Item"))]
                .OnGetMenuContent_Lambda([this]{FMenuBuilder Menu(true,nullptr);
                    Menu.AddMenuEntry(LOCTEXT("RefreshOwnedItems","Refresh My Items"),FText::GetEmpty(),FSlateIcon(),FUIAction(FExecuteAction::CreateLambda([this]{WorkshopAction(TEXT("list"));})));
                    for(int32 I=0;I<OwnedItems.Num();++I){FString Id=OwnedItems[I];Menu.AddMenuEntry(FText::AsCultureInvariant(OwnedTitles[I]),FText::GetEmpty(),FSlateIcon(),FUIAction(FExecuteAction::CreateLambda([this,Id]{WorkshopItem=Id;})));}return Menu.MakeWidget();})]
            +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SEditableTextBox).Visibility_Lambda([this]{return bCreateWorkshopItem?EVisibility::Collapsed:EVisibility::Visible;}).HintText(LOCTEXT("UpdateItemId","Existing Workshop Item ID")).Text_Lambda([this]{return FText::AsCultureInvariant(WorkshopItem);}).OnTextChanged_Lambda([this](const FText& T){WorkshopItem=T.ToString();})]
            +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SEditableTextBox).HintText(LOCTEXT("PreviewPng","Preview PNG (optional)")).Text_Lambda([this]{return FText::AsCultureInvariant(PreviewImage);}).OnTextChanged_Lambda([this](const FText& T){PreviewImage=T.ToString();})]
            +SVerticalBox::Slot().AutoHeight().Padding(8)[Button(LOCTEXT("ExportWorkshop","Export to Workshop"),[this]{UploadEdited();return FReply::Handled();})]
            +SVerticalBox::Slot().AutoHeight().Padding(8)[Button(LOCTEXT("ExportFile","Export Map File"),[this]{ExportEdited();return FReply::Handled();})]
            +SVerticalBox::Slot().AutoHeight().Padding(8)[Button(LOCTEXT("RepairMaterials","Repair Materials"),[this]{FString Report;bError=!PortableMapAuthoring::RepairMaterials(CurrentRoot(),Report);Status=FText::AsCultureInvariant(Report);return FReply::Handled();})];
        ChildSlot[SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SHorizontalBox)
                +SHorizontalBox::Slot().FillWidth(1)[Button(LOCTEXT("NewTab","New"),[this]{SetTab(0);return FReply::Handled();})]
                +SHorizontalBox::Slot().FillWidth(1)[Button(LOCTEXT("OpenTab","Open"),[this]{SetTab(1);return FReply::Handled();})]
                +SHorizontalBox::Slot().FillWidth(1)[Button(LOCTEXT("ExportTab","Export"),[this]{SetTab(2);return FReply::Handled();})]]
            +SVerticalBox::Slot().FillHeight(1)[SNew(SScrollBox)+SScrollBox::Slot()[SAssignNew(Pages,SWidgetSwitcher)+SWidgetSwitcher::Slot()[NewPage]+SWidgetSwitcher::Slot()[OpenPage]+SWidgetSwitcher::Slot()[ExportPage]]]
            +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SHorizontalBox)
                +SHorizontalBox::Slot().AutoWidth()[Button(LOCTEXT("PlayMap","Play in Tanuki"),[this]{PlayEdited();return FReply::Handled();})]
            ]
            +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(STextBlock).Text_Lambda([this]{return Status;}).AutoWrapText(true)]
            +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(STextBlock).Text_Lambda([]{return FText::AsCultureInvariant(PortableMapWorkshopEditor::IsPending()?PortableMapWorkshopEditor::GetStatus():PortableMapEditorHost::GetStatus());}).AutoWrapText(true)]];
        FString ConnectionReason;PortableMapWorkshopEditor::Connect(ConnectionReason);
    }
    void SetTab(int32 Index)
    {
        Pages->SetActiveWidgetIndex(Index);
        if(Index==2) if(auto* Root=CurrentRoot()) {WorkshopItem=Root->WorkshopItemId;bCreateWorkshopItem=WorkshopItem.IsEmpty();WorkshopTitle=Root->GetActorLabel();}
    }
    static APortableMapAuthoringRoot* CurrentRoot() {return GEditor?PortableMapAuthoring::FindRoot(GEditor->GetEditorWorldContext().World()):nullptr;}
    static void AddLight(const FString& Type)
    {
        auto* Root=CurrentRoot();if(!Root || GEditor->PlayWorld) return;
        FScopedTransaction Transaction(LOCTEXT("AddPortableLight","Add Map Light"));
        FActorSpawnParameters Params;Params.ObjectFlags=RF_Transactional;
        auto* Actor=Root->GetWorld()->SpawnActor<AActor>(Params);
        FPortableMapLight V;V.Type=Type;V.Frame.SetLocation(FVector(0,0,300));
        auto* C=PortableMapLighting::Create(Actor,V);C->SetFlags(RF_Transactional);Actor->SetRootComponent(C);Actor->AddInstanceComponent(C);C->RegisterComponent();C->SetVisibility(true);
        Actor->AttachToActor(Root,FAttachmentTransformRules::KeepRelativeTransform);Actor->SetActorLabel(Type+TEXT(" Light"));
        GEditor->SelectNone(false,true);GEditor->SelectActor(Actor,true,true);
    }
    static void AddComponent(bool Boundary)
    {
        auto* Root=CurrentRoot(); if(!Root || GEditor->PlayWorld) return;
        FScopedTransaction Transaction(LOCTEXT("AddMapComponent","Add Map Component"));
        AActor* Actor=nullptr;
        if(Boundary) {TArray<AActor*> Children;Root->GetAttachedActors(Children,true,true);for(auto* A:Children) if(A->IsA<APortableMapAuthoringBoundary>()){Actor=A;break;}
            if(!Actor) {Actor=Root->GetWorld()->SpawnActor<APortableMapAuthoringBoundary>();Actor->AttachToActor(Root,FAttachmentTransformRules::KeepRelativeTransform);Actor->SetActorRelativeTransform(FTransform(FQuat::Identity,FVector(0,0,1000),FVector(60,60,20)));Actor->SetActorLabel(TEXT("Map Boundary"));}}
        else {Actor=Root->GetWorld()->SpawnActor<APlayerStart>();Actor->AttachToActor(Root,FAttachmentTransformRules::KeepRelativeTransform);Actor->SetActorRelativeLocation(FVector(0,0,100));Actor->SetActorLabel(TEXT("Player Start"));}
        Actor->SetFlags(RF_Transactional);GEditor->SelectNone(false,true);GEditor->SelectActor(Actor,true,true);
    }
    void CreateEmpty()
    {
        PortableMapEditorHost::RegisterAuthoringPlay();
        if(!GEditor || GEditor->IsPlaySessionInProgress()) return;
        auto* World=UEditorLoadingAndSavingUtils::NewBlankMap(false);if(!World) return;
        auto* Root=PortableMapAuthoring::CreateStarter(World);if(!Root) return;
        ++SelectionRevision;GEditor->SelectNone(false,true);GEditor->SelectActor(Root,true,true);Status=FText::GetEmpty();
    }
    void OpenSample(const FString& Name) {OpenForEditing(IPluginManager::Get().FindPlugin(TEXT("PortableMaps"))->GetBaseDir()/TEXT("Samples")/(Name+TEXT(".tanukimap")),true);}
    void BrowseForEditing()
    {
        TArray<FString> Files;auto* Desktop=FDesktopPlatformModule::Get();
        if(Desktop && Desktop->OpenFileDialog(FSlateApplication::Get().FindBestParentWindowHandleForDialogs(AsShared()),LOCTEXT("OpenMapDialog","Open Map").ToString(),TEXT(""),TEXT(""),TEXT("Tanuki Map|*.tanukimap"),EFileDialogFlags::None,Files) && !Files.IsEmpty()) OpenForEditing(Files[0],false);
    }
    void PlayEdited()
    {
        FString Error;if(!PortableMapEditorHost::StartEdited(Error)){Status=FText::AsCultureInvariant(Error);bError=true;}
    }
    bool OpenEditableMap(const FString& File){return OpenForEditing(File,false);}
    bool SaveFramedPreview(FString& File,FString& Error) { return Viewport->CaptureThumbnail(File,Error); }
    void DownloadAndPlay(const FString& Item) { WorkshopItem=Item; WorkshopAction(TEXT("download")); }
    bool LoadFile(const FString& Filename)
    {
        FString Error;
        const FString FullPath = FPaths::ConvertRelativePathToFull(Filename);
        bError = !Viewport->Load(FullPath, Error);
        if (bError)
        {
            Status = FText::FormatNamed(LOCTEXT("LoadFailed", "Could not open {File}: {Reason}. The previous preview is retained."),
                TEXT("File"), FText::AsCultureInvariant(FullPath), TEXT("Reason"), FText::AsCultureInvariant(Error));
            UE_LOG(LogTemp, Warning, TEXT("PORTABLE_MAP_VIEWER_REJECTED file=%s reason=%s"), *FullPath, *Error);
            return false;
        }
        LoadedFile = FullPath;
        ++SelectionRevision;
        WorkshopTitle=FPaths::GetBaseFilename(FullPath);
        WorkshopItem=PortableMapWorkshopEditor::GetRememberedItem(Viewport->GetMap()->GetContentId());
        Status = FText::FormatNamed(LOCTEXT("Loaded", "Loaded {File} | {Starts} player starts"),
            TEXT("File"), FText::AsCultureInvariant(LoadedFile), TEXT("Starts"), Viewport->GetMap()->GetMarkers().Num());
        UE_LOG(LogTemp, Display, TEXT("PORTABLE_MAP_VIEWER_READY world=%s content=%s starts=%d"),
            *Viewport->GetPreviewWorld()->GetName(), *Viewport->GetMap()->GetContentId(), Viewport->GetMap()->GetMarkers().Num());
        return true;
    }
private:
    void ChooseWorkshopItem(FString Id) { WorkshopItem=MoveTemp(Id); }
    void WorkshopAction(const FString& Action)
    {
        FString Reason;
        FString Image=PreviewImage;
        if (Action==TEXT("upload") && Image.TrimStartAndEnd().IsEmpty() && !SaveFramedPreview(Image,Reason))
        { bError=true; Status=FText::AsCultureInvariant(Reason); return; }
        const uint64 Revision=SelectionRevision;
        const TWeakObjectPtr<APortableMapAuthoringRoot> RequestRoot=CurrentRoot();
        const FString RequestMapId=RequestRoot.IsValid()?RequestRoot->MapId:FString();
        const TWeakPtr<SViewer> Weak=SharedThis(this);
        const bool Accepted=PortableMapWorkshopEditor::Start(Action==TEXT("edit")?TEXT("download"):Action,LoadedFile,WorkshopTitle,Image,Action==TEXT("upload")&&bCreateWorkshopItem?FString():WorkshopItem,
            [Weak,Revision,Action,RequestRoot,RequestMapId](const FPortableMapWorkshopEditorResult& Result)
            {
                auto Viewer=Weak.Pin(); if (!Viewer) return;
                Viewer->bError=!Result.Success; Viewer->Status=FText::AsCultureInvariant(Result.Message);
                if (Action==TEXT("list") && Result.Success) {Viewer->SubscribedItems=Result.Items;Viewer->SubscribedTitles=Result.Titles;Viewer->OwnedItems=Result.OwnedItems;Viewer->OwnedTitles=Result.OwnedTitles;}
                if (Viewer->SelectionRevision!=Revision) return;
                if(Action==TEXT("edit") && Result.Success)
                {
                    if(Result.Owned) PortableMapWorkshopEditor::RememberOwnedMap(Result.File);
                    if(!Viewer->OpenForEditing(Result.File,!Result.Owned)) return;
                    if(auto* Root=CurrentRoot()) {Root->Modify();Root->WorkshopItemId=Result.Owned?Result.ItemId:FString();Viewer->WorkshopItem=Root->WorkshopItemId;}
                    return;
                }
                if (!Result.ItemId.IsEmpty() && Result.ItemId!=TEXT("0")) Viewer->WorkshopItem=Result.ItemId;
                if(Action==TEXT("upload") && Result.Success && RequestRoot.IsValid() && RequestRoot->MapId==RequestMapId){RequestRoot->Modify();RequestRoot->WorkshopItemId=Result.ItemId;Viewer->bCreateWorkshopItem=false;}
                if (Action==TEXT("download") && Result.Success && Viewer->LoadFile(Result.File))
                {
                    Viewer->WorkshopItem=Result.ItemId;
                    FString Reason;
                    if (!PortableMapEditorHost::Start(Result.File,Reason)) { Viewer->bError=true; Viewer->Status=FText::AsCultureInvariant(Reason); }
                }
            },Reason);
        if (!Accepted) { bError=true; Status=FText::AsCultureInvariant(Reason); }
    }
    FReply Browse()
    {
        TArray<FString> Files;
        IDesktopPlatform* Desktop = FDesktopPlatformModule::Get();
        if (Desktop && Desktop->OpenFileDialog(FSlateApplication::Get().FindBestParentWindowHandleForDialogs(AsShared()),
            LOCTEXT("DialogTitle", "Open Portable Map").ToString(), FPaths::GetPath(LoadedFile), TEXT(""),
            TEXT("Tanuki portable map (*.tanukimap)|*.tanukimap"), EFileDialogFlags::None, Files) && !Files.IsEmpty())
            LoadFile(Files[0]);
        return FReply::Handled();
    }
    bool OpenForEditing(const FString& File,bool Fork)
    {
        PortableMapEditorHost::RegisterAuthoringPlay();
        if(!GEditor || GEditor->PlayWorld || File.IsEmpty()) {bError=true; Status=LOCTEXT("EditUnavailable","Open a map file outside PIE first."); return false;}
        FString Error; UWorld* World=nullptr;
        FString Folder=TEXT("/Game/CustomMaps/Imported_")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
        if(!PortableMapAuthoring::Import(File,Folder,Fork,World,Error)) {bError=true; Status=FText::AsCultureInvariant(Error); return false;}
        // Loading replaces the imported world and may collect it. Use only the
        // world returned by LoadMap after that transition.
        const FString PackageName=World->GetPackage()->GetName();
        // New/Open intentionally closes the current scene without saving it.
        // Other dirty content assets are not saved or discarded by this action.
        if(!UEditorLoadingAndSavingUtils::NewBlankMap(false))return false;
        UWorld* LoadedWorld=UEditorLoadingAndSavingUtils::LoadMap(PackageName);
        if(!LoadedWorld) {bError=true; Status=LOCTEXT("OpenImportedFailed","The imported assets were saved, but the level could not be opened."); return false;}
        auto* Root=PortableMapAuthoring::FindRoot(LoadedWorld);
        if(!Root) {bError=true; Status=LOCTEXT("ChooseAuthoringRoot","The current level must contain exactly one Portable Map Root."); return false;}
        LoadedFile=File; WorkshopTitle=Root->GetActorLabel();
        GEditor->SelectNone(false,true);GEditor->SelectActor(Root,true,true);
        ++SelectionRevision;bError=false;Status=FText::GetEmpty();return true;
    }
    void ExportEdited()
    {
        auto* Root=GEditor ? PortableMapAuthoring::FindRoot(GEditor->GetEditorWorldContext().World()) : nullptr;
        if(!Root) {bError=true; Status=LOCTEXT("ChooseAuthoringRoot","The current level must contain exactly one Portable Map Root."); return;}
        TArray<FString> Files; auto* Desktop=FDesktopPlatformModule::Get();
        if(!Desktop || !Desktop->SaveFileDialog(FSlateApplication::Get().FindBestParentWindowHandleForDialogs(AsShared()),LOCTEXT("SaveEdited","Export Edited Map").ToString(),FPaths::GetPath(Root->ExportFile.IsEmpty()?LoadedFile:Root->ExportFile),Root->GetActorLabel()+TEXT(".tanukimap"),TEXT("Tanuki map|*.tanukimap"),EFileDialogFlags::None,Files) || Files.IsEmpty()) return;
        FString Error;
        if(!PortableMapAuthoring::Export(Root,Files[0],Error)) {bError=true; Status=FText::AsCultureInvariant(Error); return;}
        Root->Modify();Root->ExportFile=Files[0];Status=LOCTEXT("ExportComplete","Map exported.");
    }
    void UploadEdited()
    {
        auto* Root=GEditor ? PortableMapAuthoring::FindRoot(GEditor->GetEditorWorldContext().World()) : nullptr;
        if(!Root || GEditor->PlayWorld) {bError=true; Status=LOCTEXT("ChooseAuthoringRoot","The current level must contain exactly one Portable Map Root."); return;}
        FString File=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("PortableMaps/EditorUploads")/FGuid::NewGuid().ToString(EGuidFormats::Digits)/TEXT("map.tanukimap"));
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(File),true); FString Error,Title=WorkshopTitle.IsEmpty()?Root->GetActorLabel():WorkshopTitle;
        const FString TargetItem=WorkshopItem;
        if(!PortableMapAuthoring::Export(Root,File,Error) || !LoadFile(File)) {bError=true; if(!Error.IsEmpty()) Status=FText::AsCultureInvariant(Error); return;}
        WorkshopItem=TargetItem; if(!Title.IsEmpty()) WorkshopTitle=Title; WorkshopAction(TEXT("upload"));
    }
    TSharedPtr<SMapViewport> Viewport;
    TSharedPtr<SWidgetSwitcher> Pages;
    FString LoadedFile;
    FText Status;
    bool bError = false;
    FString WorkshopTitle,PreviewImage,WorkshopItem;
    TArray<FString> SubscribedItems,SubscribedTitles,OwnedItems,OwnedTitles;
    bool bCreateWorkshopItem=true;
    uint64 SelectionRevision=0;

};

TWeakPtr<SViewer> ActiveViewer;
TSharedPtr<SDockTab> OpenTab()
{
    if(!PortableMapEditorHost::HasAccess()) return nullptr;
    return FGlobalTabmanager::Get()->TryInvokeTab(TabId);
}
class FMapRootDetails : public IDetailCustomization
{
public:
    static TSharedRef<IDetailCustomization> MakeInstance(){return MakeShared<FMapRootDetails>();}
    virtual void CustomizeDetails(IDetailLayoutBuilder& Builder) override
    {
        TArray<TWeakObjectPtr<UObject>> Objects;Builder.GetObjectsBeingCustomized(Objects);
        if(Objects.Num()!=1) return;TWeakObjectPtr<APortableMapAuthoringRoot> Root=Cast<APortableMapAuthoringRoot>(Objects[0].Get());
        auto& Category=Builder.EditCategory("Portable Map");
        Category.AddCustomRow(LOCTEXT("MapNameFilter","Map Name"))
            .NameContent()[SNew(STextBlock).Text(LOCTEXT("MapName","Map Name"))]
            .ValueContent().MinDesiredWidth(200)[SNew(SEditableTextBox).Text_Lambda([Root]{return Root.IsValid()?FText::AsCultureInvariant(Root->GetActorLabel()):FText::GetEmpty();})
                .OnTextCommitted_Lambda([Root](const FText& Text,ETextCommit::Type){if(Root.IsValid()&&!Text.IsEmpty()){FScopedTransaction Transaction(LOCTEXT("RenameMap","Rename Map"));Root->Modify();Root->SetActorLabel(Text.ToString());}})];
        Category.AddCustomRow(LOCTEXT("ExportMapFilter","Export Map")).WholeRowContent()[SNew(SButton).Text(LOCTEXT("RootExportMap","Export Map"))
            .OnClicked_Lambda([]{OpenTab();if(auto Editor=ActiveViewer.Pin()) Editor->SetTab(2);return FReply::Handled();})];
    }
};
TUniquePtr<FAutoConsoleCommand> OpenCommand,EditorCommand;
TUniquePtr<FAutoConsoleCommand> CaptureCommand;
TUniquePtr<FAutoConsoleCommand> DownloadCommand;
static FAutoConsoleCommand CreateEmptyAuthoringCommand(TEXT("Tanuki.MapEditor.CreateEmpty"),TEXT("Create the same play-ready empty map as the New tab."),FConsoleCommandDelegate::CreateLambda([]
{OpenTab();if(auto Editor=ActiveViewer.Pin())Editor->CreateEmpty();}));
}

static bool WindowRegistered=false;
void OpenPortableMapEditorWindow() { TanukiPortableViewer::OpenTab(); }
bool OpenPortableMapEditorFile(const FString& File) { TanukiPortableViewer::OpenTab();if(auto Editor=TanukiPortableViewer::ActiveViewer.Pin())return Editor->OpenEditableMap(File);return false; }
bool CanAddPortableMapEditorActor(EPortableMapEditorActor Kind,FText& Reason)
{
    if(Kind<EPortableMapEditorActor::Boundary || Kind>EPortableMapEditorActor::DirectionalLight)
    {Reason=LOCTEXT("UnknownActorKind","Choose a supported Tanuki map actor.");return false;}
    if(!PortableMapEditorHost::HasAccess())
    {Reason=LOCTEXT("ActorAccessRequired","Enable Tanuki map authoring access first.");return false;}
    if(!GEditor || GEditor->IsPlaySessionInProgress() || GEditor->IsPlaySessionRequestQueued())
    {Reason=LOCTEXT("StopPreviewToAddActor","Stop Play mode before adding map actors.");return false;}
    if(!TanukiPortableViewer::SViewer::CurrentRoot())
    {Reason=LOCTEXT("OpenMapToAddActor","Open or create a Tanuki map first.");return false;}
    Reason=FText::GetEmpty();return true;
}
bool AddPortableMapEditorActor(EPortableMapEditorActor Kind,FText& Reason)
{
    if(!CanAddPortableMapEditorActor(Kind,Reason))return false;
    using TanukiPortableViewer::SViewer;
    switch(Kind)
    {
    case EPortableMapEditorActor::Boundary:SViewer::AddComponent(true);break;
    case EPortableMapEditorActor::PlayerStart:SViewer::AddComponent(false);break;
    case EPortableMapEditorActor::PointLight:SViewer::AddLight(TEXT("point"));break;
    case EPortableMapEditorActor::SpotLight:SViewer::AddLight(TEXT("spot"));break;
    case EPortableMapEditorActor::DirectionalLight:SViewer::AddLight(TEXT("directional"));break;
    }
    return true;
}
void RegisterPortableMapEditorWindow()
{
    if(WindowRegistered)return;
    WindowRegistered=true;
    PortableMapEditorHost::RegisterAuthoringPlay();
    using namespace TanukiPortableViewer;
    FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor")).RegisterCustomClassLayout(TEXT("PortableMapAuthoringRoot"),FOnGetDetailCustomizationInstance::CreateStatic(&FMapRootDetails::MakeInstance));
    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TabId, FOnSpawnTab::CreateLambda([](const FSpawnTabArgs&)
    {
        TSharedRef<SViewer> Viewer = SNew(SViewer);
        ActiveViewer = Viewer;
        return SNew(SDockTab).TabRole(ETabRole::NomadTab)[Viewer];
    }),FCanSpawnTab::CreateLambda([](const FSpawnTabArgs&){return PortableMapEditorHost::HasAccess();})).SetDisplayName(LOCTEXT("TabTitle", "Tanuki Map Editor"));
    if(UToolMenus::IsToolMenuUIEnabled())
    {
        auto* Menu=UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
        Menu->FindOrAddSection(TEXT("Radbot")).AddMenuEntry(TEXT("TanukiPortableMapViewer"),
            NSLOCTEXT("TanukiEditor","PortableMapViewerMenu","Tanuki Map Editor"),NSLOCTEXT("TanukiEditor","PortableMapViewerTooltip","Create, open and export Tanuki maps."),
            FSlateIcon(),FUIAction(FExecuteAction::CreateStatic(&OpenPortableMapEditorWindow),FCanExecuteAction::CreateStatic(&PortableMapEditorHost::HasAccess)));
    }
    RegisterPortableMapEditorToolbar();
    EditorCommand=MakeUnique<FAutoConsoleCommand>(TEXT("Tanuki.MapEditor.Open"),TEXT("Open the Map Editor: [new|open|export|filename]."),FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
    {OpenTab();if(auto Editor=ActiveViewer.Pin())if(!Args.IsEmpty()){
        if(Args[0]==TEXT("new"))Editor->SetTab(0);else if(Args[0]==TEXT("open"))Editor->SetTab(1);else if(Args[0]==TEXT("export"))Editor->SetTab(2);else Editor->OpenEditableMap(Args[0]);}}));
    OpenCommand = MakeUnique<FAutoConsoleCommand>(TEXT("Tanuki.PortableMapViewer.Open"),
        TEXT("Open the portable map viewer, optionally loading an external .tanukimap filename."),
        FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
        {
            OpenTab();
            if (!Args.IsEmpty()) if (const TSharedPtr<SViewer> Viewer = ActiveViewer.Pin()) Viewer->LoadFile(Args[0]);
        }));
    CaptureCommand=MakeUnique<FAutoConsoleCommand>(TEXT("Tanuki.PortableMapViewer.CapturePreview"),
        TEXT("Generate the same framed thumbnail used when the Workshop preview field is blank."),
        FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
        {
            FString File=Args.IsEmpty()?FString():Args[0],Error;
            const auto Viewer=ActiveViewer.Pin();
            const bool Saved=Viewer && Viewer->SaveFramedPreview(File,Error);
            UE_LOG(LogTemp,Display,TEXT("PORTABLE_MAP_THUMBNAIL_RESULT success=%d file=%s error=%s"),Saved,*File,*Error);
        }));
    DownloadCommand=MakeUnique<FAutoConsoleCommand>(TEXT("Tanuki.PortableMapViewer.WorkshopPlay"),
        TEXT("Download a Workshop item and run in PIE using the map viewer's production action."),
        FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
        { OpenTab(); if (!Args.IsEmpty()) if (const auto Viewer=ActiveViewer.Pin()) Viewer->DownloadAndPlay(Args[0]); }));
}

void UnregisterPortableMapEditorWindow()
{
    UnregisterPortableMapEditorToolbar();
    if(!WindowRegistered)return;
    WindowRegistered=false;
    PortableMapEditorHost::Shutdown();
    PortableMapWorkshopEditor::Shutdown();
    using namespace TanukiPortableViewer;
    if(auto* Properties=FModuleManager::GetModulePtr<FPropertyEditorModule>(TEXT("PropertyEditor"))) Properties->UnregisterCustomClassLayout(TEXT("PortableMapAuthoringRoot"));
    EditorCommand.Reset();OpenCommand.Reset();
    CaptureCommand.Reset();
    DownloadCommand.Reset();
    if (const TSharedPtr<SDockTab> Tab = FGlobalTabmanager::Get()->FindExistingLiveTab(TabId)) Tab->RequestCloseTab();
    ActiveViewer.Reset();
    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabId);
}

#undef LOCTEXT_NAMESPACE

#if WITH_DEV_AUTOMATION_TESTS
#include "Editor.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTanukiPortableViewerTest, "Tanuki.Editor.PortableMapViewer.IsolationAndReload",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTanukiPortableViewerTest::RunTest(const FString&)
{
    FString Filename;
    if (!FParse::Value(FCommandLine::Get(), TEXT("PortableMapViewerTest="), Filename))
    {
        AddError(TEXT("Supply -PortableMapViewerTest=<courtyard.tanukimap>; missing evidence is not a pass."));
        return false;
    }
    using namespace TanukiPortableViewer;
    UWorld* OriginalWorld = GEditor->GetEditorWorldContext().World();
    TSharedPtr<SMapViewport> Preview = SNew(SMapViewport);
    TestNotEqual(TEXT("Preview has its own world"), Preview->GetPreviewWorld(), OriginalWorld);
    FString Error;
    const bool bLoaded = Preview->Load(Filename, Error);
    TestTrue(*FString::Printf(TEXT("Courtyard imports: %s"), *Error), bLoaded);
    if (!bLoaded) return false;
    TWeakObjectPtr<APortableMapScene> Map = Preview->GetMap();
    const FString ContentId = Map->GetContentId();
    TestEqual(TEXT("Four starts are available to overlays"), Map->GetMarkers().Num(), 4);
    TestTrue(TEXT("Preview actor is transient"), Map->HasAnyFlags(RF_Transient));
    const FString InvalidFile = FPaths::CreateTempFilename(*FPaths::ProjectSavedDir(), TEXT("InvalidPortableMap"), TEXT(".tanukimap"));
    FFileHelper::SaveStringToFile(TEXT("invalid container"), *InvalidFile);
    TestFalse(TEXT("Invalid replacement is rejected"), Preview->Load(InvalidFile, Error));
    IFileManager::Get().Delete(*InvalidFile);
    TestEqual(TEXT("Failed reload retains prior map"), Map->GetContentId(), ContentId);
    TestTrue(TEXT("Valid reload succeeds"), Preview->Load(Filename, Error));
    TestEqual(TEXT("Reload does not duplicate starts"), Map->GetMarkers().Num(), 4);
    TestEqual(TEXT("Editor level remains current"), GEditor->GetEditorWorldContext().World(), OriginalWorld);
    Preview.Reset();
    TestFalse(TEXT("Closing preview destroys imported actor"), Map.IsValid());
    TSharedPtr<SMapViewport> Reopened = SNew(SMapViewport);
    TestNull(TEXT("Reopened preview begins empty"), Reopened->GetMap());
    TestTrue(TEXT("Reopened preview can import again"), Reopened->Load(Filename, Error));
    return true;
}
#endif
