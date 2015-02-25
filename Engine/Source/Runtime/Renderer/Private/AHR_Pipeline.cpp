// @RyanTorant
#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "ApproximateHybridRaytracing.h"
#include "SceneUtils.h"
#include "SceneFilterRendering.h"
#include "AHR_Voxelization.h"
#include "math.h"
//#include "../Public/AHRGlobalSignal.h"

//std::atomic<unsigned int> AHRGlobalSignal_RebuildGrids;

//#include <mutex>
//#include <thread>
#include <vector>
using namespace std;

// Using a full screen quad at every stage instead of a cs as the targets are already setted for a quad. Also, not using groupshared memory.
class AHRPassVS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(AHRPassVS,Global);
public:

	static bool ShouldCache(EShaderPlatform Platform)
	{
		return IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM5);
	}

	AHRPassVS()	{}
	AHRPassVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FGlobalShader(Initializer)
	{
	}

	void SetParameters(FRHICommandList& RHICmdList, const FViewInfo& View)
	{
		FGlobalShader::SetParameters(RHICmdList, GetVertexShader(),View);
	}

	// Begin FShader Interface
	virtual bool Serialize(FArchive& Ar)
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		return bShaderHasOutdatedParameters;
	}
	// End FShader Interface
private:
};
IMPLEMENT_SHADER_TYPE(,AHRPassVS,TEXT("AHRComposite"),TEXT("VS"),SF_Vertex);

void  FApproximateHybridRaytracer::StartFrame(FViewInfo& View)
{	
	// New frame, new starting idx
	currentLightIDX = 0;
	
	// Check if the bounds are valid
	if(View.FinalPostProcessSettings.AHR_internal_initialized)
	{
		gridSettings.Bounds = View.FinalPostProcessSettings.AHR_internal_SceneBounds;
		gridSettings.Center = View.FinalPostProcessSettings.AHR_internal_SceneOrigins;
		gridSettings.VoxelSize = View.FinalPostProcessSettings.AHRVoxelSize;

		gridSettings.SliceSize.X = ceil(gridSettings.Bounds.X / gridSettings.VoxelSize);
		gridSettings.SliceSize.Y = ceil(gridSettings.Bounds.Y / gridSettings.VoxelSize);
		gridSettings.SliceSize.Z = ceil(gridSettings.Bounds.Z / gridSettings.VoxelSize);

		if(gridSettings.SliceSize.X < 1)
			gridSettings.SliceSize.X = 1;
		else if(gridSettings.SliceSize.Y < 1)
			gridSettings.SliceSize.Y = 1;
		else if(gridSettings.SliceSize.Z < 1)
			gridSettings.SliceSize.Z = 1;
	
		uint32 maxVal = CVarAHRMaxSliceSize.GetValueOnRenderThread();
		maxVal = maxVal*maxVal*maxVal;

		if(uint32(gridSettings.SliceSize.X*gridSettings.SliceSize.Y*gridSettings.SliceSize.Z) > maxVal)
		{
			// Get the voxel size from the max
			gridSettings.VoxelSize = cbrt(gridSettings.Bounds.X*gridSettings.Bounds.Y*gridSettings.Bounds.Z/maxVal);

			gridSettings.SliceSize.X = ceil(gridSettings.Bounds.X / gridSettings.VoxelSize);
			gridSettings.SliceSize.Y = ceil(gridSettings.Bounds.Y / gridSettings.VoxelSize);
			gridSettings.SliceSize.Z = ceil(gridSettings.Bounds.Z / gridSettings.VoxelSize);
		}
	}
}

class AHRDynamicStaticVolumeCombine : public FGlobalShader
{
	DECLARE_SHADER_TYPE(AHRDynamicStaticVolumeCombine,Global);

public:

	static bool ShouldCache(EShaderPlatform Platform)
	{
		return RHISupportsComputeShaders(Platform);
	}

	static void ModifyCompilationEnvironment( EShaderPlatform Platform, FShaderCompilerEnvironment& OutEnvironment )
	{
		FGlobalShader::ModifyCompilationEnvironment( Platform, OutEnvironment );
	}

	/** Default constructor. */
	AHRDynamicStaticVolumeCombine()
	{
	}

	/** Initialization constructor. */
	explicit AHRDynamicStaticVolumeCombine( const ShaderMetaType::CompiledShaderInitializerType& Initializer )
		: FGlobalShader(Initializer)
	{
		StaticVolume.Bind( Initializer.ParameterMap, TEXT("StaticVolume") );
		DynamicVolume.Bind( Initializer.ParameterMap, TEXT("DynamicVolume") );
		DynamicEmissiveVolume.Bind( Initializer.ParameterMap, TEXT("DynamicEmissiveVolume") );
		StaticEmissiveVolume.Bind( Initializer.ParameterMap, TEXT("StaticEmissiveVolume") );
		gridRes.Bind( Initializer.ParameterMap, TEXT("gridRes") );
	}

	/** Serialization. */
	virtual bool Serialize( FArchive& Ar ) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize( Ar );
		Ar << StaticVolume;
		Ar << DynamicVolume;
		Ar << DynamicEmissiveVolume;
		Ar << StaticEmissiveVolume;
		Ar << gridRes;
		return bShaderHasOutdatedParameters;
	}

	/**
	 * Set parameters for this shader.
	 */
	
	void SetParameters(FRHICommandList& RHICmdList, FUnorderedAccessViewRHIParamRef DynamicVolumeUAV,FUnorderedAccessViewRHIParamRef emissiveUAV,
													FShaderResourceViewRHIParamRef StaticVolumeSRV,FShaderResourceViewRHIParamRef staticEmissiveSRV,
													FIntVector inGridRes
													)
	{
		FComputeShaderRHIParamRef ComputeShaderRHI = GetComputeShader();

		if ( DynamicVolume.IsBound() )
			RHICmdList.SetUAVParameter(ComputeShaderRHI, DynamicVolume.GetBaseIndex(), DynamicVolumeUAV);
		if ( StaticVolume.IsBound() )
			RHICmdList.SetShaderResourceViewParameter(ComputeShaderRHI,StaticVolume.GetBaseIndex(), StaticVolumeSRV);
		if ( DynamicEmissiveVolume.IsBound() )
			RHICmdList.SetUAVParameter(ComputeShaderRHI,DynamicEmissiveVolume.GetBaseIndex(), emissiveUAV);
		if ( StaticEmissiveVolume.IsBound() )
			RHICmdList.SetShaderResourceViewParameter(ComputeShaderRHI,StaticEmissiveVolume.GetBaseIndex(), staticEmissiveSRV);

		SetShaderValue(RHICmdList, ComputeShaderRHI, gridRes, inGridRes );
	}

	/**
	 * Unbinds any buffers that have been bound.
	 */
	void UnbindBuffers(FRHICommandList& RHICmdList)
	{
		FComputeShaderRHIParamRef ComputeShaderRHI = GetComputeShader();
		if ( DynamicVolume.IsBound() )
			RHICmdList.SetUAVParameter(ComputeShaderRHI, DynamicVolume.GetBaseIndex(), FUnorderedAccessViewRHIParamRef());
		if ( StaticVolume.IsBound() )
			RHICmdList.SetShaderResourceViewParameter(ComputeShaderRHI,StaticVolume.GetBaseIndex(), FShaderResourceViewRHIParamRef());
		if ( DynamicEmissiveVolume.IsBound() )
			RHICmdList.SetUAVParameter(ComputeShaderRHI,DynamicEmissiveVolume.GetBaseIndex(), FUnorderedAccessViewRHIParamRef());
		if ( StaticEmissiveVolume.IsBound() )
			RHICmdList.SetShaderResourceViewParameter(ComputeShaderRHI,StaticEmissiveVolume.GetBaseIndex(), FShaderResourceViewRHIParamRef());
	}

