// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LazyObjectPtr.cpp: Guid-based lazy pointer to UObject
=============================================================================*/

#include "CoreUObjectPrivate.h"
#include "UObjectAnnotation.h"


/** Annotation associating objects with their guids **/
static FUObjectAnnotationSparseSearchable<FUniqueObjectGuid,true> GuidAnnotation;

#define MAX_PIE_INSTANCES 10
static TMap<FGuid, FGuid> PIEGuidMap[MAX_PIE_INSTANCES];

/*-----------------------------------------------------------------------------
	FUniqueObjectGuid
-----------------------------------------------------------------------------*/

FUniqueObjectGuid::FUniqueObjectGuid(const class UObject* InObject)
	: Guid(GuidAnnotation.GetAnnotation(InObject).Guid)
{
}

FUniqueObjectGuid FUniqueObjectGuid::FixupForPIE() const
{
	FUniqueObjectGuid Temp(*this);

	check(GPlayInEditorID != -1)
	const FGuid *FoundGuid = PIEGuidMap[GPlayInEditorID % MAX_PIE_INSTANCES].Find(Temp.GetGuid());

	if (FoundGuid)
	{
		Temp = *FoundGuid;
	}
	return Temp;
}

UObject *FUniqueObjectGuid::ResolveObject() const
{
	return GuidAnnotation.Find(*this);
}

FString FUniqueObjectGuid::ToString() const
{
	return Guid.ToString(EGuidFormats::UniqueObjectGuid);
}

void FUniqueObjectGuid::FromString(const FString& From)
{
	TArray<FString> Split;
	Split.Empty(4);
	if( From.ParseIntoArray( &Split, TEXT("-"), false ) == 4 )
	{
		Guid.A=FParse::HexNumber(*Split[0]);
		Guid.B=FParse::HexNumber(*Split[1]);
		Guid.C=FParse::HexNumber(*Split[2]);
		Guid.D=FParse::HexNumber(*Split[3]);
	}
	else
	{
		Guid.Invalidate();
	}
}

FUniqueObjectGuid FUniqueObjectGuid::GetOrCreateIDForObject(const class UObject *Object)
{
	check(Object);
	FUniqueObjectGuid ObjectGuid(Object);
	if (!ObjectGuid.IsValid())
	{
		ObjectGuid.Guid = FGuid::NewGuid();
		GuidAnnotation.AddAnnotation(Object, ObjectGuid);
		Object->MarkPackageDirty();
	}
	return ObjectGuid;
}

int32 FUniqueObjectGuid::CurrentAnnotationTag = 1;


/*-----------------------------------------------------------------------------------------------------------
	FLazyObjectPtr
-------------------------------------------------------------------------------------------------------------*/

void FLazyObjectPtr::PossiblySerializeObjectGuid(UObject *Object, FArchive& Ar)
{
	if (Ar.IsSaving() || Ar.IsCountingMemory())
	{
		FUniqueObjectGuid Guid = GuidAnnotation.GetAnnotation(Object);
		bool HasGuid = Guid.IsValid();
		Ar << HasGuid;
		if (HasGuid)
		{
			if (Ar.GetPortFlags() & PPF_DuplicateForPIE)
			{
				check(GPlayInEditorID != -1);
				FGuid &FoundGuid = PIEGuidMap[GPlayInEditorID % MAX_PIE_INSTANCES].FindOrAdd(Guid.GetGuid());
				if (!FoundGuid.IsValid())
				{
					Guid = FoundGuid = FGuid::NewGuid();
				}
				else
				{
					Guid = FoundGuid;
				}
			}

			Ar << Guid;
		}
	}
	else if (Ar.IsLoading())
	{
		bool HasGuid = false;
		Ar << HasGuid;
		if (HasGuid)
		{
			FUniqueObjectGuid Guid;
			Ar << Guid;

			// Don't try and resolve GUIDs when loading a package for diff'ing
			const UPackage* Package = Object->GetOutermost();
			bool bLoadedForDiff = (Package && (Package->PackageFlags & PKG_ForDiffing));
			if (!bLoadedForDiff && (!(Ar.GetPortFlags() & PPF_Duplicate) || (Ar.GetPortFlags() & PPF_DuplicateForPIE)))
			{
				check(!Guid.IsDefault());
				UObject* OtherObject = Guid.ResolveObject();
				if (OtherObject != Object) // on undo/redo, the object (potentially) already exists
				{
					if (OtherObject != NULL)
					{
						UE_CLOG(!((FApp::IsGame() || GIsPlayInEditorWorld) && Package && Package->ContainsMap()), LogUObjectGlobals, Warning, TEXT("Guid is in use by %s and %s, which should never happen in the editor but could happen at runtime with duplicate level loading or PIE"), *OtherObject->GetFullName(), !!Object ? *Object->GetFullName() : TEXT("NULL"));
						// This guid is in use, which should never happen in the editor but could happen at runtime with duplicate level loading or PIE. If so give it a new GUID to avoid crashing
						Guid = FGuid::NewGuid();
					}
					GuidAnnotation.AddAnnotation(Object, Guid);
					FUniqueObjectGuid::InvalidateTag();
				}
			}
		}
	}
}

void FLazyObjectPtr::ResetPIEFixups()
{
	check(GPlayInEditorID != -1);
	PIEGuidMap[GPlayInEditorID % MAX_PIE_INSTANCES].Reset();
}

