// @RyanTorant
#pragma once
#include "ShaderBaseClasses.h"
#include "ApproximateHybridRaytracing.h"

BEGIN_UNIFORM_BUFFER_STRUCT(AHRVoxelizationCB,)
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(FVector2D,ScreenRes)

	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(FIntVector,SliceSize)
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(FVector,InvSceneBounds)
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(FVector,WorldToVoxelOffset) // -SceneCenter/SceneBounds
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(FVector,invVoxel)
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(float,TriangleSizeMultiplier)

	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(FMatrix,ShadowMatrix0)
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(FMatrix,ShadowMatrix1)
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(FMatrix,ShadowMatrix2)
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(FMatrix,ShadowMatrix3)
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(FMatrix,ShadowMatrix4)

	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(FVector2D,ShadowViewportScaling0)
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(FVector2D,ShadowViewportScaling1)
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(FVector2D,ShadowViewportScaling2)
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(FVector2D,ShadowViewportScaling3)
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(FVector2D,ShadowViewportScaling4)
END_UNIFORM_BUFFER_STRUCT(AHRVoxelizationCB)


class FAHRVoxelizationVertexShader : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FAHRVoxelizationVertexShader,MeshMaterial);

protected:
	FAHRVoxelizationVertexShader() {}
	FAHRVoxelizationVertexShader(const FMeshMaterialShaderType::CompiledShaderInitializerType& Initializer):
	FMeshMaterialShader(Initializer)
	{
	}

public:
	static bool ShouldCache(EShaderPlatform Platform,const FMaterial* Material,const FVertexFactoryType* VertexFactoryType)
	{
		return IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(EShaderPlatform Platform, const FMaterial* Material, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMeshMaterialShader::ModifyCompilationEnvironment(Platform, Material, OutEnvironment);
	}

	virtual bool Serialize(FArchive& Ar)
	{
		bool bShaderHasOutdatedParameters = FMeshMaterialShader::Serialize(Ar);
		return bShaderHasOutdatedParameters;
	}

	void SetParameters(
		FRHICommandList& RHICmdList, 
		const FMaterialRenderProxy* MaterialRenderProxy,
		const FVertexFactory* VertexFactory,
		const FMaterial& InMaterialResource,
		const FSceneView& View
		)
	{
		FMeshMaterialShader::SetParameters(RHICmdList, GetVertexShader(),MaterialRenderProxy,InMaterialResource,View, ESceneRenderTargetsMode::DontSet);
	}

	void SetMesh(FRHICommandList& RHICmdList, const FVertexFactory* VertexFactory,const FSceneView& View,const FPrimitiveSceneProxy* Proxy,const FMeshBatchElement& BatchElement)
	{
		FMeshMaterialShader::SetMesh(RHICmdList, GetVertexShader(),VertexFactory,View,Proxy,BatchElement);
	}

private:
};

class FAHRVoxelizationGeometryShader : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FAHRVoxelizationGeometryShader,MeshMaterial);

protected:
	FAHRVoxelizationGeometryShader() {}
	FAHRVoxelizationGeometryShader(const FMeshMaterialShaderType::CompiledShaderInitializerType& Initializer):
	FMeshMaterialShader(Initializer)
	{
		cb.Bind(Initializer.ParameterMap, TEXT("AHRVoxelizationCB"));
	}

public:
	static bool ShouldCache(EShaderPlatform Platform,const FMaterial* Material,const FVertexFactoryType* VertexFactoryType)
	{
		return IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(EShaderPlatform Platform, const FMaterial* Material, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMeshMaterialShader::ModifyCompilationEnvironment(Platform, Material, OutEnvironment);
	}

	virtual bool Serialize(FArchive& Ar)
	{
		bool bShaderHasOutdatedParameters = FMeshMaterialShader::Serialize(Ar);
		Ar << cb;
		return bShaderHasOutdatedParameters;
	}

	void SetParameters(
		FRHICommandList& RHICmdList, 
		const FMaterialRenderProxy* MaterialRenderProxy,
		const FVertexFactory* VertexFactory,
		const FMaterial& InMaterialResource,
		const FSceneView& View
		)
	{
		//FMeshMaterialShader::SetParameters(RHICmdList, GetVertexShader(),MaterialRenderProxy,InMaterialResource,View,ESceneRenderTargetsMode::DontSet);
		const auto ShaderRHI = GetGeometryShader();
		AHRVoxelizationCB cbdata;

		auto gridCFG = AHREngine.GetGridSettings();
		cbdata.SliceSize.X = gridCFG.SliceSize.X;
		cbdata.SliceSize.Y = gridCFG.SliceSize.Y;
		cbdata.SliceSize.Z = gridCFG.SliceSize.Z;
		cbdata.ScreenRes.X = View.Family->FamilySizeX/2;
		cbdata.ScreenRes.Y = View.Family->FamilySizeY/2;
		cbdata.invVoxel = FVector(1.0f / float(gridCFG.SliceSize.X),
								  1.0f / float(gridCFG.SliceSize.Y),
								  1.0f / float(gridCFG.SliceSize.Z));

		cbdata.InvSceneBounds = FVector(1.0f) / gridCFG.Bounds;
		cbdata.WorldToVoxelOffset = -gridCFG.Center*cbdata.InvSceneBounds; // -SceneCenter/SceneBounds
		cbdata.TriangleSizeMultiplier = View.FinalPostProcessSettings.TriangleSizeMultiplier;

		SetUniformBufferParameterImmediate(RHICmdList, ShaderRHI,cb,cbdata);
	}

	void SetMesh(FRHICommandList& RHICmdList, const FVertexFactory* VertexFactory,const FSceneView& View,const FPrimitiveSceneProxy* Proxy,const FMeshBatchElement& BatchElement)
	{
		//FMeshMaterialShader::SetMesh(RHICmdList, GetVertexShader(),VertexFactory,View,Proxy,BatchElement);
	}

private:
	TShaderUniformBufferParameter<AHRVoxelizationCB> cb;
};