private:
	FShaderResourceParameter StaticVolume;
	FShaderResourceParameter DynamicVolume;
	FShaderResourceParameter DynamicEmissiveVolume;
	FShaderResourceParameter StaticEmissiveVolume;
	FShaderParameter gridRes;
};
// I know, this is ugly. I'm lazy. Report me!
class AHRDynamicStaticEmissiveVolumeCombine : public FGlobalShader
{
	DECLARE_SHADER_TYPE(AHRDynamicStaticEmissiveVolumeCombine,Global);

public:

	static bool ShouldCache(EShaderPlatform Platform)
	{
		return RHISupportsComputeShaders(Platform);
	}

	static void ModifyCompilationEnvironment( EShaderPlatform Platform, FShaderCompilerEnvironment& OutEnvironment )
	{
		FGlobalShader::ModifyCompilationEnvironment( Platform, OutEnvironment );
	}

	/** Default constructor. */
	AHRDynamicStaticEmissiveVolumeCombine()
	{
	}

	/** Initialization constructor. */
	explicit AHRDynamicStaticEmissiveVolumeCombine( const ShaderMetaType::CompiledShaderInitializerType& Initializer )
		: FGlobalShader(Initializer)
	{
		StaticVolume.Bind( Initializer.ParameterMap, TEXT("StaticVolume") );
		DynamicVolume.Bind( Initializer.ParameterMap, TEXT("DynamicVolume") );
		DynamicEmissiveVolume.Bind( Initializer.ParameterMap, TEXT("DynamicEmissiveVolume") );
		StaticEmissiveVolume.Bind( Initializer.ParameterMap, TEXT("StaticEmissiveVolume") );
		gridRes.Bind( Initializer.ParameterMap, TEXT("gridRes") );
	}

	/** Serialization. */
	virtual bool Serialize( FArchive& Ar ) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize( Ar );
		Ar << StaticVolume;
		Ar << DynamicVolume;
		Ar << DynamicEmissiveVolume;
		Ar << StaticEmissiveVolume;
		Ar << gridRes;
		return bShaderHasOutdatedParameters;
	}

	/**
	 * Set parameters for this shader.
	 */
	
	void SetParameters(FRHICommandList& RHICmdList, FUnorderedAccessViewRHIParamRef DynamicVolumeUAV,FUnorderedAccessViewRHIParamRef emissiveUAV,
													FShaderResourceViewRHIParamRef StaticVolumeSRV,FShaderResourceViewRHIParamRef staticEmissiveSRV,
													FIntVector inGridRes)
	{
		FComputeShaderRHIParamRef ComputeShaderRHI = GetComputeShader();

		if ( DynamicVolume.IsBound() )
			RHICmdList.SetUAVParameter(ComputeShaderRHI, DynamicVolume.GetBaseIndex(), DynamicVolumeUAV);
		if ( StaticVolume.IsBound() )
			RHICmdList.SetShaderResourceViewParameter(ComputeShaderRHI,StaticVolume.GetBaseIndex(), StaticVolumeSRV);
		if ( DynamicEmissiveVolume.IsBound() )
			RHICmdList.SetUAVParameter(ComputeShaderRHI,DynamicEmissiveVolume.GetBaseIndex(), emissiveUAV);
		if ( StaticEmissiveVolume.IsBound() )
			RHICmdList.SetShaderResourceViewParameter(ComputeShaderRHI,StaticEmissiveVolume.GetBaseIndex(), staticEmissiveSRV);

		SetShaderValue(RHICmdList, ComputeShaderRHI, gridRes, inGridRes );
	}

	/**
	 * Unbinds any buffers that have been bound.
	 */
	void UnbindBuffers(FRHICommandList& RHICmdList)
	{
		FComputeShaderRHIParamRef ComputeShaderRHI = GetComputeShader();

		if ( DynamicVolume.IsBound() )
			RHICmdList.SetUAVParameter(ComputeShaderRHI, DynamicVolume.GetBaseIndex(), FUnorderedAccessViewRHIParamRef());
		if ( StaticVolume.IsBound() )
			RHICmdList.SetShaderResourceViewParameter(ComputeShaderRHI,StaticVolume.GetBaseIndex(), FShaderResourceViewRHIParamRef());
		if ( DynamicEmissiveVolume.IsBound() )
			RHICmdList.SetUAVParameter(ComputeShaderRHI,DynamicEmissiveVolume.GetBaseIndex(), FUnorderedAccessViewRHIParamRef());
		if ( StaticEmissiveVolume.IsBound() )
			RHICmdList.SetShaderResourceViewParameter(ComputeShaderRHI,StaticEmissiveVolume.GetBaseIndex(), FShaderResourceViewRHIParamRef());
	}

private:
	FShaderResourceParameter StaticVolume;
	FShaderResourceParameter DynamicVolume;
	FShaderResourceParameter DynamicEmissiveVolume;
	FShaderResourceParameter StaticEmissiveVolume;
	FShaderParameter gridRes;
};

IMPLEMENT_SHADER_TYPE(,AHRDynamicStaticVolumeCombine,TEXT("AHRDynamicStaticVolumeCombine"),TEXT("mainBinary"),SF_Compute);
IMPLEMENT_SHADER_TYPE(,AHRDynamicStaticEmissiveVolumeCombine,TEXT("AHRDynamicStaticVolumeCombine"),TEXT("mainEmissive"),SF_Compute);

//std::mutex cs;
//pthread_mutex_t Mutex;
FCriticalSection cs;
AHRGridSettings prevGridSettings;
vector<FName> prevStaticObjects;
FLinearColor prevPalette[256];
//once_flag stdOnceFlag;
template<class Function>
void Once(Function&& f)
{
	static bool run = true;
	if(run)
		f();
	run = false;
	return;
}

