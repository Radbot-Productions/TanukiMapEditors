#include "PortableMapEditorHost.h"
#include "PortableMapEditorPreview.h"
#include "PortableMapEnvironment.h"
#include "PortableMapGamePlayer.h"

namespace PortableMapEditorHost
{
namespace { FPortableMapEditorHost Integration; }
void Configure(FPortableMapEditorHost Host) { Integration=MoveTemp(Host); }
void ConfigureReferences(APortableMapAuthoringRoot* Root) {if(Integration.ConfigureReferences)Integration.ConfigureReferences(Root);else PortableMapEnvironment::Configure(Root);}
bool HasAccess() { return !Integration.HasAccess || Integration.HasAccess(); }
bool Start(const FString& File,FString& Error) { return Integration.Start?Integration.Start(File,Error):PortableMapGamePlayer::Start(File,Error); }
bool StartEdited(FString& Error) { return Integration.StartEdited?Integration.StartEdited(Error):PortableMapGamePlayer::StartEdited(Error); }
FString GetStatus() { return Integration.GetStatus?Integration.GetStatus():PortableMapGamePlayer::GetStatus(); }
void RegisterAuthoringPlay() { if(Integration.RegisterAuthoringPlay) Integration.RegisterAuthoringPlay();else PortableMapEditorPreview::RegisterAuthoringPlay(); }
void Shutdown() { if(Integration.Shutdown) Integration.Shutdown();PortableMapGamePlayer::Shutdown();PortableMapEditorPreview::Shutdown(); }
}
