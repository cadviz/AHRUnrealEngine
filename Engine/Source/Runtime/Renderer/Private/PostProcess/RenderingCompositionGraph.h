// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	RenderingCompositionGraph.h: Scene pass order and dependency system.
=============================================================================*/

#pragma once

#include "RHIStaticStates.h"
#include "RHICommandList.h"

struct FRenderingCompositePass;
struct FRenderingCompositeOutputRef;
struct FRenderingCompositeOutput;
class FRenderingCompositionGraph;
class FViewInfo;
class FShaderParameterMap;

struct FRenderingCompositeOutputRef;
struct FRenderingCompositePassContext;

class FRenderingCompositionGraph
{
public:
	FRenderingCompositionGraph();
	~FRenderingCompositionGraph();

	/**
	 * Returns the input pointer as output to allow this:
	 * Example:  SceneColor = Graph.RegisterPass(new FRCPassPostProcessInput(GSceneRenderTargets.SceneColor));
	 * @param InPass - must not be 0
	 */
	template<class T>
	T* RegisterPass(T* InPass)
	{
		check(InPass);
		Nodes.Add(InPass);

		return InPass;
	}

	friend struct FRenderingCompositePassContext;

private:
	/** */
	TArray<FRenderingCompositePass*> Nodes;

	/** */
	void Free();

	/** calls ResetDependency() in OutputId in each pass */
	void ResetDependencies();

	/** */
	void ProcessGatherDependency(const FRenderingCompositeOutputRef* OutputRefIt);

	/** should only be called by GatherDependencies(), can also be implemented without recursion */
	static void RecursivelyGatherDependencies(const FRenderingCompositeOutputRef& InOutputRef);

	/** can also be implemented without recursion */
	void RecursivelyProcess(const FRenderingCompositeOutputRef& InOutputRef, FRenderingCompositePassContext& Context) const;

	/** Write the contents of the specified output to a file */
	void DumpOutputToFile(FRenderingCompositePassContext& Context, const FString& Filename, FRenderingCompositeOutput* Output) const;

	/**
	 * for debugging purpose O(n)
	 * @return -1 if not found
	 */
	int32 ComputeUniquePassId(FRenderingCompositePass* Pass) const;

	/**
	 * for debugging purpose O(n), unique and not overlapping with the PassId
	 * @return -1 if not found
	 */
	int32 ComputeUniqueOutputId(FRenderingCompositePass* Pass, EPassOutputId OutputId) const;
};


struct FRenderingCompositePassContext
{
	FRenderingCompositePassContext(FRHICommandListImmediate& RHICmdList, FViewInfo& InView);

	~FRenderingCompositePassContext();

	// @param GraphDebugName must not be 0
	void Process(const TCHAR *GraphDebugName);

	//
	FViewInfo& View;
	FSceneViewState* ViewState;
	// is updated before each Pass->Process() call
	FRenderingCompositePass* Pass;

	// call this method instead of RHISetViewport() so we can cache the values and use them to map beteen ScreenPos and pixels
	void SetViewportAndCallRHI(FIntRect InViewPortRect, float InMinZ = 0.0f, float InMaxZ = 1.0f)
	{
		ViewPortRect = InViewPortRect;

		RHICmdList.SetViewport(ViewPortRect.Min.X, ViewPortRect.Min.Y, InMinZ, ViewPortRect.Max.X, ViewPortRect.Max.Y, InMaxZ);
	}

	// call this method instead of RHISetViewport() so we can cache the values and use them to map beteen ScreenPos and pixels
	void SetViewportAndCallRHI(uint32 InMinX, uint32 InMinY, float InMinZ, uint32 InMaxX, uint32 InMaxY, float InMaxZ)
	{
		SetViewportAndCallRHI(FIntRect(InMinX, InMinY, InMaxX, InMaxY), InMinZ, InMaxZ);

		// otherwise the input parameters are bad
		check(IsViewportValid());
	}

	// should be called before each pass so we don't get state from the pass before
	void SetViewportInvalid()
	{
		ViewPortRect = FIntRect(0, 0, 0, 0);

		check(!IsViewportValid());
	}
	
	//
	FIntRect GetViewport() const
	{
		// need to call SetViewportAndCallRHI() before
		check(IsViewportValid());

		return ViewPortRect;
	}

	bool IsViewportValid() const
	{
		return ViewPortRect.Min != ViewPortRect.Max;
	}

	ERHIFeatureLevel::Type GetFeatureLevel() const { return FeatureLevel; }
	EShaderPlatform GetShaderPlatform() const { return GShaderPlatformForFeatureLevel[FeatureLevel]; }
	TShaderMap<FGlobalShaderType>* GetShaderMap() const { check(ShaderMap); return ShaderMap; }

	FRenderingCompositePass* Root;

	FRenderingCompositionGraph Graph;

	FRHICommandListImmediate& RHICmdList;

private:
	// cached state to map between ScreenPos and pixels
	FIntRect ViewPortRect;