void FApproximateHybridRaytracer::VoxelizeScene(FRHICommandListImmediate& RHICmdList,FViewInfo& View)
{
	SCOPED_DRAW_EVENT(RHICmdList,AHRVoxelizeScene);

	/*static bool ran = false;
	if(ran) return;
	ran = true;

	if(View.PrimitivesToVoxelize.Num() == 0)
		return;*/
	
	bool RebuildGrids = View.FinalPostProcessSettings.AHRRebuildGrids;
	
	/*if(AHRGlobalSignal_RebuildGrids.load() == 1)
	{
		RebuildGrids = true;
		AHRGlobalSignal_RebuildGrids.store(0);
	}*/

	TArray<const FPrimitiveSceneInfo*,SceneRenderingAllocator> staticObjects;
	TArray<const FPrimitiveSceneInfo*,SceneRenderingAllocator> dynamicsObjects;
	bool voxelizeStatic = false;

	FLinearColor palette[256];
	for(auto& c : palette) c = FLinearColor(0,0,0);

	{
		FScopeLock ScopeLock(&cs);
		Once([=](){ for(auto& c : prevPalette) c = FLinearColor(0,0,0); });
	}

	uint32 currentMatIDX = 0;

	// Reset the state of the materials
	for(auto obj : View.PrimitivesToVoxelize)
	{
		for(int n = 0;n < obj->StaticMeshes.Num();n++)
		{
			auto mat = obj->StaticMeshes[n].MaterialRenderProxy->GetMaterial(View.GetFeatureLevel());
			auto state = mat->GetAHRPaletteState();
			state->stored = false;
		}
	}


	//cs.lock();
	//pthread_mutex_lock(&Mutex);
	{
		// Make this thread-safe
		FScopeLock ScopeLock(&cs);

		// Could this be optimized? It feels wrong...
		bool staticListChanged = false;
		for(auto obj : View.PrimitivesToVoxelize)
		{
			for(int n = 0;n < obj->StaticMeshes.Num();n++)
			{
				auto mat = obj->StaticMeshes[n].MaterialRenderProxy->GetMaterial(View.GetFeatureLevel());
				auto state = mat->GetAHRPaletteState();

				// If the material is not stored, increase the index and store
				if(!state->stored && mat->ShouldInjectEmissiveIntoDynamicGI())
				{
					// Output a warning if we are out of bounds
					if(currentMatIDX >= 256)
					{
						UE_LOG(LogRenderer, Warning, TEXT("Tried to add more emissive materials to the AHR engine that the maximum supported, will be ignored."));
						break;
					}

					currentMatIDX++;
					palette[currentMatIDX] = mat->GetAHREmissiveColor();
					state->stored = true;
					state->idx = currentMatIDX;
				}
			}

			if(obj->Proxy->NeedsEveryFrameVoxelization())
			{
				dynamicsObjects.Add(obj);
			}
			else
			{
				// Check if the object exists on the prev statics. O(x^2). Need to make this faster
				bool found = false;
				for(auto sObjName : prevStaticObjects)
					found |= sObjName == obj->Proxy->GetOwnerName();

				staticListChanged |= !found;
				staticObjects.Add(obj);
			}
		}
		// Voxelize static only once, to the static grid. Revoxelize static if something changed (add, delete, grid res changed)
		voxelizeStatic = prevGridSettings.Bounds != gridSettings.Bounds ||
						 prevGridSettings.Center != gridSettings.Center ||
						 prevGridSettings.SliceSize != gridSettings.SliceSize ||
						 prevStaticObjects.size() != staticObjects.Num() ||
					     staticListChanged ||
						 RebuildGrids;

		if(voxelizeStatic)
		{
			// we are going to voxelize, so store state
			prevStaticObjects.clear();
			for(auto obj : staticObjects)
				prevStaticObjects.push_back(obj->Proxy->GetOwnerName());

			prevGridSettings = gridSettings;
		}

		// Check if the palette changed
		bool paletteChanged = false;
		for(int i = 0; i < 256; i++) paletteChanged |= (prevPalette[i] != palette[i]);
		if(paletteChanged)
		{
			// TODO: a dynamic texture could be more efficient, specially if the material emissive color is driven trough blueprints/c++ code and changes often

			// Recreate the texture
			for(int i = 0; i < 256; i++) prevPalette[i] = palette[i];

			// Destroy the texture
			EmissivePaletteTexture.SafeRelease();
			EmissivePaletteSRV.SafeRelease();

			FRHIResourceCreateInfo CreateInfo;
			EmissivePaletteTexture = RHICreateTexture2D(256,1,PF_B8G8R8A8,1,1,TexCreate_ShaderResource,CreateInfo);

			// Copy palette
			uint8 palleteTexBuff[256*4];
			for(int i = 0; i < 256; i++)
			{
				auto col = palette[i].Quantize();

				palleteTexBuff[i*4]     =  col.B;
				palleteTexBuff[i*4 + 1] =  col.G;
				palleteTexBuff[i*4 + 2] =  col.R;
				palleteTexBuff[i*4 + 3] =  col.A; // Alpha is used as a multiplier
			}

			uint32 Stride = 0;
			uint8* textureData = (uint8*)RHILockTexture2D( EmissivePaletteTexture, 0, RLM_WriteOnly, Stride, false );
			FMemory::Memcpy(textureData, palleteTexBuff, 256*4);
			RHIUnlockTexture2D( EmissivePaletteTexture, 0, false );

			EmissivePaletteSRV = RHICreateShaderResourceView(EmissivePaletteTexture,0,1,PF_B8G8R8A8);
		}
	}

	//pthread_mutex_unlock(&Mutex);
	//cs.unlock();

	uint32 cls[4] = { 0,0,0,0 };

	// Voxelize only static
	if( staticObjects.Num( ) > 0  && voxelizeStatic)
	{
		// Clear
		RHICmdList.ClearUAV(StaticSceneVolume->UAV, cls);
		RHICmdList.ClearUAV(StaticEmissiveVolume->UAV, cls);
		SetStaticVolumeAsActive();

		TAHRVoxelizerElementPDI<FAHRVoxelizerDrawingPolicyFactory> Drawer(
			&View, FAHRVoxelizerDrawingPolicyFactory::ContextType(RHICmdList) );

		for( auto PrimitiveSceneInfo : staticObjects )
		{
			FScopeCycleCounter Context( PrimitiveSceneInfo->Proxy->GetStatId( ) );
			Drawer.SetPrimitive( PrimitiveSceneInfo->Proxy );
			
			// Calls SceneProxy DrawDynamicElements function
			PrimitiveSceneInfo->Proxy->DrawDynamicElements(&Drawer,&View,EDrawDynamicFlags::Voxelize);
		}
	}

	
	// Voxelize dynamic to the dynamic grid
	RHICmdList.ClearUAV(DynamicSceneVolume->UAV, cls);
	RHICmdList.ClearUAV(DynamicEmissiveVolume->UAV, cls);
	SetDynamicVolumeAsActive();

	TAHRVoxelizerElementPDI<FAHRVoxelizerDrawingPolicyFactory> Drawer(
			&View, FAHRVoxelizerDrawingPolicyFactory::ContextType(RHICmdList) );

	for( auto PrimitiveSceneInfo : dynamicsObjects )
	{
		FScopeCycleCounter Context( PrimitiveSceneInfo->Proxy->GetStatId( ) );
		Drawer.SetPrimitive( PrimitiveSceneInfo->Proxy );
			
		// Calls SceneProxy DrawDynamicElements function
		PrimitiveSceneInfo->Proxy->DrawDynamicElements(&Drawer,&View,EDrawDynamicFlags::Voxelize);
	}
	
	// Dispatch a compute shader that applies a per voxel pack or between the static grid and the dynamic grid.
	// The dynamic grid is the one that gets binded as an SRV
	TShaderMapRef<AHRDynamicStaticVolumeCombine> combineCS(GetGlobalShaderMap(View.GetFeatureLevel()));
	RHICmdList.SetComputeShader(combineCS->GetComputeShader());

	uint32 l = gridSettings.SliceSize.X*gridSettings.SliceSize.Y*gridSettings.SliceSize.Z/32;
	uint32 x = ceil(cbrt(l / 256.0f)); // cbrt = cubic root
	combineCS->SetParameters(RHICmdList, DynamicSceneVolume->UAV,DynamicEmissiveVolume->UAV,StaticSceneVolume->SRV,StaticEmissiveVolume->SRV,FIntVector(x*8,x*8,x*4));
	DispatchComputeShader(RHICmdList, *combineCS, x, x, x);

	combineCS->UnbindBuffers(RHICmdList);
	
	TShaderMapRef<AHRDynamicStaticEmissiveVolumeCombine> combineCSEmissive(GetGlobalShaderMap(View.GetFeatureLevel()));
	RHICmdList.SetComputeShader(combineCSEmissive->GetComputeShader());

	l = gridSettings.SliceSize.X*gridSettings.SliceSize.Y*gridSettings.SliceSize.Z/4;
	x = ceil(cbrt(l / 256.0f)); // cbrt = cubic root
	combineCSEmissive->SetParameters(RHICmdList, DynamicSceneVolume->UAV,DynamicEmissiveVolume->UAV,StaticSceneVolume->SRV,StaticEmissiveVolume->SRV,FIntVector(x*8,x*8,x*4));
	DispatchComputeShader(RHICmdList, *combineCSEmissive, x, x ,x);

	combineCSEmissive->UnbindBuffers(RHICmdList);
}

