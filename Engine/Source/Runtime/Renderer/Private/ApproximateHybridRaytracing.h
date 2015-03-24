#pragma once
// @RyanTorant

// small macro
#define _DEBUG_MSG(msg) ::MessageBoxA(nullptr,__FUNCTION__##" "##msg,"DEBUG",MB_ICONWARNING)
#define MAX_AHR_LIGHTS 5

extern TAutoConsoleVariable<int32> CVarApproximateHybridRaytracing;
extern TAutoConsoleVariable<int32> CVarAHRMaxSliceSize;
extern TAutoConsoleVariable<int32> CVarAHRTraceReflections;

struct AHRLightData
{
	AHRLightData()
	{
		IsValid = false;
		//Albedo = Normals = Depth = nullptr;
		Depth = nullptr;
	}
	bool IsValid;
	/*FTexture2DRHIRef Albedo;
	FTexture2DRHIRef Normals;*/
	FTexture2DRHIRef Depth;
	FMatrix ViewProj;
	FVector Offset;
};
struct AHRGridSettings
{
	FVector Bounds;
	FVector Center;
	float VoxelSize;
	FIntVector SliceSize;
};

inline int fceil(int a,int b)
{
	return (a + b - 1)/b;
}

// Main class
class FApproximateHybridRaytracer : public FRenderResource
{
public:
	// Constructor
	FApproximateHybridRaytracer() : FRenderResource(ERHIFeatureLevel::SM5)
	{
		currentVolume = nullptr;
		StaticSceneVolume = DynamicSceneVolume = nullptr;
		ResX = ResY = -1;
		currentLightIDX = 0;
	}

	// Main pipeline functions
	void StartFrame(FViewInfo& View);
	void VoxelizeScene(FRHICommandListImmediate& RHICmdList,FViewInfo& View);
	void TraceScene(FRHICommandListImmediate& RHICmdList,FViewInfo& View);
	void Upsample(FRHICommandListImmediate& RHICmdList,FViewInfo& View);
	void Composite(FRHICommandListImmediate& RHICmdList,FViewInfo& View);

	// Data functions
	void UpdateSettings(); // Resizes the grid if needed
	//void ClearGrids(FRHICommandListImmediate& RHICmdList);
	void SetGridSettings(AHRGridSettings settings){ gridSettings = settings; }
	AHRGridSettings GetGridSettings(){ return gridSettings; }
	void SetStaticVolumeAsActive(){ currentVolume = &StaticSceneVolume; currentEmissiveVolume = &StaticEmissiveVolume; }
	void SetDynamicVolumeAsActive(){ currentVolume = &DynamicSceneVolume; currentEmissiveVolume = &DynamicEmissiveVolume; }
	FUnorderedAccessViewRHIRef GetSceneVolumeUAV(){ return (*currentVolume)->UAV; }
	FUnorderedAccessViewRHIRef GetEmissiveVolumeUAV(){ return (*currentEmissiveVolume)->UAV; }

	void AppendLight(AHRLightData& light);
	AHRLightData* GetLightsList(){ return lights; }

	// FRenderResource code : Mainly, InitDynamicRHI()/ReleaseDynamicRHI(). Also, IsInitialized()
	void InitDynamicRHI() override final;
	void ReleaseDynamicRHI() override final;

	// Public because ..., I'm Batman!
	FTexture2DRHIRef RaytracingTarget;
	FTexture2DRHIRef UpsampledTarget0;
	FTexture2DRHIRef UpsampledTarget1;
	FTexture2DRHIRef DownsampledNormal;
	FShaderResourceViewRHIRef RaytracingTargetSRV;
	FShaderResourceViewRHIRef UpsampledTargetSRV0;
	FShaderResourceViewRHIRef UpsampledTargetSRV1;
	FShaderResourceViewRHIRef ObjectNormalSRV;

	FRWBufferByteAddress* StaticSceneVolume;
	FRWBufferByteAddress* DynamicSceneVolume;
	uint32 ResX,ResY;
private:
	FRWBufferByteAddress** currentVolume; // ptr-to-ptr to remember people that this is JUST AN UTILITY! IT IS NOT THE ACTUAL VOLUME!
	FRWBufferByteAddress** currentEmissiveVolume;

	/* 
	// Not using a texture3D as:
	// a) ClearUAV causes a BSOD on tex3D with format R8_UINT on AMD hardware. I reported the bug at the 15/1/2015
	// b) I should be faster to use a raw buffer, as the emissive clear should be faster. Unprofiled

	FTexture3DRHIRef StaticEmissiveVolume;
	FTexture3DRHIRef DynamicEmissiveVolume;
	FShaderResourceViewRHIRef StaticEmissiveVolumeSRV;
	FShaderResourceViewRHIRef DynamicEmissiveVolumeSRV;
	FUnorderedAccessViewRHIRef StaticEmissiveVolumeUAV;
	FUnorderedAccessViewRHIRef DynamicEmissiveVolumeUAV;*/

	FRWBufferByteAddress* StaticEmissiveVolume;
	FRWBufferByteAddress* DynamicEmissiveVolume;

	FTexture2DRHIRef EmissivePaletteTexture;
	FShaderResourceViewRHIRef EmissivePaletteSRV;
	FTexture2DRHIRef SamplingKernel;
	FShaderResourceViewRHIRef SamplingKernelSRV;
	AHRLightData lights[MAX_AHR_LIGHTS];
	uint32 currentLightIDX;

	AHRGridSettings gridSettings;
};

extern TGlobalResource<FApproximateHybridRaytracer> AHREngine;

// use for render thread only
bool UseApproximateHybridRaytracingRT(ERHIFeatureLevel::Type InFeatureLevel);
