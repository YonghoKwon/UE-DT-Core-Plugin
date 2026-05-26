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