///
/// Tracing
///
BEGIN_UNIFORM_BUFFER_STRUCT(AHRTraceSceneCB,)
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(FVector2D,ScreenRes)

	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(FIntVector,SliceSize)
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(FVector,InvSceneBounds)
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(FVector,WorldToVoxelOffset) // -SceneCenter/SceneBounds
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(FVector,invVoxel)

	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(float,InitialDispMult)
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(float,SamplesDispMultiplier)

	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(uint32,GlossyRayCount)
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(uint32,GlossySamplesCount)
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(uint32,DiffuseRayCount)
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(uint32,DiffuseSamplesCount)

	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(FVector,LostRayColor)
END_UNIFORM_BUFFER_STRUCT(AHRTraceSceneCB)
IMPLEMENT_UNIFORM_BUFFER_STRUCT(AHRTraceSceneCB,TEXT("AHRTraceCB"));

BEGIN_UNIFORM_BUFFER_STRUCT(AHRShadowMatrices,)
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(FMatrix,Matrix0)
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(FMatrix,Matrix1)
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(FMatrix,Matrix2)
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(FMatrix,Matrix3)
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(FMatrix,Matrix4)

	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(FVector,Offset0)
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(FVector,Offset1)
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(FVector,Offset2)
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(FVector,Offset3)
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(FVector,Offset4)
END_UNIFORM_BUFFER_STRUCT(AHRShadowMatrices)
IMPLEMENT_UNIFORM_BUFFER_STRUCT(AHRShadowMatrices,TEXT("AHRShadowMatrices"));

