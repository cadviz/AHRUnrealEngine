#pragma once
// @RyanTorant

// small macro
#define _DEBUG_MSG(msg) ::MessageBoxA(nullptr,__FUNCTION__##" "##msg,"DEBUG",MB_ICONWARNING)

extern TAutoConsoleVariable<int32> CVarApproximateHybridRaytracing;
extern TAutoConsoleVariable<int32> CVarAHRVoxelSliceSize;
extern TAutoConsoleVariable<int32> CVarAHRTraceReflections;

// Main class
class FApproximateHybridRaytracer : public FRenderResource
{
public:
	// Constructor
	FApproximateHybridRaytracer() : FRenderResource(ERHIFeatureLevel::SM5)
	{
		SceneVolume = nullptr;
		ResX = ResY = -1;
	}

	// Main pipeline functions
	void InitializeViewTargets(uint32 ResX,uint32 ResY);
	void VoxelizeScene(FRHICommandListImmediate& RHICmdList);
	void TraceScene(FRHICommandListImmediate& RHICmdList,FViewInfo& View);
	void Upsample(FRHICommandListImmediate& RHICmdList,FViewInfo& View);
	void Composite(FRHICommandListImmediate& RHICmdList,FViewInfo& View);

	// Data functions
	void UpdateSettings(); // Resizes the grid if needed
	void ClearGrids(FRHICommandListImmediate& RHICmdList);

	// FRenderResource code : Mainly, InitDynamicRHI()/ReleaseDynamicRHI(). Also, IsInitialized()
	void InitDynamicRHI() override final;
	void ReleaseDynamicRHI() override final;
private:
	FRWBufferByteAddress* SceneVolume;

	FTexture2DRHIRef RaytracingTarget;
	FTexture2DRHIRef UpsampledTarget;
	FShaderResourceViewRHIRef RaytracingTargetSRV;
	FShaderResourceViewRHIRef UpsampledTargetSRV;

	uint32 ResX,ResY;
};

extern TGlobalResource<FApproximateHybridRaytracer> AHREngine;

// use for render thread only
bool UseApproximateHybridRaytracingRT(ERHIFeatureLevel::Type InFeatureLevel);
