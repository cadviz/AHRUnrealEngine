// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "NiagaraScriptSourceBase.generated.h"


struct EditorExposedVectorConstant
{
	FName ConstName;
	FVector4 Value;
};

/** Runtime data for a Niagara system */
UCLASS(MinimalAPI)
class UNiagaraScriptSourceBase : public UObject
{
	GENERATED_UCLASS_BODY()

	TArray<TSharedPtr<EditorExposedVectorConstant> > ExposedVectorConstants;
	virtual void Compile() {};
};