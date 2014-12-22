// @RyanTorant
#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "ApproximateHybridRaytracing.h"
#include "SceneUtils.h"
#include "SceneFilterRendering.h"
#include "AHR_Voxelization.h"

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

void  FApproximateHybridRaytracer::InitializeViewTargets(uint32 _resX,uint32 _resY)
{	
	if(_resX != ResX || _resY != ResY && _resX >= 128 && _resY >= 128) // If you are rendering to a target less than 128x128, you are doing it wrong. This is to bypass auxiliary views
	{
		// Store size
		ResX = _resX; ResY = _resY;

		// The view size changed, so we have to rebuild the targets
		FRHIResourceCreateInfo CreateInfo;

		// PF_FloatRGBA is 16 bits float per component. Nice documentation Epic ...
		RaytracingTarget = RHICreateTexture2D(ResX/2,ResY/2,PF_FloatRGBA,1,1,TexCreate_RenderTargetable | TexCreate_ShaderResource,CreateInfo);
		UpsampledTarget = RHICreateTexture2D(ResX,ResY,PF_FloatRGBA,1,1,TexCreate_RenderTargetable | TexCreate_ShaderResource,CreateInfo);

		RaytracingTargetSRV = RHICreateShaderResourceView(RaytracingTarget,0);
		UpsampledTargetSRV = RHICreateShaderResourceView(UpsampledTarget,0);
	}
}

void FApproximateHybridRaytracer::VoxelizeScene(FRHICommandListImmediate& RHICmdList,FViewInfo& View)
{
	SCOPED_DRAW_EVENT(RHICmdList,AHRVoxelizeScene);

	// Voxelize the objects to the binary grid
	if( View.PrimitivesToVoxelize.Num( ) > 0 )
	{
		TAHRVoxelizerElementPDI<FAHRVoxelizerDrawingPolicyFactory> Drawer(
			&View, FAHRVoxelizerDrawingPolicyFactory::ContextType(RHICmdList) );

		for( int32 PrimitiveIndex = 0, Num = View.PrimitivesToVoxelize.Num( ); PrimitiveIndex < Num; PrimitiveIndex++ )
		{
			const FPrimitiveSceneInfo* PrimitiveSceneInfo = View.PrimitivesToVoxelize[ PrimitiveIndex ];
			
			FScopeCycleCounter Context( PrimitiveSceneInfo->Proxy->GetStatId( ) );
			Drawer.SetPrimitive( PrimitiveSceneInfo->Proxy );
			
			// Calls SceneProxy DrawDynamicElements function
			PrimitiveSceneInfo->Proxy->DrawDynamicElements(&Drawer,&View,EDrawDynamicFlags::Voxelize);
		}
	}
}

///
/// Tracing
///
BEGIN_UNIFORM_BUFFER_STRUCT(AHRTraceSceneCB,)
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(FVector2D,ScreenRes)

	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(uint32,SliceSize)
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
	}

	AHRTraceScenePS()
	{
	}

	void SetParameters(FRHICommandList& RHICmdList, const FSceneView& View, const FShaderResourceViewRHIRef sceneVolumeSRV)
	{
		const FPixelShaderRHIParamRef ShaderRHI = GetPixelShader();
		FGlobalShader::SetParameters(RHICmdList, ShaderRHI,View);
		DeferredParameters.Set(RHICmdList, ShaderRHI, View);

		AHRTraceSceneCB cbdata;

		cbdata.SliceSize = CVarAHRVoxelSliceSize.GetValueOnRenderThread();
		cbdata.ScreenRes.X = View.Family->FamilySizeX/2;
		cbdata.ScreenRes.Y = View.Family->FamilySizeY/2;
		cbdata.invVoxel = FVector(1.0f / float(cbdata.SliceSize));

		cbdata.InvSceneBounds = FVector(1.0f) / View.FinalPostProcessSettings.AHRSceneScale;
		cbdata.WorldToVoxelOffset = -FVector(View.FinalPostProcessSettings.AHRSceneCenterX,View.FinalPostProcessSettings.AHRSceneCenterY,View.FinalPostProcessSettings.AHRSceneCenterZ)*cbdata.InvSceneBounds; // -SceneCenter/SceneBounds
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

		if(SceneVolume.IsBound())
			RHICmdList.SetShaderResourceViewParameter(ShaderRHI,SceneVolume.GetBaseIndex(),sceneVolumeSRV);
		if(LinearSampler.IsBound())
			RHICmdList.SetShaderSampler(ShaderRHI,LinearSampler.GetBaseIndex(),TStaticSamplerState<SF_Trilinear,AM_Wrap,AM_Wrap,AM_Wrap>::GetRHI());
	}

	virtual bool Serialize(FArchive& Ar)
	{		
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << DeferredParameters;
		Ar << SceneVolume;
		Ar << LinearSampler;
		Ar << cb;
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
	TShaderUniformBufferParameter<AHRTraceSceneCB> cb;
};
IMPLEMENT_SHADER_TYPE(,AHRTraceScenePS,TEXT("AHRTraceScene"),TEXT("PS"),SF_Pixel);

