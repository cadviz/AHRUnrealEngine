// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SlateBlueprintLibrary.generated.h"

UCLASS(MinimalAPI)
class USlateBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_UCLASS_BODY()

	/**
	 * Absolute coordinates could be either desktop or window space depending on what space the root of the widget hierarchy is in.
	 *
	 * @return true if the provided location in absolute coordinates is within the bounds of this geometry.
	 */
	UFUNCTION(BlueprintCallable, Category="User Interface|Geometry")
	static bool IsUnderLocation(const FGeometry& Geometry, const FVector2D& AbsoluteCoordinate);

	/**
	 * Absolute coordinates could be either desktop or window space depending on what space the root of the widget hierarchy is in.
	 *
	 * @return Transforms AbsoluteCoordinate into the local space of this Geometry.
	 */
	UFUNCTION(BlueprintCallable, Category="User Interface|Geometry")
	static FVector2D AbsoluteToLocal(const FGeometry& Geometry, FVector2D AbsoluteCoordinate);

	/**
	 * Translates local coordinates into absolute coordinates
	 *
	 * Absolute coordinates could be either desktop or window space depending on what space the root of the widget hierarchy is in.
	 *
	 * @return  Absolute coordinates
	 */
	UFUNCTION(BlueprintCallable, Category="User Interface|Geometry")
	static FVector2D LocalToAbsolute(const FGeometry& Geometry, FVector2D LocalCoordinate);
};