class AHRTraceScenePS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(AHRTraceScenePS,Global)
public:

	static bool ShouldCache(EShaderPlatform Platform)
	{
		return IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(EShaderPlatform Platform, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Platform, OutEnvironment);
		if(CVarAHRTraceReflections.GetValueOnRenderThread() == 1)
			OutEnvironment.SetDefine(TEXT("_GLOSSY"),1);
	}

	AHRTraceScenePS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
	:	FGlobalShader(Initializer)
	{
		DeferredParameters.Bind(Initializer.ParameterMap);
		SceneVolume.Bind(Initializer.ParameterMap, TEXT("SceneVolume"));
		LinearSampler.Bind(Initializer.ParameterMap, TEXT("samLinear"));
		cb.Bind(Initializer.ParameterMap, TEXT("AHRTraceCB"));
		matrixCB.Bind(Initializer.ParameterMap, TEXT("AHRShadowMatrices"));

		ShadowAlbedo0.Bind(Initializer.ParameterMap, TEXT("ShadowAlbedo0"));
		ShadowAlbedo1.Bind(Initializer.ParameterMap, TEXT("ShadowAlbedo1"));
		ShadowAlbedo2.Bind(Initializer.ParameterMap, TEXT("ShadowAlbedo2"));
		ShadowAlbedo3.Bind(Initializer.ParameterMap, TEXT("ShadowAlbedo3"));
		ShadowAlbedo4.Bind(Initializer.ParameterMap, TEXT("ShadowAlbedo4"));

		ShadowNormals0.Bind(Initializer.ParameterMap, TEXT("ShadowNormals0"));
		ShadowNormals1.Bind(Initializer.ParameterMap, TEXT("ShadowNormals1"));
		ShadowNormals2.Bind(Initializer.ParameterMap, TEXT("ShadowNormals2"));
		ShadowNormals3.Bind(Initializer.ParameterMap, TEXT("ShadowNormals3"));
		ShadowNormals4.Bind(Initializer.ParameterMap, TEXT("ShadowNormals4"));

		ShadowZ0.Bind(Initializer.ParameterMap, TEXT("ShadowZ0"));
		ShadowZ1.Bind(Initializer.ParameterMap, TEXT("ShadowZ1"));
		ShadowZ2.Bind(Initializer.ParameterMap, TEXT("ShadowZ2"));
		ShadowZ3.Bind(Initializer.ParameterMap, TEXT("ShadowZ3"));
		ShadowZ4.Bind(Initializer.ParameterMap, TEXT("ShadowZ4"));

		cmpSampler.Bind(Initializer.ParameterMap, TEXT("cmpSampler"));

		EmissiveVolume.Bind(Initializer.ParameterMap, TEXT("EmissiveVolume"));
		EmissivePalette.Bind(Initializer.ParameterMap, TEXT("EmissivePalette"));

		SamplingKernel.Bind(Initializer.ParameterMap, TEXT("SamplingKernel"));
		samPoint.Bind(Initializer.ParameterMap, TEXT("samPoint"));

		ObjNormal.Bind(Initializer.ParameterMap, TEXT("ObjNormal"));
	}

	AHRTraceScenePS()
	{
	}

	void SetParameters(	FRHICommandList& RHICmdList, const FSceneView& View, 
						const FShaderResourceViewRHIRef sceneVolumeSRV, 
						const FShaderResourceViewRHIRef paletteSRV, 
						const FShaderResourceViewRHIRef emissiveVolumeSRV,
						const FShaderResourceViewRHIRef samplingKernelSRV
					   )
	{
		FRHIResourceCreateInfo CreateInfo;
		static auto dummyTexture = RHICreateTexture2D(1,1,PF_ShadowDepth,1,1,TexCreate_ShaderResource,CreateInfo);

		const FPixelShaderRHIParamRef ShaderRHI = GetPixelShader();
		FGlobalShader::SetParameters(RHICmdList, ShaderRHI,View);
		DeferredParameters.Set(RHICmdList, ShaderRHI, View);

		AHRTraceSceneCB cbdata;

		AHRGridSettings ahrGrid = AHREngine.GetGridSettings();

		cbdata.SliceSize.X = ahrGrid.SliceSize.X;
		cbdata.SliceSize.Y = ahrGrid.SliceSize.Y;
		cbdata.SliceSize.Z = ahrGrid.SliceSize.Z;
		cbdata.ScreenRes.X = View.Family->FamilySizeX/2;
		cbdata.ScreenRes.Y = View.Family->FamilySizeY/2;
		cbdata.invVoxel = FVector(1.0f / float(cbdata.SliceSize.X),
								  1.0f / float(cbdata.SliceSize.Y),
								  1.0f / float(cbdata.SliceSize.Z));

		cbdata.InvSceneBounds = FVector(1.0f) / ahrGrid.Bounds;
		cbdata.WorldToVoxelOffset = -ahrGrid.Center*cbdata.InvSceneBounds; // -SceneCenter/SceneBounds
		cbdata.GlossyRayCount = View.FinalPostProcessSettings.AHRGlossyRayCount;
		cbdata.GlossySamplesCount = View.FinalPostProcessSettings.AHRGlossySamplesCount;
		cbdata.DiffuseRayCount = View.FinalPostProcessSettings.AHRDiffuseRayCount;
		cbdata.DiffuseSamplesCount = View.FinalPostProcessSettings.AHRDiffuseSamplesCount;
		cbdata.LostRayColor.X = View.FinalPostProcessSettings.AHRLostRayColor.R;
		cbdata.LostRayColor.Y = View.FinalPostProcessSettings.AHRLostRayColor.G;
		cbdata.LostRayColor.Z = View.FinalPostProcessSettings.AHRLostRayColor.B;
		cbdata.InitialDispMult = View.FinalPostProcessSettings.AHRInitialDisplacement;
		cbdata.SamplesDispMultiplier = View.FinalPostProcessSettings.AHRSamplesDisplacement;

		SetUniformBufferParameterImmediate(RHICmdList, ShaderRHI,cb,cbdata);

		AHRShadowMatrices matrix_cbdata;

		auto lList = AHREngine.GetLightsList();
		matrix_cbdata.Matrix0 = lList[0].ViewProj;
		matrix_cbdata.Matrix1 = lList[1].ViewProj;
		matrix_cbdata.Matrix2 = lList[2].ViewProj;
		matrix_cbdata.Matrix3 = lList[3].ViewProj;
		matrix_cbdata.Matrix4 = lList[4].ViewProj;

		matrix_cbdata.Offset0 = lList[0].Offset;
		matrix_cbdata.Offset1 = lList[1].Offset;
		matrix_cbdata.Offset2 = lList[2].Offset;
		matrix_cbdata.Offset3 = lList[3].Offset;
		matrix_cbdata.Offset4 = lList[4].Offset;

		SetUniformBufferParameterImmediate(RHICmdList, ShaderRHI,matrixCB,matrix_cbdata);

		if(SceneVolume.IsBound())
			RHICmdList.SetShaderResourceViewParameter(ShaderRHI,SceneVolume.GetBaseIndex(),sceneVolumeSRV);
		if(EmissiveVolume.IsBound())
			RHICmdList.SetShaderResourceViewParameter(ShaderRHI,EmissiveVolume.GetBaseIndex(),emissiveVolumeSRV);
		if(EmissivePalette.IsBound())
			RHICmdList.SetShaderResourceViewParameter(ShaderRHI,EmissivePalette.GetBaseIndex(),paletteSRV);
		if(LinearSampler.IsBound())
			RHICmdList.SetShaderSampler(ShaderRHI,LinearSampler.GetBaseIndex(),TStaticSamplerState<SF_Trilinear,AM_Wrap,AM_Wrap,AM_Wrap>::GetRHI());
		if(cmpSampler.IsBound())
			RHICmdList.SetShaderSampler(ShaderRHI,cmpSampler.GetBaseIndex(),TStaticSamplerState<SF_Trilinear,AM_Clamp,AM_Clamp,AM_Clamp,0,0,0,SCF_Less>::GetRHI());
		if(SamplingKernel.IsBound())
			RHICmdList.SetShaderResourceViewParameter(ShaderRHI,SamplingKernel.GetBaseIndex(),samplingKernelSRV);
		if(samPoint.IsBound())
			RHICmdList.SetShaderSampler(ShaderRHI,LinearSampler.GetBaseIndex(),TStaticSamplerState<SF_Point,AM_Wrap,AM_Wrap,AM_Wrap>::GetRHI());

		FSamplerStateRHIParamRef SamplerStateLinear  = TStaticSamplerState<SF_Bilinear,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI();

		if(ObjNormal.IsBound())
			RHICmdList.SetShaderResourceViewParameter(ShaderRHI,ObjNormal.GetBaseIndex(),AHREngine.ObjectNormalSRV);

		if(ShadowAlbedo0.IsBound() && lList[0].IsValid)
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowAlbedo0, LinearSampler, SamplerStateLinear,  lList[0].Albedo );
		else
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowAlbedo0, LinearSampler, SamplerStateLinear, dummyTexture);
		if(ShadowAlbedo1.IsBound() && lList[1].IsValid)
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowAlbedo1, LinearSampler, SamplerStateLinear,  lList[1].Albedo );
		else
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowAlbedo1, LinearSampler, SamplerStateLinear, dummyTexture);
		if(ShadowAlbedo2.IsBound() && lList[2].IsValid)
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowAlbedo2, LinearSampler, SamplerStateLinear,  lList[2].Albedo );
		else
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowAlbedo2, LinearSampler, SamplerStateLinear, dummyTexture);
		if(ShadowAlbedo3.IsBound() && lList[3].IsValid)
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowAlbedo3, LinearSampler, SamplerStateLinear,  lList[3].Albedo );
		else
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowAlbedo3, LinearSampler, SamplerStateLinear, dummyTexture);
		if(ShadowAlbedo4.IsBound() && lList[4].IsValid)
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowAlbedo4, LinearSampler, SamplerStateLinear,  lList[4].Albedo );
		else
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowAlbedo4, LinearSampler, SamplerStateLinear, dummyTexture);

		if(ShadowNormals0.IsBound() && lList[0].IsValid)
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowNormals0, LinearSampler, SamplerStateLinear,  lList[0].Normals );
		else
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowNormals0, LinearSampler, SamplerStateLinear, dummyTexture);
		if(ShadowNormals1.IsBound() && lList[1].IsValid)
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowNormals1, LinearSampler, SamplerStateLinear,  lList[1].Normals );
		else
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowNormals1, LinearSampler, SamplerStateLinear, dummyTexture);
		if(ShadowNormals2.IsBound() && lList[2].IsValid)
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowNormals2, LinearSampler, SamplerStateLinear,  lList[2].Normals );
		else
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowNormals2, LinearSampler, SamplerStateLinear, dummyTexture);
		if(ShadowNormals3.IsBound() && lList[3].IsValid)
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowNormals3, LinearSampler, SamplerStateLinear,  lList[3].Normals );
		else
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowNormals3, LinearSampler, SamplerStateLinear, dummyTexture);
		if(ShadowNormals4.IsBound() && lList[4].IsValid)
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowNormals4, LinearSampler, SamplerStateLinear,  lList[4].Normals );
		else
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowNormals4, LinearSampler, SamplerStateLinear, dummyTexture);

		if(ShadowZ0.IsBound() && lList[0].IsValid)
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowZ0, LinearSampler, SamplerStateLinear,  lList[0].Depth );
		else
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowZ0, LinearSampler, SamplerStateLinear, dummyTexture);
		if(ShadowZ1.IsBound() && lList[1].IsValid)
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowZ1, LinearSampler, SamplerStateLinear,  lList[1].Depth );
		else
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowZ1, LinearSampler, SamplerStateLinear, dummyTexture);
		if(ShadowZ2.IsBound() && lList[2].IsValid)
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowZ2, LinearSampler, SamplerStateLinear,  lList[2].Depth );
		else
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowZ2, LinearSampler, SamplerStateLinear, dummyTexture);
		if(ShadowZ3.IsBound() && lList[3].IsValid)
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowZ3, LinearSampler, SamplerStateLinear,  lList[3].Depth );
		else
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowZ3, LinearSampler, SamplerStateLinear, dummyTexture);
		if(ShadowZ4.IsBound() && lList[4].IsValid)
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowZ4, LinearSampler, SamplerStateLinear,  lList[4].Depth );
		else
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowZ4, LinearSampler, SamplerStateLinear, dummyTexture);
	}

	virtual bool Serialize(FArchive& Ar)
	{		
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << DeferredParameters;
		Ar << SceneVolume;
		Ar << LinearSampler;
		Ar << cmpSampler;
		Ar << cb;
		Ar << matrixCB;

		Ar << ShadowAlbedo0;
		Ar << ShadowAlbedo1;
		Ar << ShadowAlbedo2;
		Ar << ShadowAlbedo3;
		Ar << ShadowAlbedo4;

		Ar << ShadowNormals0;
		Ar << ShadowNormals1;
		Ar << ShadowNormals2;
		Ar << ShadowNormals3;
		Ar << ShadowNormals4;

		Ar << ShadowZ0;
		Ar << ShadowZ1;
		Ar << ShadowZ2;
		Ar << ShadowZ3;
		Ar << ShadowZ4;

		Ar << EmissiveVolume;
		Ar << EmissivePalette;
		
		Ar << SamplingKernel;
		Ar << samPoint;

		Ar << ObjNormal;
		return bShaderHasOutdatedParameters;
	}

	FGlobalBoundShaderState& GetBoundShaderState()
	{
		static FGlobalBoundShaderState State;

		return State;
	}

