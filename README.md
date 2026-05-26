# UE-DT-Core-Plugin

Reusable Unreal Engine runtime plugin for Digital Twin projects.

## Main features

- API request subsystem
- WebSocket data routing subsystem
- Data queue processing subsystem
- Object registry subsystem
- Search base subsystem
- Runtime logging subsystem
- UI base widgets and theme support
- Sensor utility components

## Installation

Copy this repository into a UE project:

```text
YourProject/Plugins/DTCore
```

Then regenerate project files, enable the plugin, and build the project.

## Configuration

Configure the plugin from Project Settings or project config files.

Important settings are defined in `UDTCoreSettings`:

- API DataTable
- WebSocket DataTable
- WebSocket URL
- WebSocket login and passcode
- API base URLs
- WebSocket topics
- API topics
- Default widget theme

## Runtime override with Game.ini

For packaged clients, API and WebSocket connection values can be changed without repackaging.

The plugin uses this priority:

1. Editable packaged `Game.ini`: `<PackageRoot>/<ProjectName>/Config/Game.ini`
2. `[DTCoreRuntimeOverride]` values in Unreal `GGameIni` if they are not empty
3. `/Script/DTCore.DTCoreSettings` values in Unreal `GGameIni` if they are not empty
4. `UDTCoreSettings` defaults
5. code fallback values

For a Windows package, the editable path is usually:

```text
Windows/<ProjectName>/Config/Game.ini
```

For example:

```text
Windows/m7at10_dt/Config/Game.ini
```

The plugin creates missing keys in both Unreal `GGameIni` and the editable packaged `Game.ini` during subsystem initialization.

Template:

```ini
[DTCoreRuntimeOverride]
BaseApiUrl=
LocalApiUrl=
TestApiUrl=
ProdApiUrl=
WebSocketUrl=
WebSocketLogin=
WebSocketPasscode=
```

After packaging, fill only the values that should override the defaults. Empty values are ignored.

Example:

```ini
[DTCoreRuntimeOverride]
LocalApiUrl=http://192.168.0.10:8090
TestApiUrl=http://192.168.0.20:8000
WebSocketUrl=ws://192.168.0.20:61616
WebSocketLogin=artemis
WebSocketPasscode=artemis
```

Restart the client after changing `Game.ini` so the new values are loaded safely.

## Data processing

`UDxDataSubsystem` receives API and WebSocket payloads through queues. It batches data, parses payloads in background tasks, and runs final processing on the GameThread.

Parser functions such as `ParseToStruct` should stay pure. Use them for JSON or string parsing only. Gameplay or UObject work should be done in `ProcessStructData`.

## Object registry

`UDxObjectSubsystem` provides shared actor registration and lookup helpers:

- `RegisterObject`
- `FindObject`
- `GetAllObjects`
- `GetObjectIds`
- `ContainsObject`
- `CompactInvalidObjects`

## Search

`UDxSearchSubsystem` provides a base class for project-specific search logic. New code should prefer FName-based category search through `PerformSearchByCategory`.

## Project independence

DTCore should remain reusable across projects. Project-specific classes, maps, assets, and environment values should live in the consuming UE project.