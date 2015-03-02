// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "SubversionSourceControlPrivatePCH.h"
#include "SubversionSourceControlSettings.h"
#include "SourceControlHelpers.h"

namespace SubversionSettingsConstants
{

/** The section of the ini file we load our settings from */
static const FString SettingsSection = TEXT("SubversionSourceControl.SubversionSourceControlSettings");

}

const FString& FSubversionSourceControlSettings::GetRepository() const
{
	FScopeLock ScopeLock(&CriticalSection);
	return Repository;
}

void FSubversionSourceControlSettings::SetRepository(const FString& InString)
{
	FScopeLock ScopeLock(&CriticalSection);
	Repository = InString;
}

const FString& FSubversionSourceControlSettings::GetUserName() const
{
	FScopeLock ScopeLock(&CriticalSection);
	return UserName;
}

void FSubversionSourceControlSettings::SetUserName(const FString& InString)
{
	FScopeLock ScopeLock(&CriticalSection);
	UserName = InString;
}

const FString& FSubversionSourceControlSettings::GetLabelsRoot() const
{
	FScopeLock ScopeLock(&CriticalSection);
	return LabelsRoot;
}

void FSubversionSourceControlSettings::SetLabelsRoot(const FString& InString)
{
	FScopeLock ScopeLock(&CriticalSection);
	LabelsRoot = InString;
}

void FSubversionSourceControlSettings::LoadSettings()
{
	FScopeLock ScopeLock(&CriticalSection);
	const FString& IniFile = SourceControlHelpers::GetSettingsIni();
	GConfig->GetString(*SubversionSettingsConstants::SettingsSection, TEXT("Repository"), Repository, IniFile);
	GConfig->GetString(*SubversionSettingsConstants::SettingsSection, TEXT("UserName"), UserName, IniFile);
	GConfig->GetString(*SubversionSettingsConstants::SettingsSection, TEXT("LabelsRoot"), LabelsRoot, IniFile);
}

void FSubversionSourceControlSettings::SaveSettings() const
{
	FScopeLock ScopeLock(&CriticalSection);
	const FString& IniFile = SourceControlHelpers::GetSettingsIni();
	GConfig->SetString(*SubversionSettingsConstants::SettingsSection, TEXT("Repository"), *Repository, IniFile);
	GConfig->SetString(*SubversionSettingsConstants::SettingsSection, TEXT("UserName"), *UserName, IniFile);
	GConfig->SetString(*SubversionSettingsConstants::SettingsSection, TEXT("LabelsRoot"), *LabelsRoot, IniFile);
}