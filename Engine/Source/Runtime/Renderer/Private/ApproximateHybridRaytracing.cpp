// @RyanTorant
#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "ApproximateHybridRaytracing.h"
#include "SceneUtils.h"

TGlobalResource<FApproximateHybridRaytracer> AHREngine;

TAutoConsoleVariable<int32> CVarApproximateHybridRaytracing = TAutoConsoleVariable<int32>(
	TEXT("r.ApproximateHybridRaytracing"),
	0,
	TEXT("Project setting of the work in progress feature Approximate Hybrid Raytracing. Cannot be changed at runtime.\n")
	TEXT(" 0 : off (default)\n")
	TEXT(" 1 : on"),
	ECVF_RenderThreadSafe | ECVF_ReadOnly);

TAutoConsoleVariable<int32> CVarAHRMaxSliceSize = TAutoConsoleVariable<int32>(
	TEXT("r.AHRMaxSliceSize"),
	512,
	TEXT("Max slice size for the voxel grids. Default value is 512"),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarAHRTraceReflections = TAutoConsoleVariable<int32>(
	TEXT("r.AHRTraceReflections"),
	0,
	TEXT("Trace reflections or not using AHR.\n")
	TEXT(" 0 : off (default)\n")
	TEXT(" 1 : on"),
	ECVF_RenderThreadSafe);


void FApproximateHybridRaytracer::UpdateSettings()
{
	check(IsInRenderingThread());

	bool changed = false;

	{
		static FIntVector prevRes(-1,-1,-1);
		static FCriticalSection cs2;

		// Make this thread-safe
		FScopeLock ScopeLock(&cs2);

		if(prevRes != gridSettings.SliceSize)
			changed = true;
		prevRes = gridSettings.SliceSize;
	}

	if(changed)
	{
		if(DynamicSceneVolume)
		{
			// Destroy the volume
			DynamicSceneVolume->Release();
			// ... and recreate it
			// Try to keep the dynamic scene volume on fast vram, as it is small, and it's the one that will get bounded to the shader at the end
			DynamicSceneVolume->Initialize(gridSettings.SliceSize.X*gridSettings.SliceSize.Y*gridSettings.SliceSize.Z/32*4,BUF_FastVRAM);
		}
		if(StaticSceneVolume)
		{
			// Destroy the volume
			StaticSceneVolume->Release();
			// ... and recreate it
			StaticSceneVolume->Initialize(gridSettings.SliceSize.X*gridSettings.SliceSize.Y*gridSettings.SliceSize.Z/32*4);
		}
		
		if(DynamicEmissiveVolume)
		{
			DynamicEmissiveVolume->Release();
			DynamicEmissiveVolume->Initialize(gridSettings.SliceSize.X*gridSettings.SliceSize.Y*gridSettings.SliceSize.Z/4*4);
		}
		if(StaticEmissiveVolume)
		{
			StaticEmissiveVolume->Release();
			StaticEmissiveVolume->Initialize(gridSettings.SliceSize.X*gridSettings.SliceSize.Y*gridSettings.SliceSize.Z/4*4);
		}
		/*
		// Destroy the emissive grid and recreate
		StaticEmissiveVolume.SafeRelease();
		DynamicEmissiveVolume.SafeRelease();
		StaticEmissiveVolumeSRV.SafeRelease();
		DynamicEmissiveVolumeSRV.SafeRelease();
		StaticEmissiveVolumeUAV.SafeRelease();
		DynamicEmissiveVolumeUAV.SafeRelease();

		FRHIResourceCreateInfo createInfo;
		StaticEmissiveVolume = RHICreateTexture3D(gridSettings.SliceSize.X,gridSettings.SliceSize.Y,gridSettings.SliceSize.Z,PF_R8_UINT,1,TexCreate_UAV | TexCreate_ShaderResource,createInfo);
		DynamicEmissiveVolume = RHICreateTexture3D(gridSettings.SliceSize.X,gridSettings.SliceSize.Y,gridSettings.SliceSize.Z,PF_R8_UINT,1,TexCreate_UAV | TexCreate_ShaderResource,createInfo);
		
		StaticEmissiveVolumeSRV = RHICreateShaderResourceView(StaticEmissiveVolume,0);
		DynamicEmissiveVolumeSRV = RHICreateShaderResourceView(DynamicEmissiveVolume,0);

		StaticEmissiveVolumeUAV = RHICreateUnorderedAccessView(StaticEmissiveVolume);
		DynamicEmissiveVolumeUAV = RHICreateUnorderedAccessView(DynamicEmissiveVolume);*/
	}
}
/*
void FApproximateHybridRaytracer::ClearGrids(FRHICommandListImmediate& RHICmdList)
{
	SCOPED_DRAW_EVENT(RHICmdList,AHRInternalClearGrids);

	uint32 cls[4] = { 0,0,0,0 };
	RHICmdList.ClearUAV(SceneVolume->UAV, cls);
}
*/
bool UseApproximateHybridRaytracingRT(ERHIFeatureLevel::Type InFeatureLevel)
{
	if(InFeatureLevel < ERHIFeatureLevel::SM5 || !AHREngine.IsInitialized())
	{
		_DEBUG_MSG("oops, cant use AHR. What are you trying to do?");
		return false;
	}

	int32 Value = CVarApproximateHybridRaytracing.GetValueOnRenderThread();

	return Value != 0;
}


// 
//	RHI code
//

void FApproximateHybridRaytracer::InitDynamicRHI()
{
	// On start, init it to 256
	gridSettings.SliceSize = FIntVector(256,256,256);
	gridSettings.Bounds = FVector(1000,1000,1000);
	gridSettings.Center = FVector(0,0,0);

	StaticSceneVolume = new FRWBufferByteAddress;
	StaticSceneVolume->Initialize(gridSettings.SliceSize.X*gridSettings.SliceSize.Y*gridSettings.SliceSize.Z/32*4);

	DynamicSceneVolume = new FRWBufferByteAddress;
	DynamicSceneVolume->Initialize(gridSettings.SliceSize.X*gridSettings.SliceSize.Y*gridSettings.SliceSize.Z/32*4,BUF_FastVRAM);

	DynamicEmissiveVolume = new FRWBufferByteAddress;
	DynamicEmissiveVolume->Initialize(gridSettings.SliceSize.X*gridSettings.SliceSize.Y*gridSettings.SliceSize.Z/4*4);

	StaticEmissiveVolume = new FRWBufferByteAddress;
	StaticEmissiveVolume->Initialize(gridSettings.SliceSize.X*gridSettings.SliceSize.Y*gridSettings.SliceSize.Z/4*4);
	/*
	FRHIResourceCreateInfo CreateInfo;
	StaticEmissiveVolume = RHICreateTexture3D(gridSettings.SliceSize.X,gridSettings.SliceSize.Y,gridSettings.SliceSize.Z,PF_R8_UINT,1,TexCreate_UAV | TexCreate_ShaderResource,CreateInfo);
	DynamicEmissiveVolume = RHICreateTexture3D(gridSettings.SliceSize.X,gridSettings.SliceSize.Y,gridSettings.SliceSize.Z,PF_R8_UINT,1,TexCreate_UAV | TexCreate_ShaderResource,CreateInfo);
		
	StaticEmissiveVolumeSRV = RHICreateShaderResourceView(StaticEmissiveVolume,0);
	DynamicEmissiveVolumeSRV = RHICreateShaderResourceView(DynamicEmissiveVolume,0);

	StaticEmissiveVolumeUAV = RHICreateUnorderedAccessView(StaticEmissiveVolume);
	DynamicEmissiveVolumeUAV = RHICreateUnorderedAccessView(DynamicEmissiveVolume);*/

	// Create the sampling kernel
	// SIZE MUST BE A POWER OF 2
	FRHIResourceCreateInfo CreateInfo;
	SamplingKernel = RHICreateTexture2D(8,8,PF_R32_UINT,1,1,TexCreate_ShaderResource,CreateInfo);

	// same kernel, rotated  
	uint32 kernel[64] = {
							906459532, 72893876, 492795701, 532344103,    1740361530, 226519499, 1570237062, 389231761,
							326635764, 417976112, 293516711, 189627760,   705480491, 423810863, 187018483, 239779822,
							705480491, 423810863, 187018483, 239779822,   26635764, 417976112, 293516711, 189627760,
							1740361530, 226519499, 1570237062, 389231761, 906459532, 72893876, 492795701, 532344103,

							532344103, 492795701, 906459532, 72893876,    389231761, 1570237062, 1740361530, 226519499,
							189627760, 293516711, 326635764, 417976112,   239779822, 187018483, 705480491, 423810863, 
							239779822, 187018483, 705480491, 423810863,   189627760, 293516711, 326635764, 417976112,
							389231761, 1570237062, 1740361530, 226519499, 532344103, 492795701, 906459532, 72893876
						};
	uint32 Stride = 0;
	uint32* textureData = (uint32*)RHILockTexture2D( SamplingKernel, 0, RLM_WriteOnly, Stride, false );
	FMemory::Memcpy(textureData, kernel, 64*4);
	RHIUnlockTexture2D( SamplingKernel, 0, false );

	SamplingKernelSRV = RHICreateShaderResourceView(SamplingKernel,0);
}


void FApproximateHybridRaytracer::ReleaseDynamicRHI()
{
	// Destroy grids
	if(StaticSceneVolume)
	{
		StaticSceneVolume->Release();
		delete StaticSceneVolume;
	}
	if(DynamicSceneVolume)
	{
		DynamicSceneVolume->Release();
		delete DynamicSceneVolume;
	}
}

void FApproximateHybridRaytracer::AppendLightRSM(LightRSMData& light)
{
	if(currentLightIDX < MAX_AHR_LIGHTS)
	{
		lights[currentLightIDX] = light;
		currentLightIDX++;
	}
	else
	{
		UE_LOG(LogRenderer, Warning, TEXT("Tried to add more lights to the AHR engine that the maximum supported, will be ignored."));
	}
}