	ERHIFeatureLevel::Type FeatureLevel;
	TShaderMap<FGlobalShaderType>* ShaderMap;
};

// ---------------------------------------------------------------------------

struct FRenderingCompositePass
{
	/** constructor */
	FRenderingCompositePass() : bGraphMarker(false)
	{
	}

	virtual ~FRenderingCompositePass() {}

	/** @return 0 if outside the range */
	virtual FRenderingCompositeOutputRef* GetInput(EPassInputId InPassInputId) = 0;

	/**
	 * Each input is a dependency and will be processed before the node itself (don't generate cycles)
	 * The index allows to access the input in Process() and on the shader side
	 * @param InInputIndex silently ignores calls outside the range
	 */
	virtual void SetInput(EPassInputId InPassInputId, const FRenderingCompositeOutputRef& InOutputRef) = 0;

	/**
	 * Allows to add additional dependencies (cannot be accessed by the node but need to be processed before the node)
	 */
	virtual void AddDependency(const FRenderingCompositeOutputRef& InOutputRef) = 0;

	/** @param Parent the one that was pointing to *this */
	virtual void Process(FRenderingCompositePassContext& Context) = 0;

	// @return true: ePId_Input0 is used as output, cannot make texture lookups, does not support MRT yet
	virtual bool FrameBufferBlendingWithInput0() const { return false; }

	/** @return 0 if outside the range */
	virtual FRenderingCompositeOutput* GetOutput(EPassOutputId InPassOutputId) = 0;

	/**
	 * Allows to iterate through all dependencies (inputs and additional dependency)
	 * @return 0 if outside the range
	 */
	virtual FRenderingCompositeOutputRef* GetDependency(uint32 Index) = 0;

	/**
	 * Allows to iterate through all additional dependencies
	 * @return 0 if outside the range
	 */
	virtual FRenderingCompositeOutputRef* GetAdditionalDependency(uint32 Index) = 0;

	/**
	 * Allows access to dump filename for a given output
	 * @return Filename for output dump with extension
	 */
	virtual const FString& GetOutputDumpFilename(EPassOutputId OutputId) = 0;

	/**
	 * Allows setting of a dump filename for a given output
	 * @param Index - Output index
	 * @param Filename - Output dump filename, needs to have extension, gets modified if we have an HDR image e.g. ".png"
	 */
	virtual void SetOutputDumpFilename(EPassOutputId OutputId, const TCHAR* Filename) = 0;

	/**
	 * Allows access to an optional TArray of colors in which to capture the pass output
	 * @return Filename for output dump
	 */
	virtual TArray<FColor>* GetOutputColorArray(EPassOutputId OutputId) const = 0;

	/**
	 * Allows setting of a pointer to a color array, into which the specified pass output will be copied
	 * @param Index - Output index
	 * @param OutputBuffer - Output array pointer
	 */
	virtual void SetOutputColorArray(EPassOutputId OutputId, TArray<FColor>* OutputBuffer) = 0;

	/** */
	virtual FPooledRenderTargetDesc ComputeOutputDesc(EPassOutputId InPassOutputId) const = 0;

	/** Convenience method as this could have been done with GetInput() alone, performance: O(n) */
	uint32 ComputeInputCount();

	/** Convenience method as this could have been done with GetOutput() alone, performance: O(n) */
	uint32 ComputeOutputCount();

	FString ConstructDebugName();

	/**
	 * Convenience method, is using other virtual methods.
	 * @return 0 if there is an error
	 */
	const FPooledRenderTargetDesc* GetInputDesc(EPassInputId InPassInputId) const;

	/** */
	virtual void Release() = 0;

private:

	/** to allow the graph to mark already processed nodes */
	bool bGraphMarker;

	friend class FRenderingCompositionGraph;
};

struct FRenderingCompositeOutputRef
{
	FRenderingCompositeOutputRef(FRenderingCompositePass* InSource = 0, EPassOutputId InPassOutputId = ePId_Output0)
		:Source(InSource), PassOutputId(InPassOutputId)
	{
	}

	FRenderingCompositePass* GetPass() const; 

	/** @return can be 0 */
	FRenderingCompositeOutput* GetOutput() const;

	EPassOutputId GetOutputId() const { return PassOutputId; }

	bool IsValid() const
	{
		return Source != 0;
	}

private:
	/** can be 0 */
	FRenderingCompositePass* Source;
	/** to call Source->GetInput(SourceSubIndex) */
	EPassOutputId PassOutputId;

	friend class FRenderingCompositionGraph;
};

struct FRenderingCompositeOutput
{
	FRenderingCompositeOutput()
		:Dependencies(0)
	{
	}

	void ResetDependency()
	{
		Dependencies = 0;
	}

	void AddDependency()
	{
		++Dependencies;
	}

	uint32 GetDependencyCount() const
	{
		return Dependencies;
	}

	void ResolveDependencies()
	{
		if(Dependencies > 0)
		{
			--Dependencies;

			if(!Dependencies)
			{
				// the internal reference is released
				PooledRenderTarget.SafeRelease();
			}
		}
	}

