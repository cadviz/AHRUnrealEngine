// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	KismetCompilerModule.cpp
=============================================================================*/

#include "KismetCompilerPrivatePCH.h"

#include "KismetCompiler.h"
#include "AnimBlueprintCompiler.h"

#include "Editor/UnrealEd/Public/Kismet2/KismetDebugUtilities.h"
#include "Editor/UnrealEd/Public/Kismet2/KismetReinstanceUtilities.h"

#include "UserDefinedStructureCompilerUtils.h"

DEFINE_LOG_CATEGORY(LogK2Compiler);
DECLARE_CYCLE_STAT(TEXT("Compile Time"), EKismetCompilerStats_CompileTime, STATGROUP_KismetCompiler);
DECLARE_CYCLE_STAT(TEXT("Compile Skeleton Class"), EKismetCompilerStats_CompileSkeletonClass, STATGROUP_KismetCompiler);
DECLARE_CYCLE_STAT(TEXT("Compile Generated Class"), EKismetCompilerStats_CompileGeneratedClass, STATGROUP_KismetCompiler);

IMPLEMENT_MODULE( FKismet2CompilerModule, KismetCompiler );


#define LOCTEXT_NAMESPACE "KismetCompiler"

//////////////////////////////////////////////////////////////////////////
// FKismet2CompilerModule

struct FBlueprintIsBeingCompiledHelper
{
private:
	UBlueprint* Blueprint;
public:
	FBlueprintIsBeingCompiledHelper(UBlueprint* InBlueprint) : Blueprint(InBlueprint)
	{
		check(NULL != Blueprint);
		check(!Blueprint->bBeingCompiled);
		Blueprint->bBeingCompiled = true;
	}

	~FBlueprintIsBeingCompiledHelper()
	{
		Blueprint->bBeingCompiled = false;
	}
};

// Compiles a blueprint.
void FKismet2CompilerModule::CompileBlueprintInner(class UBlueprint* Blueprint, const FKismetCompilerOptions& CompileOptions, FCompilerResultsLog& Results, TArray<UObject*>* ObjLoaded)
{
	FBlueprintIsBeingCompiledHelper BeingCompiled(Blueprint);

	Blueprint->CurrentMessageLog = &Results;

	// Early out if blueprint parent is missing
	if (Blueprint->ParentClass == NULL)
	{
		Results.Error(*LOCTEXT("KismetCompileError_MalformedParentClasss", "Blueprint @@ has missing or NULL parent class.").ToString(), Blueprint);
	}
	else
	{
		// Loop through all external compiler delegates attempting to compile the blueprint.
		bool Compiled = false;
		for ( IBlueprintCompiler* Compiler : Compilers )
		{
			if ( Compiler->CanCompile(Blueprint) )
			{
				Compiled = true;
				Compiler->Compile(Blueprint, CompileOptions, Results, ObjLoaded);
				break;
			}
		}

		// if no one handles it, then use the default blueprint compiler.
		if ( !Compiled )
		{
			if ( UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(Blueprint) )
			{
				FAnimBlueprintCompiler Compiler(AnimBlueprint, Results, CompileOptions, ObjLoaded);
				Compiler.Compile();
				check(Compiler.NewClass);
			}
			else
			{
				FKismetCompilerContext Compiler(Blueprint, Results, CompileOptions, ObjLoaded);
				Compiler.Compile();
				check(Compiler.NewClass);
			}
		}
	}

	Blueprint->CurrentMessageLog = NULL;
}


void FKismet2CompilerModule::CompileStructure(class UUserDefinedStruct* Struct, FCompilerResultsLog& Results)
{
	Results.SetSourceName(Struct->GetName());
	BP_SCOPED_COMPILER_EVENT_STAT(EKismetCompilerStats_CompileTime);
	FUserDefinedStructureCompilerUtils::CompileStruct(Struct, Results, true);
}

