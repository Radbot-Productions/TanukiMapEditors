#include "PortableMapEditorWindow.h"
#include "PortableMapEditorHost.h"
#include "PortableMapAuthoring.h"
#include "ToolMenus.h"
#include "Styling/AppStyle.h"
#include "HAL/IConsoleManager.h"
#include "Editor.h"

#define LOCTEXT_NAMESPACE "PortableMapEditorToolbar"

namespace
{
const FName ToolbarOwner(TEXT("PortableMaps.AuthoringToolbar"));
const FName ToolbarMenu(TEXT("LevelEditor.LevelEditorToolBar.PlayToolBar"));

void CreateMaterial(const FString& Profile)
{
    FString BeforeReason,Reason;
    const bool Ready=PortableMapAuthoring::CanCreateMaterialInCurrentFolder(Profile,BeforeReason);
    const bool Accepted=PortableMapAuthoring::CreateMaterialInCurrentFolder(Profile,Reason);
    UE_LOG(LogTemp,Display,TEXT("PORTABLE_MAP_MATERIAL_CREATE profile=%s ready=%d accepted=%d reason=%s"),*Profile,Ready,Accepted,*Reason);
}

void AddActorEntry(FToolMenuSection& Section,FName Name,FText Label,EPortableMapEditorActor Kind)
{
    Section.AddMenuEntry(Name,Label,LOCTEXT("AddActorTooltip","Add this actor to the current Tanuki map."),FSlateIcon(),
        FUIAction(FExecuteAction::CreateLambda([Kind]
        {
            FText Reason;
            if(!AddPortableMapEditorActor(Kind,Reason))UE_LOG(LogTemp,Warning,TEXT("PORTABLE_MAP_ACTOR_REJECTED reason=%s"),*Reason.ToString());
        }),FCanExecuteAction::CreateLambda([Kind]{FText Reason;return CanAddPortableMapEditorActor(Kind,Reason);})));
}

void PopulateAuthoringMenu(UToolMenu* Menu)
{
    auto& Components=Menu->AddSection(TEXT("Components"),LOCTEXT("ComponentsSection","Components"));
    Components.AddMenuEntry(TEXT("Skybox"),LOCTEXT("Skybox","Skybox"),
        LOCTEXT("SkyboxHelp","Select the shared skybox and base-lighting preset. Changing Day/Night replaces the authored Unreal sky together with its lighting."),FSlateIcon(),
        FUIAction(FExecuteAction::CreateLambda([]
        {
            if(auto* Root=GEditor?PortableMapAuthoring::FindRoot(GEditor->GetEditorWorldContext().World()):nullptr)
            {GEditor->SelectNone(false,true);GEditor->SelectActor(Root,true,true);GEditor->NoteSelectionChange();}
        }),FCanExecuteAction::CreateLambda([]{return GEditor&&!GEditor->PlayWorld&&PortableMapEditorHost::HasAccess()&&PortableMapAuthoring::FindRoot(GEditor->GetEditorWorldContext().World());})));
    Components.AddMenuEntry(TEXT("LightingSettings"),LOCTEXT("LightingSettings","Lighting Settings"),
        LOCTEXT("LightingSettingsHelp","Select the map's unique base-lighting settings. Changing the profile replaces only base illumination."),FSlateIcon(),
        FUIAction(FExecuteAction::CreateLambda([]
        {
            if(auto* Root=GEditor?PortableMapAuthoring::FindRoot(GEditor->GetEditorWorldContext().World()):nullptr)
            {GEditor->SelectNone(false,true);GEditor->SelectActor(Root,true,true);GEditor->NoteSelectionChange();}
        }),FCanExecuteAction::CreateLambda([]{return GEditor&&!GEditor->PlayWorld&&PortableMapEditorHost::HasAccess()&&PortableMapAuthoring::FindRoot(GEditor->GetEditorWorldContext().World());})));
    auto& Actors=Menu->AddSection(TEXT("Actors"),LOCTEXT("ActorsSection","Actors"));
    AddActorEntry(Actors,TEXT("Boundary"),LOCTEXT("Boundary","Map Boundary"),EPortableMapEditorActor::Boundary);
    AddActorEntry(Actors,TEXT("PlayerStart"),LOCTEXT("PlayerStart","Player Start"),EPortableMapEditorActor::PlayerStart);
    AddActorEntry(Actors,TEXT("PointLight"),LOCTEXT("PointLight","Point Light"),EPortableMapEditorActor::PointLight);
    AddActorEntry(Actors,TEXT("SpotLight"),LOCTEXT("SpotLight","Spot Light"),EPortableMapEditorActor::SpotLight);
    AddActorEntry(Actors,TEXT("DirectionalLight"),LOCTEXT("DirectionalLight","Directional Light"),EPortableMapEditorActor::DirectionalLight);
    auto& Materials=Menu->AddSection(TEXT("Materials"),LOCTEXT("MaterialsSection","Materials"));
    for(const auto& Entry:TArray<TPair<FString,FText>>{
        {TEXT("PBR"),LOCTEXT("PBRMaterial","PBR Material Instance")},
        {TEXT("Cutout"),LOCTEXT("CutoutMaterial","Cutout Material Instance")},
        {TEXT("Transparent"),LOCTEXT("TransparentMaterial","Transparent Material Instance")}})
    {
        const FString Profile=Entry.Key;
        Materials.AddMenuEntry(FName(*(TEXT("CreateMaterial")+Profile)),Entry.Value,
            LOCTEXT("CreateMaterialTooltip","Create a material instance in the selected Content Browser folder. Emission is optional on PBR."),FSlateIcon(),
            FUIAction(FExecuteAction::CreateLambda([Profile]{CreateMaterial(Profile);}),
                FCanExecuteAction::CreateLambda([Profile]{FString Reason;return PortableMapAuthoring::CanCreateMaterialInCurrentFolder(Profile,Reason);})));
    }
}
}

