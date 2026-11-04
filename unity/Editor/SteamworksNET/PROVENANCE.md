# Steamworks.NET editor integration

Pinned upstream: https://github.com/rlabrecque/Steamworks.NET/tree/2025.164.1

MIT C# bindings are included under their original license. Changes: private
Radbot.MapAuthoring.Steamworks namespace, editor-only assembly, and the Windows
SDK DLL import renamed to tanuki_steam_api64 to avoid binding to another package's
SDK version. Valve's matching Windows x64 redistributable is otherwise unchanged.
The DLL importer enables Windows Editor only; no Steam dependency enters game builds.
No SteamManager gameplay objects or third-party editor scripts are included.
