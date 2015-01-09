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

TAutoConsoleVariable<int32> CVarAHRVoxelSliceSize = TAutoConsoleVariable<int32>(
	TEXT("r.AHRSliceSize"),
	512,
	TEXT("Slice size for the voxel grids. Default value is 512"),
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

	if(CVarAHRVoxelSliceSize.AsVariable()->TestFlags(ECVF_Changed))
	{
		CVarAHRVoxelSliceSize.AsVariable()->ClearFlags(ECVF_Changed);

		uint32 vSliceSize = CVarAHRVoxelSliceSize.GetValueOnAnyThread();
		if(DynamicSceneVolume)
		{
			// Destroy the volume
			DynamicSceneVolume->Release();
			// ... and recreate it
			// Try to keep the dynamic scene volume on fast vram, as it is small, and it's the one that will get bounded to the shader at the end
			DynamicSceneVolume->Initialize(vSliceSize*vSliceSize*vSliceSize/32*4,BUF_FastVRAM);
		}
		if(StaticSceneVolume)
		{
			// Destroy the volume
			StaticSceneVolume->Release();
			// ... and recreate it
			StaticSceneVolume->Initialize(vSliceSize*vSliceSize*vSliceSize/32*4);
		}
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

int32 AHRGetVoxelResolution()
{
	return CVarAHRVoxelSliceSize.GetValueOnAnyThread();
}

// 
//	RHI code
//

void FApproximateHybridRaytracer::InitDynamicRHI()
{
	uint32 vSliceSize = CVarAHRVoxelSliceSize.GetValueOnAnyThread();

	StaticSceneVolume = new FRWBufferByteAddress;
	StaticSceneVolume->Initialize(vSliceSize*vSliceSize*vSliceSize/32*4);
	DynamicSceneVolume = new FRWBufferByteAddress;
	DynamicSceneVolume->Initialize(vSliceSize*vSliceSize*vSliceSize/32*4);
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