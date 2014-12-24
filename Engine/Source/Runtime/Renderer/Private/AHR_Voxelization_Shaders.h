// @RyanTorant
#pragma once
#include "ShaderBaseClasses.h"
#include "ApproximateHybridRaytracing.h"

BEGIN_UNIFORM_BUFFER_STRUCT(AHRVoxelizationCB,)
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(FVector2D,ScreenRes)

	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(uint32,SliceSize)
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(FVector,InvSceneBounds)
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(FVector,WorldToVoxelOffset) // -SceneCenter/SceneBounds
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(FVector,invVoxel)
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(float,TriangleSizeMultiplier)
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

		cbdata.SliceSize = CVarAHRVoxelSliceSize.GetValueOnRenderThread();
		cbdata.ScreenRes.X = View.Family->FamilySizeX/2;
		cbdata.ScreenRes.Y = View.Family->FamilySizeY/2;
		cbdata.invVoxel = FVector(1.0f / float(cbdata.SliceSize));

		cbdata.InvSceneBounds = FVector(1.0f) / View.FinalPostProcessSettings.AHRSceneScale;
		cbdata.WorldToVoxelOffset = -FVector(View.FinalPostProcessSettings.AHRSceneCenterX,View.FinalPostProcessSettings.AHRSceneCenterY,View.FinalPostProcessSettings.AHRSceneCenterZ)*cbdata.InvSceneBounds; // -SceneCenter/SceneBounds
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

		cbdata.SliceSize = CVarAHRVoxelSliceSize.GetValueOnRenderThread();
		cbdata.ScreenRes.X = View->Family->FamilySizeX/2;
		cbdata.ScreenRes.Y = View->Family->FamilySizeY/2;
		cbdata.invVoxel = FVector(1.0f / float(cbdata.SliceSize));

		cbdata.InvSceneBounds = FVector(1.0f) / View->FinalPostProcessSettings.AHRSceneScale;
		cbdata.WorldToVoxelOffset = -FVector(View->FinalPostProcessSettings.AHRSceneCenterX,View->FinalPostProcessSettings.AHRSceneCenterY,View->FinalPostProcessSettings.AHRSceneCenterZ)*cbdata.InvSceneBounds; // -SceneCenter/SceneBounds
		
		SetUniformBufferParameterImmediate(RHICmdList, ShaderRHI,cb,cbdata);
	}

	void SetMesh(FRHICommandList& RHICmdList, const FVertexFactory* VertexFactory,const FSceneView& View,const FPrimitiveSceneProxy* Proxy,const FMeshBatchElement& BatchElement)
	{
		FMeshMaterialShader::SetMesh(RHICmdList, GetPixelShader(),VertexFactory,View,Proxy,BatchElement);
	}

	virtual bool Serialize(FArchive& Ar)
	{
		bool bShaderHasOutdatedParameters = FMeshMaterialShader::Serialize(Ar);
		Ar << cb;
		return bShaderHasOutdatedParameters;
	}

private:
	TShaderUniformBufferParameter<AHRVoxelizationCB> cb;
};