private:
	FDeferredPixelShaderParameters DeferredParameters;
	FShaderResourceParameter SceneVolume;
	FShaderResourceParameter LinearSampler;
	FShaderResourceParameter cmpSampler;
	TShaderUniformBufferParameter<AHRTraceSceneCB> cb;
	TShaderUniformBufferParameter<AHRShadowMatrices> matrixCB;

	FShaderResourceParameter ShadowAlbedo0;
	FShaderResourceParameter ShadowAlbedo1;
	FShaderResourceParameter ShadowAlbedo2;
	FShaderResourceParameter ShadowAlbedo3;
	FShaderResourceParameter ShadowAlbedo4;

	FShaderResourceParameter ShadowNormals0;
	FShaderResourceParameter ShadowNormals1;
	FShaderResourceParameter ShadowNormals2;
	FShaderResourceParameter ShadowNormals3;
	FShaderResourceParameter ShadowNormals4;

	FShaderResourceParameter ShadowZ0;
	FShaderResourceParameter ShadowZ1;
	FShaderResourceParameter ShadowZ2;
	FShaderResourceParameter ShadowZ3;
	FShaderResourceParameter ShadowZ4;

	FShaderResourceParameter EmissivePalette;
	FShaderResourceParameter EmissiveVolume;

	FShaderResourceParameter SamplingKernel;
	FShaderResourceParameter samPoint;

	FShaderResourceParameter ObjNormal;
};
IMPLEMENT_SHADER_TYPE(,AHRTraceScenePS,TEXT("AHRTraceScene"),TEXT("PS_SPHI"),SF_Pixel);

void FApproximateHybridRaytracer::TraceScene(FRHICommandListImmediate& RHICmdList,FViewInfo& View)
{
	SCOPED_DRAW_EVENT(RHICmdList,AHRTraceScene);
	RHICmdList.SetBlendState(TStaticBlendState<CW_RGBA>::GetRHI());

	// Draw a full screen quad into the half res target
	// Trace the GI and reflections if they are enabled

	// Set the viewport, raster state , depth stencil and render target
	SetRenderTarget(RHICmdList, RaytracingTarget, FTextureRHIRef());
	FIntRect SrcRect = View.ViewRect;
	FIntRect DestRect(FIntPoint(0,0),FIntPoint(RaytracingTarget->GetSizeX(),RaytracingTarget->GetSizeY()));
	RHICmdList.SetViewport(SrcRect.Min.X, SrcRect.Min.Y, 0.0f,RaytracingTarget->GetSizeX(), RaytracingTarget->GetSizeY(), 1.0f);
	//RHICmdList.SetViewport(0, 0, 0.0f,ResX/2, ResY/2, 1.0f);
	RHICmdList.SetRasterizerState(TStaticRasterizerState<FM_Solid, CM_None>::GetRHI());
	RHICmdList.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());
	
	// Clear the target before drawing
	RHICmdList.Clear(true, FLinearColor::Black, false, 1.0f, false, 0, FIntRect());

	// Get the shaders
	TShaderMapRef<AHRPassVS> VertexShader(View.ShaderMap);
	TShaderMapRef<AHRTraceScenePS> PixelShader(View.ShaderMap);

	// Bound shader parameters
	SetGlobalBoundShaderState(RHICmdList, View.FeatureLevel, PixelShader->GetBoundShaderState(),  GFilterVertexDeclaration.VertexDeclarationRHI, *VertexShader, *PixelShader);
	VertexShader->SetParameters(RHICmdList,View);
	// The dynamic grid should have both the static and dynamic data by now
	PixelShader->SetParameters(RHICmdList, View, 
								DynamicSceneVolume->SRV,
								EmissivePaletteSRV,
								DynamicEmissiveVolume->SRV,
								SamplingKernelSRV);

	// Draw a quad mapping scene color to the view's render target
	DrawRectangle( 
		RHICmdList,
		0, 0,
		DestRect.Width(), DestRect.Height(),
		SrcRect.Min.X, SrcRect.Min.Y, 
		SrcRect.Width(), SrcRect.Height(),
		DestRect.Size(),
		GSceneRenderTargets.GetBufferSizeXY(),
		*VertexShader,
		EDRF_UseTriangleOptimization);
	/*DrawRectangle( 
		RHICmdList,
		0, 0,
		ResX/2, ResY/2,
		0, 0, 
		ResX, ResY,
		FIntPoint(ResX/2,ResY/2),
		GSceneRenderTargets.GetBufferSizeXY(),
		*VertexShader,
		EDRF_UseTriangleOptimization);*/
}


///
/// Upsampling and composite
///
class AHRBlurH : public FGlobalShader
{
	DECLARE_SHADER_TYPE(AHRBlurH,Global)
public:

	static bool ShouldCache(EShaderPlatform Platform)
	{
		return IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(EShaderPlatform Platform, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Platform, OutEnvironment);
	}

	AHRBlurH(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
	:	FGlobalShader(Initializer)
	{
		DeferredParameters.Bind(Initializer.ParameterMap);
		GIBufferTexture.Bind(Initializer.ParameterMap, TEXT("tGI"));
		LinearSampler.Bind(Initializer.ParameterMap, TEXT("samLinear"));
		BlurKernelSize.Bind(Initializer.ParameterMap,TEXT("size"));
		zMax.Bind(Initializer.ParameterMap,TEXT("zMax"));
		NormalTex.Bind(Initializer.ParameterMap,TEXT("NormalTex"));
	}

	AHRBlurH()
	{
	}

	void SetParameters(FRHICommandList& RHICmdList, const FSceneView& View, const FShaderResourceViewRHIRef giSRV,float blurKernelSize)
	{
		const FPixelShaderRHIParamRef ShaderRHI = GetPixelShader();
		FGlobalShader::SetParameters(RHICmdList, ShaderRHI,View);
		DeferredParameters.Set(RHICmdList, ShaderRHI, View);

		if(GIBufferTexture.IsBound())
			RHICmdList.SetShaderResourceViewParameter(ShaderRHI,GIBufferTexture.GetBaseIndex(),giSRV);
		if(NormalTex.IsBound())
			RHICmdList.SetShaderResourceViewParameter(ShaderRHI,NormalTex.GetBaseIndex(),AHREngine.ObjectNormalSRV);
		if(LinearSampler.IsBound())
			RHICmdList.SetShaderSampler(ShaderRHI,LinearSampler.GetBaseIndex(),TStaticSamplerState<SF_Trilinear,AM_Wrap,AM_Wrap,AM_Wrap>::GetRHI());

		SetShaderValue(RHICmdList, ShaderRHI, BlurKernelSize,blurKernelSize);
		SetShaderValue(RHICmdList, ShaderRHI, zMax,View.PixelToWorld(0,0,1).Z);
	}

	virtual bool Serialize(FArchive& Ar)
	{		
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << DeferredParameters;
		Ar << GIBufferTexture;
		Ar << LinearSampler;
		Ar << BlurKernelSize;
		Ar << zMax;
		Ar << NormalTex;
		return bShaderHasOutdatedParameters;
	}

	FGlobalBoundShaderState& GetBoundShaderState()
	{
		static FGlobalBoundShaderState State;

		return State;
	}

private:
	FDeferredPixelShaderParameters DeferredParameters;
	FShaderResourceParameter GIBufferTexture;
	FShaderResourceParameter NormalTex;
	FShaderResourceParameter LinearSampler;
	FShaderParameter BlurKernelSize;
	FShaderParameter zMax;
};
class AHRBlurV : public FGlobalShader
{
	DECLARE_SHADER_TYPE(AHRBlurV,Global)
public:

