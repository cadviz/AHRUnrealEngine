// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "AbilitySystemPrivatePCH.h"
#include "GameplayAbilityWorldReticle.h"
#include "GameplayAbilityTargetActor.h"

// --------------------------------------------------------------------------------------------------------------------------------------------------------
//
//	AGameplayAbilityWorldReticle
//
// --------------------------------------------------------------------------------------------------------------------------------------------------------

AGameplayAbilityWorldReticle::AGameplayAbilityWorldReticle(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	bIsTargetValid = true;
	bIsTargetAnActor = false;
	bFaceOwnerFlat = true;
}


void AGameplayAbilityWorldReticle::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	FaceTowardSource(bFaceOwnerFlat);
}

void AGameplayAbilityWorldReticle::InitializeReticle(AGameplayAbilityTargetActor* InTargetingActor, FWorldReticleParameters InParameters)
{
	check(InTargetingActor);
	TargetingActor = InTargetingActor;
	MasterPC = InTargetingActor->MasterPC;
	AddTickPrerequisiteActor(TargetingActor);		//We want the reticle to tick after the targeting actor so that designers have the final say on the position
	Parameters = InParameters;
	OnParametersInitialized();
}

bool AGameplayAbilityWorldReticle::IsNetRelevantFor(const APlayerController* RealViewer, const AActor* Viewer, const FVector& SrcLocation) const
{
	//The player who created the ability doesn't need to be updated about it - there should be local prediction in place.
	if (RealViewer == MasterPC)
	{
		return false;
	}

	return Super::IsNetRelevantFor(RealViewer, Viewer, SrcLocation);
}

bool AGameplayAbilityWorldReticle::GetReplicates() const
{
	return bReplicates;
}

void AGameplayAbilityWorldReticle::SetIsTargetValid(bool bNewValue)
{
	if (bIsTargetValid != bNewValue)
	{
		bIsTargetValid = bNewValue;
		OnValidTargetChanged(bNewValue);
	}
}

void AGameplayAbilityWorldReticle::SetIsTargetAnActor(bool bNewValue)
{
	if (bIsTargetAnActor != bNewValue)
	{
		bIsTargetAnActor = bNewValue;
		OnTargetingAnActor(bNewValue);
	}
}

void AGameplayAbilityWorldReticle::FaceTowardSource(bool bFaceIn2D)
{
	if (TargetingActor)
	{
		if (bFaceIn2D)
		{
			FVector FacingVector = (TargetingActor->StartLocation.GetTargetingTransform().GetLocation() - GetActorLocation()).GetSafeNormal2D();
			if (FacingVector.IsZero())
			{
				FacingVector = -GetActorForwardVector().GetSafeNormal2D();
			}
			if (!FacingVector.IsZero())
			{
				SetActorRotation(FacingVector.Rotation());
			}
		}
		else
		{
			FVector FacingVector = (TargetingActor->StartLocation.GetTargetingTransform().GetLocation() - GetActorLocation()).GetSafeNormal();
			if (FacingVector.IsZero())
			{
				FacingVector = -GetActorForwardVector().GetSafeNormal();
			}
			SetActorRotation(FacingVector.Rotation());
		}
	}
}
