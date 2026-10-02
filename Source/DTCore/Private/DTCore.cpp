// Copyright Epic Games, Inc. All Rights Reserved.

#include "DTCore.h"

#define LOCTEXT_NAMESPACE "FDTCoreModule"

void FDTCoreModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
}

void FDTCoreModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FDTCoreModule, DTCore)

DEFINE_LOG_CATEGORY(LogBase);
DEFINE_LOG_CATEGORY(LogDx);