	static bool ShouldCache(EShaderPlatform Platform)
	{
		return IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(EShaderPlatform Platform, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Platform, OutEnvironment);
	}

	AHRBlurV(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
	:	FGlobalShader(Initializer)
	{
		DeferredParameters.Bind(Initializer.ParameterMap);
		GIBufferTexture.Bind(Initializer.ParameterMap, TEXT("tGI"));
		LinearSampler.Bind(Initializer.ParameterMap, TEXT("samLinear"));
		BlurKernelSize.Bind(Initializer.ParameterMap,TEXT("size"));
		zMax.Bind(Initializer.ParameterMap,TEXT("zMax"));
		NormalTex.Bind(Initializer.ParameterMap,TEXT("NormalTex"));
	}

	AHRBlurV()
	{
	}

	void SetParameters(FRHICommandList& RHICmdList, const FSceneView& View, const FShaderResourceViewRHIRef giSRV,float blurKernelSize)
	{
		const FPixelShaderRHIParamRef ShaderRHI = GetPixelShader();
		FGlobalShader::SetParameters(RHICmdList, ShaderRHI,View);
		DeferredParameters.Set(RHICmdList, ShaderRHI, View);

		if(GIBufferTexture.IsBound())
			RHICmdList.SetShaderResourceViewParameter(ShaderRHI,GIBufferTexture.GetBaseIndex(),giSRV);
		if(NormalTex.IsBound())
			RHICmdList.SetShaderResourceViewParameter(ShaderRHI,NormalTex.GetBaseIndex(),AHREngine.ObjectNormalSRV);
		if(LinearSampler.IsBound())
			RHICmdList.SetShaderSampler(ShaderRHI,LinearSampler.GetBaseIndex(),TStaticSamplerState<SF_Trilinear,AM_Wrap,AM_Wrap,AM_Wrap>::GetRHI());

		SetShaderValue(RHICmdList, ShaderRHI, BlurKernelSize,blurKernelSize);
		SetShaderValue(RHICmdList, ShaderRHI, zMax,View.PixelToWorld(0,0,1).Z);
	}

	virtual bool Serialize(FArchive& Ar)
	{		
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << DeferredParameters;
		Ar << GIBufferTexture;
		Ar << LinearSampler;
		Ar << BlurKernelSize;
		Ar << zMax;
		Ar << NormalTex;
		return bShaderHasOutdatedParameters;
	}

	FGlobalBoundShaderState& GetBoundShaderState()
	{
		static FGlobalBoundShaderState State;

		return State;
	}

private:
	FDeferredPixelShaderParameters DeferredParameters;
	FShaderResourceParameter GIBufferTexture;
	FShaderResourceParameter NormalTex;
	FShaderResourceParameter LinearSampler;
	FShaderParameter BlurKernelSize;
	FShaderParameter zMax;
};
//IMPLEMENT_SHADER_TYPE(,AHRUpsamplePS,TEXT("AHRUpsample"),TEXT("PS"),SF_Pixel);
IMPLEMENT_SHADER_TYPE(,AHRBlurH,TEXT("AHRUpsample"),TEXT("BlurH"),SF_Pixel);
IMPLEMENT_SHADER_TYPE(,AHRBlurV,TEXT("AHRUpsample"),TEXT("BlurV"),SF_Pixel);

void FApproximateHybridRaytracer::Upsample(FRHICommandListImmediate& RHICmdList,FViewInfo& View)
{
	SCOPED_DRAW_EVENT(RHICmdList,AHRUpsample);

	RHICmdList.SetBlendState(TStaticBlendState<CW_RGBA>::GetRHI());
	RHICmdList.SetRasterizerState(TStaticRasterizerState<FM_Solid, CM_None>::GetRHI());
	RHICmdList.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());
	RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f,RaytracingTarget->GetSizeX(), RaytracingTarget->GetSizeY(), 1.0f);

	FIntRect SrcRect = View.ViewRect;
	FIntRect DestRect(FIntPoint(0,0),FIntPoint(RaytracingTarget->GetSizeX(),RaytracingTarget->GetSizeY()));

	// Get the shaders
	TShaderMapRef<AHRPassVS> VertexShader(View.ShaderMap);
	TShaderMapRef<AHRBlurH> PSBlurH(View.ShaderMap);
	TShaderMapRef<AHRBlurV> PSBlurV(View.ShaderMap);

	///////// Pass 0
	SetRenderTarget(RHICmdList, UpsampledTarget0, FTextureRHIRef());

	// Clear the target before drawing
	RHICmdList.Clear(true, FLinearColor::Black, false, 1.0f, false, 0, FIntRect());

	SetGlobalBoundShaderState(RHICmdList, View.FeatureLevel, PSBlurH->GetBoundShaderState(),  GFilterVertexDeclaration.VertexDeclarationRHI, *VertexShader, *PSBlurH);
	VertexShader->SetParameters(RHICmdList,View);
	PSBlurH->SetParameters(RHICmdList, View, RaytracingTargetSRV,1.0);

	// Draw!
	DrawRectangle( 
		RHICmdList,
		0, 0,
		DestRect.Width(), DestRect.Height(),
		SrcRect.Min.X, SrcRect.Min.Y, 
		SrcRect.Width(), SrcRect.Height(),
		DestRect.Size(),
		GSceneRenderTargets.GetBufferSizeXY(),
		*VertexShader,
		EDRF_UseTriangleOptimization);

	///////// Pass 1
	SetRenderTarget(RHICmdList, UpsampledTarget1, FTextureRHIRef());

	// Clear the target before drawing
	RHICmdList.Clear(true, FLinearColor::Black, false, 1.0f, false, 0, FIntRect());

	SetGlobalBoundShaderState(RHICmdList, View.FeatureLevel, PSBlurV->GetBoundShaderState(),  GFilterVertexDeclaration.VertexDeclarationRHI, *VertexShader, *PSBlurV);
	VertexShader->SetParameters(RHICmdList,View);
	PSBlurV->SetParameters(RHICmdList, View, UpsampledTargetSRV0,1.0);

	// Draw!
	DrawRectangle( 
		RHICmdList,
		0, 0,
		DestRect.Width(), DestRect.Height(),
		SrcRect.Min.X, SrcRect.Min.Y, 
		SrcRect.Width(), SrcRect.Height(),
		DestRect.Size(),
		GSceneRenderTargets.GetBufferSizeXY(),
		*VertexShader,
		EDRF_UseTriangleOptimization);
#define TWO_PASS_BLUR
#ifdef TWO_PASS_BLUR
	///////// Pass 2
	SetRenderTarget(RHICmdList, UpsampledTarget0, FTextureRHIRef());

	// Clear the target before drawing
	RHICmdList.Clear(true, FLinearColor::Black, false, 1.0f, false, 0, FIntRect());

	SetGlobalBoundShaderState(RHICmdList, View.FeatureLevel, PSBlurH->GetBoundShaderState(),  GFilterVertexDeclaration.VertexDeclarationRHI, *VertexShader, *PSBlurH);
	VertexShader->SetParameters(RHICmdList,View);
	PSBlurH->SetParameters(RHICmdList, View, UpsampledTargetSRV1,1.0);

