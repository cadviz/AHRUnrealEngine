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
	TEXT(" 0: off (default)\n")
	TEXT(" 1: on"),
	ECVF_RenderThreadSafe | ECVF_ReadOnly);

TAutoConsoleVariable<int32> CVarAHRVoxelSliceSize = TAutoConsoleVariable<int32>(
	TEXT("r.AHRSliceSize"),
	512,
	TEXT("Slice size for the voxel grids. Default value is 512\n"),
	ECVF_RenderThreadSafe);

void FApproximateHybridRaytracer::UpdateSettings()
{
	check(IsInRenderingThread());

	if(CVarAHRVoxelSliceSize.AsVariable()->TestFlags(ECVF_Changed))
	{
		CVarAHRVoxelSliceSize.AsVariable()->ClearFlags(ECVF_Changed);

		uint32 vSliceSize = CVarAHRVoxelSliceSize.GetValueOnAnyThread();
		if(SceneVolume)
		{
			// Destroy the volume
			SceneVolume->Release();
			// ... and recreate it
			SceneVolume->Initialize(vSliceSize*vSliceSize*vSliceSize/32*4);
			_DEBUG_MSG("Changed grid res");
		}
	}
}



void FApproximateHybridRaytracer::ClearGrids(FRHICommandListImmediate& RHICmdList)
{
	SCOPED_DRAW_EVENT(RHICmdList,AHRInternalClearGrids, DEC_SCENE_ITEMS);

	uint32 cls[4] = { 0,0,0,0 };
	RHICmdList.ClearUAV(SceneVolume->UAV, cls);
}

bool UseApproximateHybridRaytracingRT(ERHIFeatureLevel::Type InFeatureLevel)
{
	if(!IsFeatureLevelSupported(GRHIShaderPlatform, ERHIFeatureLevel::SM5) || !AHREngine.IsInitialized())
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
	uint32 vSliceSize = CVarAHRVoxelSliceSize.GetValueOnAnyThread();

	SceneVolume = new FRWBufferByteAddress;
	SceneVolume->Initialize(vSliceSize*vSliceSize*vSliceSize/32*4);
}


void FApproximateHybridRaytracer::ReleaseDynamicRHI()
{
	// Destroy grids
	if(SceneVolume)
		SceneVolume->Release();
	delete SceneVolume;
}