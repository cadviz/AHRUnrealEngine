// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Interface_PostProcessVolume.generated.h"

/** Interface for general PostProcessVolume access **/
UINTERFACE()
class UInterface_PostProcessVolume : public UInterface
{
	GENERATED_UINTERFACE_BODY()
};

struct __AHRGridBounds
{
	FVector Bounds;
	FVector Center;
};

struct FPostProcessVolumeProperties
{
	const FPostProcessSettings* Settings;
	float Priority;
	float BlendRadius;
	float BlendWeight;
	bool bIsEnabled;
	bool bIsUnbound;
	// @RyanTorant
	bool bDefinesAHRGridSettings;
	__AHRGridBounds AHRGridSettings;
};

class IInterface_PostProcessVolume
{
	GENERATED_IINTERFACE_BODY()

	ENGINE_API virtual bool EncompassesPoint(FVector Point, float SphereRadius/*=0.f*/, float* OutDistanceToPoint) = 0;
	ENGINE_API virtual FPostProcessVolumeProperties GetProperties() const = 0;
};