	// Draw!
	DrawRectangle( 
		RHICmdList,
		0, 0,
		DestRect.Width(), DestRect.Height(),
		SrcRect.Min.X, SrcRect.Min.Y, 
		SrcRect.Width(), SrcRect.Height(),
		DestRect.Size(),
		GSceneRenderTargets.GetBufferSizeXY(),
		*VertexShader,
		EDRF_UseTriangleOptimization);

	///////// Pass 3
	SetRenderTarget(RHICmdList, UpsampledTarget1, FTextureRHIRef());

	// Clear the target before drawing
	RHICmdList.Clear(true, FLinearColor::Black, false, 1.0f, false, 0, FIntRect());

	SetGlobalBoundShaderState(RHICmdList, View.FeatureLevel, PSBlurV->GetBoundShaderState(),  GFilterVertexDeclaration.VertexDeclarationRHI, *VertexShader, *PSBlurV);
	VertexShader->SetParameters(RHICmdList,View);
	PSBlurV->SetParameters(RHICmdList, View, UpsampledTargetSRV0,1.0);

	// Draw!
	DrawRectangle( 
		RHICmdList,
		0, 0,
		DestRect.Width(), DestRect.Height(),
		SrcRect.Min.X, SrcRect.Min.Y, 
		SrcRect.Width(), SrcRect.Height(),
		DestRect.Size(),
		GSceneRenderTargets.GetBufferSizeXY(),
		*VertexShader,
		EDRF_UseTriangleOptimization);
#endif
}

BEGIN_UNIFORM_BUFFER_STRUCT(AHRCompositeCB,)
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(float,GIMultiplier)
END_UNIFORM_BUFFER_STRUCT(AHRCompositeCB)
IMPLEMENT_UNIFORM_BUFFER_STRUCT(AHRCompositeCB,TEXT("AHRCompositeCB"));

class AHRCompositePS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(AHRCompositePS,Global)
public:

	static bool ShouldCache(EShaderPlatform Platform)
	{
		return IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(EShaderPlatform Platform, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Platform, OutEnvironment);
	}

	AHRCompositePS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
	:	FGlobalShader(Initializer)
	{
		DeferredParameters.Bind(Initializer.ParameterMap);
		GIBufferTexture.Bind(Initializer.ParameterMap, TEXT("tGI"));
		LinearSampler.Bind(Initializer.ParameterMap, TEXT("samLinear"));
		cb.Bind(Initializer.ParameterMap,TEXT("AHRCompositeCB"));
		ObjNormal.Bind(Initializer.ParameterMap,TEXT("ObjNormal"));
	}

	AHRCompositePS()
	{
	}

	void SetParameters(FRHICommandList& RHICmdList, const FSceneView& View, const FShaderResourceViewRHIRef giSRV)
	{
		const FPixelShaderRHIParamRef ShaderRHI = GetPixelShader();
		FGlobalShader::SetParameters(RHICmdList, ShaderRHI,View);
		DeferredParameters.Set(RHICmdList, ShaderRHI, View);

		if(GIBufferTexture.IsBound())
			RHICmdList.SetShaderResourceViewParameter(ShaderRHI,GIBufferTexture.GetBaseIndex(),giSRV);
		if(LinearSampler.IsBound())
			RHICmdList.SetShaderSampler(ShaderRHI,LinearSampler.GetBaseIndex(),TStaticSamplerState<SF_Trilinear,AM_Wrap,AM_Wrap,AM_Wrap>::GetRHI());
		if(ObjNormal.IsBound())
			RHICmdList.SetShaderResourceViewParameter(ShaderRHI,ObjNormal.GetBaseIndex(),AHREngine.ObjectNormalSRV);
		AHRCompositeCB cbdata;

		cbdata.GIMultiplier = View.FinalPostProcessSettings.AHRIntensity;

		SetUniformBufferParameterImmediate(RHICmdList, ShaderRHI,cb,cbdata);
	}

	virtual bool Serialize(FArchive& Ar)
	{		
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << DeferredParameters;
		Ar << GIBufferTexture;
		Ar << LinearSampler;
		Ar << cb;
		Ar << ObjNormal;
		return bShaderHasOutdatedParameters;
	}

	FGlobalBoundShaderState& GetBoundShaderState()
	{
		static FGlobalBoundShaderState State;
		
		return State;
	}

private:
	FDeferredPixelShaderParameters DeferredParameters;
	FShaderResourceParameter GIBufferTexture;
	FShaderResourceParameter LinearSampler;
	FShaderResourceParameter ObjNormal;
	TShaderUniformBufferParameter<AHRCompositeCB> cb;
};
IMPLEMENT_SHADER_TYPE(,AHRCompositePS,TEXT("AHRComposite"),TEXT("PS"),SF_Pixel);

void FApproximateHybridRaytracer::Composite(FRHICommandListImmediate& RHICmdList,FViewInfo& View)
{
	SCOPED_DRAW_EVENT(RHICmdList,AHRComposite);

	// Simply render a full screen quad and sample the upsampled buffer. Use additive blending to mix it with the light accumulation buffer
	// Only one view at a time for now (1/11/2014)

	// Set additive blending
	//RHICmdList.SetBlendState(TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One>::GetRHI());
	RHICmdList.SetBlendState(TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero>::GetRHI());

	// add gi and multiply scene color by ao
	// final = gi + ao*direct
	//RHICmdList.SetBlendState(TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_SourceAlpha, BO_Add, BF_One, BF_One>::GetRHI());


	//		DEBUG!!!!!
	//RHICmdList.SetBlendState(TStaticBlendState<CW_RGBA, BO_Add, BF_Zero, BF_SourceColor, BO_Add, BF_One, BF_One>::GetRHI());



	// Set the viewport, raster state and depth stencil
	RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);
	//RHICmdList.SetViewport(0, 0, 0.0f,ResX, ResY, 1.0f);
	RHICmdList.SetRasterizerState(TStaticRasterizerState<FM_Solid, CM_None>::GetRHI());
	RHICmdList.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());

	// Get the shaders
	TShaderMapRef<AHRPassVS> VertexShader(View.ShaderMap);
	TShaderMapRef<AHRCompositePS> PixelShader(View.ShaderMap);

	// Bound shader parameters
	SetGlobalBoundShaderState(RHICmdList, View.FeatureLevel, PixelShader->GetBoundShaderState(),  GFilterVertexDeclaration.VertexDeclarationRHI, *VertexShader, *PixelShader);
	VertexShader->SetParameters(RHICmdList,View);
	PixelShader->SetParameters(RHICmdList, View, UpsampledTargetSRV0);
	//PixelShader->SetParameters(RHICmdList, View, RaytracingTargetSRV);

	// Draw!
	DrawRectangle( 
				RHICmdList,
				0, 0,
				View.ViewRect.Width(), View.ViewRect.Height(),
				View.ViewRect.Min.X, View.ViewRect.Min.Y, 
				View.ViewRect.Width(), View.ViewRect.Height(),
				View.ViewRect.Size(),
				GSceneRenderTargets.GetBufferSizeXY(),
				*VertexShader,
				EDRF_UseTriangleOptimization);
	/*	DrawRectangle( 
		RHICmdList,
		0, 0,
		ResX, ResY,
		0, 0, 
		ResX, ResY,
		FIntPoint(ResX,ResY),
		GSceneRenderTargets.GetBufferSizeXY(),
		*VertexShader,
		EDRF_UseTriangleOptimization);*/
}