void RegisterPortableMapEditorToolbar()
{
    if(!UToolMenus::IsToolMenuUIEnabled())return;
    UToolMenus::UnregisterOwner(ToolbarOwner);
    FToolMenuOwnerScoped Owner(ToolbarOwner);
    auto* Menus=UToolMenus::Get();
    Menus->RemoveEntry(TEXT("LevelEditor.MainMenu.Tools"),TEXT("Radbot"),TEXT("TanukiMaterialEditor"));
    Menus->RemoveEntry(TEXT("LevelEditor.MainMenu.Tools"),TEXT("Radbot"),TEXT("TanukiMapComponents"));
    auto& Section=Menus->ExtendMenu(ToolbarMenu)->AddSection(TEXT("TanukiAuthoring"),FText(),FToolMenuInsert(TEXT("Play"),EToolMenuInsertType::After));
    Section.AddEntry(FToolMenuEntry::InitToolBarButton(TEXT("TanukiMapEditor"),
        FUIAction(FExecuteAction::CreateStatic(&OpenPortableMapEditorWindow),FCanExecuteAction::CreateStatic(&PortableMapEditorHost::HasAccess)),
        LOCTEXT("OpenEditor","Tanuki Map Editor"),LOCTEXT("OpenEditorTooltip","Open the Tanuki map editor."),
        FSlateIcon(FAppStyle::GetAppStyleSetName(),TEXT("LevelEditor.Tabs.Levels"))));
    Section.AddEntry(FToolMenuEntry::InitComboButton(TEXT("TanukiAuthoringActions"),FUIAction(),
        FNewToolMenuDelegate::CreateStatic(&PopulateAuthoringMenu),LOCTEXT("Add","Add"),
        LOCTEXT("AddTooltip","Add actors and create materials for Tanuki maps."),FSlateIcon(),true));
    auto& Tools=Menus->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"))->FindOrAddSection(TEXT("Radbot"));
    Tools.AddSubMenu(TEXT("TanukiMapComponents"),LOCTEXT("ToolsMenu","Tanuki Actors and Materials"),
        LOCTEXT("ToolsTooltip","Add actors and create materials for Tanuki maps."),FNewToolMenuDelegate::CreateStatic(&PopulateAuthoringMenu));
    Menus->RefreshAllWidgets();
}

void UnregisterPortableMapEditorToolbar()
{
    UToolMenus::UnregisterOwner(ToolbarOwner);
}

// Refresh only menu registrations after a native patch; this does not touch authored content or PIE.
static FAutoConsoleCommand RefreshToolbar(TEXT("Tanuki.MapEditor.RefreshToolbar"),TEXT("Refresh the Tanuki map authoring toolbar."),
    FConsoleCommandDelegate::CreateStatic(&RegisterPortableMapEditorToolbar));

static FAutoConsoleCommand MaterialReadiness(TEXT("Tanuki.MapEditor.MaterialReadiness"),TEXT("Report material creation readiness for the selected Content Browser folder."),
    FConsoleCommandDelegate::CreateLambda([]
    {
        for(const TCHAR* Profile:{TEXT("PBR"),TEXT("Cutout"),TEXT("Transparent")})
        {
            FString Reason;
            const bool Ready=PortableMapAuthoring::CanCreateMaterialInCurrentFolder(Profile,Reason);
            UE_LOG(LogTemp,Display,TEXT("PORTABLE_MAP_MATERIAL_READY profile=%s ready=%d reason=%s"),Profile,Ready,*Reason);
        }
    }));

static FAutoConsoleCommand CreateMaterialCommand(TEXT("Tanuki.MapEditor.CreateMaterial"),TEXT("Create a native material instance in the selected Content Browser folder: PBR, Cutout or Transparent."),
    FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
    {
        if(Args.Num()==1)CreateMaterial(Args[0]);
    }));

#undef LOCTEXT_NAMESPACE
