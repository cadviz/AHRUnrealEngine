// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.


#pragma once
#include "ReflectionCaptureComponent.h"
#include "PlaneReflectionCaptureComponent.generated.h"

	// -> will be exported to EngineDecalClasses.h
UCLASS(hidecategories=(Collision, Object, Physics, SceneComponent), MinimalAPI)
class UPlaneReflectionCaptureComponent : public UReflectionCaptureComponent
{
	GENERATED_UCLASS_BODY()

	/** Radius of the area that can receive reflections from this capture. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=ReflectionCapture, meta=(UIMin = "8.0", UIMax = "16384.0"))
	float InfluenceRadiusScale;

	UPROPERTY()
	class UDrawSphereComponent* PreviewInfluenceRadius;

	UPROPERTY()
	class UBoxComponent* PreviewCaptureBox;

public:
	virtual void UpdatePreviewShape();
	virtual float GetInfluenceBoundingRadius() const;

	// Begin UObject Interface
	// End UObject Interface
};