// Compiles a blueprint.
void FKismet2CompilerModule::CompileBlueprint(class UBlueprint* Blueprint, const FKismetCompilerOptions& CompileOptions, FCompilerResultsLog& Results, FBlueprintCompileReinstancer* ParentReinstancer, TArray<UObject*>* ObjLoaded)
{
	SCOPE_SECONDS_COUNTER(GBlueprintCompileTime);
	BP_SCOPED_COMPILER_EVENT_STAT(EKismetCompilerStats_CompileTime);

	Results.SetSourceName(Blueprint->GetName());

	const bool bIsBrandNewBP = (Blueprint->SkeletonGeneratedClass == NULL) && (Blueprint->GeneratedClass == NULL) && (Blueprint->ParentClass != NULL) && !CompileOptions.bIsDuplicationInstigated;

	for ( IBlueprintCompiler* Compiler : Compilers )
	{
		Compiler->PreCompile(Blueprint);
	}

	if (CompileOptions.CompileType != EKismetCompileType::Cpp)
	{
		BP_SCOPED_COMPILER_EVENT_STAT(EKismetCompilerStats_CompileSkeletonClass);

		FBlueprintCompileReinstancer SkeletonReinstancer(Blueprint->SkeletonGeneratedClass);

		FCompilerResultsLog SkeletonResults;
		SkeletonResults.bSilentMode = true;
		FKismetCompilerOptions SkeletonCompileOptions;
		SkeletonCompileOptions.CompileType = EKismetCompileType::SkeletonOnly;
		CompileBlueprintInner(Blueprint, SkeletonCompileOptions, SkeletonResults, ObjLoaded);
	}

	// If this was a full compile, take appropriate actions depending on the success of failure of the compile
	if( CompileOptions.IsGeneratedClassCompileType() )
	{
		BP_SCOPED_COMPILER_EVENT_STAT(EKismetCompilerStats_CompileGeneratedClass);

		// Perform the full compile
		CompileBlueprintInner(Blueprint, CompileOptions, Results, ObjLoaded);

		if (Results.NumErrors == 0)
		{
			// Blueprint is error free.  Go ahead and fix up debug info
			Blueprint->Status = (0 == Results.NumWarnings) ? BS_UpToDate : BS_UpToDateWithWarnings;
			
			Blueprint->BlueprintSystemVersion = UBlueprint::GetCurrentBlueprintSystemVersion();

			// Reapply breakpoints to the bytecode of the new class
			for (int32 Index = 0; Index < Blueprint->Breakpoints.Num(); ++Index)
			{
				UBreakpoint* Breakpoint = Blueprint->Breakpoints[Index];
				FKismetDebugUtilities::ReapplyBreakpoint(Breakpoint);
			}
		}
		else
		{
			// Should never get errors from a brand new blueprint!
			ensure(!bIsBrandNewBP || (Results.NumErrors == 0));

			// There were errors.  Compile the generated class to have function stubs
			Blueprint->Status = BS_Error;

			static const FBoolConfigValueHelper ReinstanceOnlyWhenNecessary(TEXT("Kismet"), TEXT("bReinstanceOnlyWhenNecessary"), GEngineIni);

			// Reinstance objects here, so we can preserve their memory layouts to reinstance them again
			if( ParentReinstancer != NULL )
			{
				ParentReinstancer->UpdateBytecodeReferences();

				if(!Blueprint->bIsRegeneratingOnLoad)
				{
					ParentReinstancer->ReinstanceObjects(!ReinstanceOnlyWhenNecessary);
				}
			}

			FBlueprintCompileReinstancer StubReinstancer(Blueprint->GeneratedClass);

			// Toss the half-baked class and generate a stubbed out skeleton class that can be used
			FCompilerResultsLog StubResults;
			StubResults.bSilentMode = true;
			FKismetCompilerOptions StubCompileOptions(CompileOptions);
			StubCompileOptions.CompileType = EKismetCompileType::StubAfterFailure;

			CompileBlueprintInner(Blueprint, StubCompileOptions, StubResults, ObjLoaded);

			StubReinstancer.UpdateBytecodeReferences();
			if( !Blueprint->bIsRegeneratingOnLoad )
			{
				StubReinstancer.ReinstanceObjects(!ReinstanceOnlyWhenNecessary);
			}
		}
	}

	for ( IBlueprintCompiler* Compiler : Compilers )
	{
		Compiler->PostCompile(Blueprint);
	}

	UPackage* Package = Blueprint->GetOutermost();
	if( Package )
	{
		UMetaData* MetaData =  Package->GetMetaData();
		MetaData->RemoveMetaDataOutsidePackage();
	}
}

// Recover a corrupted blueprint
void FKismet2CompilerModule::RecoverCorruptedBlueprint(class UBlueprint* Blueprint)
{
	UPackage* Package = Blueprint->GetOutermost();

	// Get rid of any stale classes
	for (FObjectIterator ObjIt; ObjIt; ++ObjIt)
	{
		UObject* TestObject = *ObjIt;
		if (TestObject->GetOuter() == Package)
		{
			// This object is in the blueprint package; is it expected?
			if (UClass* TestClass = Cast<UClass>(TestObject))
			{
				if ((TestClass != Blueprint->SkeletonGeneratedClass) && (TestClass != Blueprint->GeneratedClass))
				{
					// Unexpected UClass
					FKismetCompilerUtilities::ConsignToOblivion(TestClass, false);
				}
			}
		}
	}

	CollectGarbage( GARBAGE_COLLECTION_KEEPFLAGS );
}

void FKismet2CompilerModule::RemoveBlueprintGeneratedClasses(class UBlueprint* Blueprint)
{
	if (Blueprint != NULL)
	{
		if (Blueprint->GeneratedClass != NULL)
		{
			FKismetCompilerUtilities::ConsignToOblivion(Blueprint->GeneratedClass, Blueprint->bIsRegeneratingOnLoad);
			Blueprint->GeneratedClass = NULL;
		}

		if (Blueprint->SkeletonGeneratedClass != NULL)
		{
			FKismetCompilerUtilities::ConsignToOblivion(Blueprint->SkeletonGeneratedClass, Blueprint->bIsRegeneratingOnLoad);
			Blueprint->SkeletonGeneratedClass = NULL;
		}
	}
}

#undef LOCTEXT_NAMESPACE