void FApproximateHybridRaytracer::TraceScene(FRHICommandListImmediate& RHICmdList,FViewInfo& View)
{
	SCOPED_DRAW_EVENT(RHICmdList,AHRTraceScene);

	// Draw a full screen quad into the half res target
	// Trace the GI and reflections if they are enabled

	// Set the viewport, raster state , depth stencil and render target
	SetRenderTarget(RHICmdList, RaytracingTarget, FTextureRHIRef());
	FIntRect SrcRect = View.ViewRect;
	FIntRect DestRect = SrcRect/2;
	RHICmdList.SetViewport(SrcRect.Min.X, SrcRect.Min.Y, 0.0f,DestRect.Max.X, DestRect.Max.Y, 1.0f);
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
	PixelShader->SetParameters(RHICmdList, View, SceneVolume->SRV);

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
}


///
/// Upsampling and composite
///
class AHRUpsamplePS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(AHRUpsamplePS,Global)
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

	AHRUpsamplePS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
	:	FGlobalShader(Initializer)
	{
		DeferredParameters.Bind(Initializer.ParameterMap);
		GIBufferTexture.Bind(Initializer.ParameterMap, TEXT("tGI"));
		LinearSampler.Bind(Initializer.ParameterMap, TEXT("samLinear"));
	}

	AHRUpsamplePS()
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
	}

	virtual bool Serialize(FArchive& Ar)
	{		
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << DeferredParameters;
		Ar << GIBufferTexture;
		Ar << LinearSampler;
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
};
IMPLEMENT_SHADER_TYPE(,AHRUpsamplePS,TEXT("AHRUpsample"),TEXT("PS"),SF_Pixel);

void FApproximateHybridRaytracer::Upsample(FRHICommandListImmediate& RHICmdList,FViewInfo& View)
{
	SCOPED_DRAW_EVENT(RHICmdList,AHRUpsample);

	// Set the viewport, raster state , depth stencil and render target
	SetRenderTarget(RHICmdList, UpsampledTarget, FTextureRHIRef());
	RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);
	RHICmdList.SetRasterizerState(TStaticRasterizerState<FM_Solid, CM_None>::GetRHI());
	RHICmdList.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());

	// Clear the target before drawing
	RHICmdList.Clear(true, FLinearColor::Black, false, 1.0f, false, 0, FIntRect());

	// Get the shaders
	TShaderMapRef<AHRPassVS> VertexShader(View.ShaderMap);
	TShaderMapRef<AHRUpsamplePS> PixelShader(View.ShaderMap);

	// Bound shader parameters
	SetGlobalBoundShaderState(RHICmdList, View.FeatureLevel, PixelShader->GetBoundShaderState(),  GFilterVertexDeclaration.VertexDeclarationRHI, *VertexShader, *PixelShader);
	VertexShader->SetParameters(RHICmdList,View);
	PixelShader->SetParameters(RHICmdList, View, RaytracingTargetSRV);

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
	RHICmdList.SetRasterizerState(TStaticRasterizerState<FM_Solid, CM_None>::GetRHI());
	RHICmdList.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());

	// Get the shaders
	TShaderMapRef<AHRPassVS> VertexShader(View.ShaderMap);
	TShaderMapRef<AHRCompositePS> PixelShader(View.ShaderMap);

	// Bound shader parameters
	SetGlobalBoundShaderState(RHICmdList, View.FeatureLevel, PixelShader->GetBoundShaderState(),  GFilterVertexDeclaration.VertexDeclarationRHI, *VertexShader, *PixelShader);
	VertexShader->SetParameters(RHICmdList,View);
	PixelShader->SetParameters(RHICmdList, View, UpsampledTargetSRV); // just binds the upsampled texture using SetTextureParameter()

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
}