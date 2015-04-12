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
			DynamicSceneVolume->Initialize((gridSettings.SliceSize.X*gridSettings.SliceSize.Y*gridSettings.SliceSize.Z/32)*4,BUF_FastVRAM);
		}
		if(StaticSceneVolume)
		{
			// Destroy the volume
			StaticSceneVolume->Release();
			// ... and recreate it
			StaticSceneVolume->Initialize((gridSettings.SliceSize.X*gridSettings.SliceSize.Y*gridSettings.SliceSize.Z/32)*4);
		}
		
		if(DynamicEmissiveVolume)
		{
			DynamicEmissiveVolume->Release();
			DynamicEmissiveVolume->Initialize((gridSettings.SliceSize.X/2)*(gridSettings.SliceSize.Y/2)*(gridSettings.SliceSize.Z/2)*4);
		}
		if(StaticEmissiveVolume)
		{
			StaticEmissiveVolume->Release();
			DynamicEmissiveVolume->Initialize((gridSettings.SliceSize.X/2)*(gridSettings.SliceSize.Y/2)*(gridSettings.SliceSize.Z/2)*4);
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
	StaticSceneVolume->Initialize((gridSettings.SliceSize.X*gridSettings.SliceSize.Y*gridSettings.SliceSize.Z/32)*4);

	DynamicSceneVolume = new FRWBufferByteAddress;
	DynamicSceneVolume->Initialize((gridSettings.SliceSize.X*gridSettings.SliceSize.Y*gridSettings.SliceSize.Z/32)*4,BUF_FastVRAM);

	DynamicEmissiveVolume = new FRWBufferByteAddress;
	DynamicEmissiveVolume->Initialize((gridSettings.SliceSize.X/2)*(gridSettings.SliceSize.Y/2)*(gridSettings.SliceSize.Z/2)*4);

	StaticEmissiveVolume = new FRWBufferByteAddress;
	StaticEmissiveVolume->Initialize((gridSettings.SliceSize.X/2)*(gridSettings.SliceSize.Y/2)*(gridSettings.SliceSize.Z/2)*4);
	/*
	FRHIResourceCreateInfo CreateInfo;
	StaticEmissiveVolume = RHICreateTexture3D(gridSettings.SliceSize.X,gridSettings.SliceSize.Y,gridSettings.SliceSize.Z,PF_R8_UINT,1,TexCreate_UAV | TexCreate_ShaderResource,CreateInfo);
	DynamicEmissiveVolume = RHICreateTexture3D(gridSettings.SliceSize.X,gridSettings.SliceSize.Y,gridSettings.SliceSize.Z,PF_R8_UINT,1,TexCreate_UAV | TexCreate_ShaderResource,CreateInfo);
		
	StaticEmissiveVolumeSRV = RHICreateShaderResourceView(StaticEmissiveVolume,0);
	DynamicEmissiveVolumeSRV = RHICreateShaderResourceView(DynamicEmissiveVolume,0);
	StaticEmissiveVolumeUAV = RHICreateUnorderedAccessView(StaticEmissiveVolume);
	DynamicEmissiveVolumeUAV = RHICreateUnorderedAccessView(DynamicEmissiveVolume);*/

	// Create the sampling kernel
	FRHIResourceCreateInfo CreateInfo;
	for(auto& k : SamplingKernel) k = RHICreateTexture2D(4,4,PF_A32B32G32R32F,1,1,TexCreate_ShaderResource,CreateInfo);

	// same kernel, rotated  
	/*uint32 kernel[64] = {
							906459532, 72893876, 492795701, 532344103,    1740361530, 226519499, 1570237062, 389231761,
							326635764, 417976112, 293516711, 189627760,   705480491, 423810863, 187018483, 239779822,
							705480491, 423810863, 187018483, 239779822,   26635764, 417976112, 293516711, 189627760,
							1740361530, 226519499, 1570237062, 389231761, 906459532, 72893876, 492795701, 532344103,

							532344103, 492795701, 906459532, 72893876,    389231761, 1570237062, 1740361530, 226519499,
							189627760, 293516711, 326635764, 417976112,   239779822, 187018483, 705480491, 423810863, 
							239779822, 187018483, 705480491, 423810863,   189627760, 293516711, 326635764, 417976112,
							389231761, 1570237062, 1740361530, 226519499, 532344103, 492795701, 906459532, 72893876
						};*/

	// Hardcoded ray directions
	FVector kernel[16][6] = { { FVector( 0.000, 0.000, 1.000), FVector(-0.310, 0.826, 0.470), FVector(-0.882,-0.394, 0.470), FVector( 0.761,-0.313, 0.568), FVector(-0.030,-0.742, 0.670), FVector( 0.476, 0.760, 0.443) },
							  { FVector( 0.335,-0.194, 0.922), FVector(-0.587, 0.349, 0.731), FVector(-0.005, 0.401, 0.916), FVector(-0.556,-0.480, 0.671), FVector( 0.315,-0.859, 0.405), FVector( 0.855, 0.331, 0.399) },
							  { FVector(-0.149,-0.036, 0.988), FVector( 0.472,-0.632, 0.614), FVector(-0.821, 0.435, 0.370), FVector( 0.129, 0.893, 0.431), FVector( 0.904,-0.195, 0.381), FVector(-0.297,-0.913, 0.280) },
							  { FVector(-0.203,-0.148, 0.968), FVector( 0.240, 0.321, 0.916), FVector(-0.948, 0.162, 0.276), FVector(-0.094, 0.936, 0.340), FVector( 0.890, 0.153, 0.431), FVector( 0.023,-0.920, 0.390) },
							  { FVector( 0.166, 0.120, 0.979), FVector( 0.309, 0.762, 0.569), FVector( 0.840,-0.117, 0.531), FVector(-0.186,-0.706, 0.683), FVector(-0.886,-0.234, 0.399), FVector(-0.575, 0.687, 0.443) },
							  { FVector( 0.048,-0.352, 0.935), FVector( 0.460, 0.334, 0.823), FVector(-0.018,-0.942, 0.334), FVector(-0.813,-0.295, 0.502), FVector(-0.362, 0.734, 0.575), FVector( 0.942,-0.214, 0.260) },
							  { FVector( 0.335,-0.194, 0.922), FVector(-0.586, 0.349, 0.731), FVector(-0.005, 0.401, 0.916), FVector( 0.315,-0.859, 0.405), FVector(-0.556,-0.480, 0.678), FVector( 0.855, 0.331, 0.399) },
							  { FVector( 0.166, 0.120, 0.979), FVector( 0.310, 0.762, 0.570), FVector(-0.186,-0.706, 0.683), FVector( 0.839,-0.117, 0.531), FVector(-0.575, 0.687, 0.443), FVector(-0.886,-0.234, 0.399) },
							  { FVector( 0.017, 0.055, 0.998), FVector( 0.160,-0.915, 0.370), FVector(-0.251, 0.903, 0.347), FVector( 0.782, 0.518, 0.347), FVector( 0.882,-0.262, 0.391), FVector(-0.880,-0.344, 0.326) },
							  { FVector( 0.142,-0.059, 0.988), FVector(-0.189, 0.214, 0.958), FVector( 0.061,-0.894, 0.443), FVector( 0.155, 0.955, 0.251), FVector(-0.876, 0.118, 0.467), FVector( 0.957, 0.148, 0.251) },
							  { FVector( 0.251, 0.000, 0.968), FVector(-0.263, 0.038, 0.963), FVector( 0.088,-0.962, 0.259), FVector(-0.447, 0.850, 0.276), FVector( 0.914, 0.034, 0.405), FVector(-0.824,-0.507, 0.253) },
							  { FVector( 0.048,-0.352, 0.935), FVector( 0.460, 0.334, 0.823), FVector(-0.362, 0.734, 0.575), FVector(-0.813,-0.295, 0.502), FVector(-0.018,-0.942, 0.334), FVector( 0.942,-0.214, 0.259) },
							  { FVector(-0.149,-0.108, 0.983), FVector( 0.550,-0.400, 0.433), FVector(-0.387,-0.679, 0.624), FVector( 0.951,-0.036, 0.308), FVector(-0.737, 0.627, 0.253), FVector( 0.410, 0.891, 0.194) },
							  { FVector( 0.037,-0.113, 0.993), FVector(-0.810,-0.118, 0.575), FVector( 0.712, 0.321, 0.624), FVector(-0.138, 0.951, 0.276), FVector( 0.568,-0.752, 0.334), FVector(-0.615,-0.763, 0.198) },
							  { FVector(-0.030,-0.920, 0.995), FVector( 0.706, 0.126, 0.696), FVector( 0.671,-0.688, 0.276), FVector(-0.447,-0.851, 0.276), FVector(-0.829, 0.468, 0.306), FVector( 0.536, 0.820, 0.198) },
							  { FVector(-0.097, 0.000, 0.995), FVector(-0.272, 0.442, 0.854), FVector(-0.625, 0.041, 0.780), FVector( 0.650,-0.128, 0.749), FVector( 0.417, 0.681, 0.602), FVector(-0.701, 0.510, 0.498) } };
	/*uint32 kernel[64] = {
							906459532, 728938764, 492795701, 532344103,   11437521, 124754451, 27327375, 23346752,
							326635764, 417976112, 293516711, 189627760,   45761216, 634347625, 45375374, 56437894,
							705480491, 423810863, 187018483, 239779822,   52166884, 564134894, 45372160, 57689721,
							174036153, 226519499, 157023706, 389231761,   56724678, 457672353, 49279570, 46813854,
							124553125, 124237531, 123627311, 782517244,   45721414, 247871735, 24273253, 25660846,
							245722674, 293516711, 457378294, 133742285,   42475215, 112439156, 65431564, 78724276, 
							122543457, 556344767, 133132185, 515826467,   73892245, 475372377, 51656548, 45737847,
							457238567, 121245334, 122574753, 427677346,   64277353, 235353445, 54165863, 24524245
						};*/
	
	for(int n = 0;n < 6;n++)
	{
		uint32 Stride = 0;

		float* textureData = (float*)RHILockTexture2D( SamplingKernel[n], 0, RLM_WriteOnly, Stride, false );

		for(int i = 0;i < 16*4;i++)
		{
			textureData[i  ] = kernel[i][n].X;
			textureData[i+1] = kernel[i][n].Y;
			textureData[i+2] = kernel[i][n].Z;
			textureData[i+3] = 0;

			i += 4;
		}

		RHIUnlockTexture2D( SamplingKernel[n], 0, false );
	}
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
	if(DynamicEmissiveVolume)
	{
		DynamicEmissiveVolume->Release();
		delete DynamicEmissiveVolume;
	}
	if(StaticEmissiveVolume)
	{
		StaticEmissiveVolume->Release();
		delete StaticEmissiveVolume;
	}
}

void FApproximateHybridRaytracer::AppendLight(const AHRLightData& light)
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