	/** Get the texture to read from */
	TRefCountPtr<IPooledRenderTarget> RequestInput()
	{
//		check(PooledRenderTarget);
		check(Dependencies > 0);

		return PooledRenderTarget;
	}

	/**
	 * get the surface to write to
	 * @param DebugName must not be 0
	 */
	const FSceneRenderTargetItem& RequestSurface(const FRenderingCompositePassContext& Context);

	// private:
	FPooledRenderTargetDesc RenderTargetDesc; 
	TRefCountPtr<IPooledRenderTarget> PooledRenderTarget;


private:

	uint32 Dependencies;
};

//
template <uint32 InputCount, uint32 OutputCount>
struct TRenderingCompositePassBase :public FRenderingCompositePass
{
	TRenderingCompositePassBase()
	{
		for (uint32 i = 0; i < OutputCount; ++i)
		{
			PassOutputColorArrays[i] = nullptr;
		}
	}

	virtual ~TRenderingCompositePassBase()
	{
	}

	// interface FRenderingCompositePass

	virtual FRenderingCompositeOutputRef* GetInput(EPassInputId InPassInputId)
	{
		if((int32)InPassInputId < InputCount)
		{
			return &PassInputs[InPassInputId];
		}

		return 0;
	}
	
	virtual void SetInput(EPassInputId InPassInputId, const FRenderingCompositeOutputRef& VirtualBuffer)
	{
		if((int32)InPassInputId < InputCount)
		{
			PassInputs[InPassInputId] = VirtualBuffer;
		}
		else
		{
			// this node doesn't have this input
			check(0);
		}
	}

	void AddDependency(const FRenderingCompositeOutputRef& InOutputRef)
	{
		AdditionalDependencies.Add(InOutputRef);
	}

	virtual FRenderingCompositeOutput* GetOutput(EPassOutputId InPassOutputId)
	{
		if((int32)InPassOutputId < OutputCount)
		{
			return &PassOutputs[InPassOutputId];
		}

		return 0;
	}

	/** can be overloaded for more control */
/*	virtual FPooledRenderTargetDesc ComputeOutputDesc(EPassOutputId InPassOutputId) const
	{
		FPooledRenderTargetDesc Ret = PassInputs[0].GetOutput()->RenderTargetDesc;

		Ret.Reset();

		return Ret;
	}
*/	
	virtual FRenderingCompositeOutputRef* GetDependency(uint32 Index)
	{
		// first through all inputs
		FRenderingCompositeOutputRef* Ret = GetInput((EPassInputId)Index);

		if(!Ret)
		{
			// then all additional dependencies
			Ret = GetAdditionalDependency(Index - InputCount);
		}

		return Ret;
	}

	virtual FRenderingCompositeOutputRef* GetAdditionalDependency(uint32 Index)
	{
		uint32 AdditionalDependenciesCount = AdditionalDependencies.Num();

		if(Index < AdditionalDependenciesCount)
		{
			return &AdditionalDependencies[Index];
		}

		return 0;
	}

	virtual const FString& GetOutputDumpFilename(EPassOutputId OutputId)
	{
		check (OutputId < OutputCount);
		return PassOutputDumpFilenames[OutputId];
	}

	virtual void SetOutputDumpFilename(EPassOutputId OutputId, const TCHAR* Filename)
	{
		check (OutputId < OutputCount);
		PassOutputDumpFilenames[OutputId] = Filename;
	}

	virtual TArray<FColor>* GetOutputColorArray(EPassOutputId OutputId) const
	{
		check (OutputId < OutputCount);
		return PassOutputColorArrays[OutputId];
	}

	virtual void SetOutputColorArray(EPassOutputId OutputId, TArray<FColor>* OutputBuffer)
	{
		check (OutputId < OutputCount);
		PassOutputColorArrays[OutputId] = OutputBuffer;
	}

protected:
	/** hack to allow 0 inputs */
	FRenderingCompositeOutputRef PassInputs[InputCount == 0 ? 1 : InputCount];
	/** */
	FRenderingCompositeOutput PassOutputs[OutputCount];
	/** Filenames that the outputs can be written to after being processed */
	FString PassOutputDumpFilenames[OutputCount];
	/** Color arrays for saving off a copy of the pixel data from this pass output */
	TArray<FColor>* PassOutputColorArrays[OutputCount];
	/** All dependencies: PassInputs and all objects in this container */
	TArray<FRenderingCompositeOutputRef> AdditionalDependencies;
};


// derives from TRenderingCompositePassBase<InputCount, OutputCount>
class FRCPassPostProcessRoot : public TRenderingCompositePassBase<0, 1>
{
public:
	// interface FRenderingCompositePass ---------
	virtual void Process(FRenderingCompositePassContext& Context) {}
	virtual void Release() override { delete this; }
	FPooledRenderTargetDesc ComputeOutputDesc(EPassOutputId InPassOutputId) const { FPooledRenderTargetDesc Desc; Desc.DebugName = TEXT("Root"); return Desc; }
};

void CompositionGraph_OnStartFrame();