class FAHRVoxelizationPixelShader : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FAHRVoxelizationPixelShader,MeshMaterial);

public:

	static bool ShouldCache(EShaderPlatform Platform,const FMaterial* Material,const FVertexFactoryType* VertexFactoryType)
	{
		return IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(EShaderPlatform Platform, const FMaterial* Material, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMeshMaterialShader::ModifyCompilationEnvironment(Platform, Material, OutEnvironment);
	}

	/** Initialization constructor. */
	FAHRVoxelizationPixelShader(const FMeshMaterialShaderType::CompiledShaderInitializerType& Initializer):
		FMeshMaterialShader(Initializer)
	{
		cb.Bind(Initializer.ParameterMap, TEXT("AHRVoxelizationCB"));
		ShadowDepth0.Bind(Initializer.ParameterMap, TEXT("ShadowDepth0"));
		pointSampler.Bind(Initializer.ParameterMap, TEXT("pointSampler"));
	}
	FAHRVoxelizationPixelShader() {}

	void SetParameters(
		FRHICommandList& RHICmdList, 
		const FMaterialRenderProxy* MaterialRenderProxy, 
		const FMaterial& MaterialResource, 
		const FSceneView* View)
	{
		const FPixelShaderRHIParamRef ShaderRHI = GetPixelShader();

		FMeshMaterialShader::SetParameters(RHICmdList, ShaderRHI, MaterialRenderProxy, MaterialResource, *View, ESceneRenderTargetsMode::DontSet);

		AHRVoxelizationCB cbdata;
		auto gridCFG = AHREngine.GetGridSettings();
		cbdata.SliceSize.X = gridCFG.SliceSize.X;
		cbdata.SliceSize.Y = gridCFG.SliceSize.Y;
		cbdata.SliceSize.Z = gridCFG.SliceSize.Z;
		cbdata.ScreenRes.X = View->Family->FamilySizeX/2;
		cbdata.ScreenRes.Y = View->Family->FamilySizeY/2;
		cbdata.invVoxel = FVector(1.0f / float(gridCFG.SliceSize.X),
								  1.0f / float(gridCFG.SliceSize.Y),
								  1.0f / float(gridCFG.SliceSize.Z));

		cbdata.InvSceneBounds = FVector(1.0f) / gridCFG.Bounds;
		cbdata.WorldToVoxelOffset = -gridCFG.Center*cbdata.InvSceneBounds; // -SceneCenter/SceneBounds
		cbdata.TriangleSizeMultiplier = View->FinalPostProcessSettings.TriangleSizeMultiplier;

		cbdata.ShadowMatrix0 = AHREngine.GetLightsList()[0].ViewProj;
		cbdata.ShadowViewportScaling0 = AHREngine.GetLightsList()[0].ViewportScaling;

		SetUniformBufferParameterImmediate(RHICmdList, ShaderRHI,cb,cbdata);

		auto sampler = TStaticSamplerState<SF_Point,AM_Border,AM_Border,AM_Border,0,0,0,SCF_Never>::GetRHI();
		SetTextureParameter(RHICmdList, ShaderRHI, ShadowDepth0, pointSampler,sampler, AHREngine.GetShadowTexture(0));		
		
		if(pointSampler.IsBound())
			RHICmdList.SetShaderSampler(ShaderRHI,pointSampler.GetBaseIndex(),sampler);
	}

	void SetMesh(FRHICommandList& RHICmdList, const FVertexFactory* VertexFactory,const FSceneView& View,const FPrimitiveSceneProxy* Proxy,const FMeshBatchElement& BatchElement)
	{
		FMeshMaterialShader::SetMesh(RHICmdList, GetPixelShader(),VertexFactory,View,Proxy,BatchElement);
	}

	virtual bool Serialize(FArchive& Ar)
	{
		bool bShaderHasOutdatedParameters = FMeshMaterialShader::Serialize(Ar);
		Ar << cb;
		Ar << ShadowDepth0;
		Ar << pointSampler;
		return bShaderHasOutdatedParameters;
	}

private:
	TShaderUniformBufferParameter<AHRVoxelizationCB> cb;
	FShaderResourceParameter ShadowDepth0;
	FShaderResourceParameter pointSampler;
};