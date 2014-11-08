// @RyanTorant
#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "ApproximateHybridRaytracing.h"
#include "SceneUtils.h"
#include "SceneFilterRendering.h"

void  FApproximateHybridRaytracer::InitializeViewTargets(uint32 _resX,uint32 _resY)
{	
	if(_resX != ResX || _resY != ResY && _resX >= 128 && _resY >= 128) // If you are rendering to a target less than 128x128, you are doing it wrong. This is to bypass auxiliary views
	{
		// Store size
		ResX = _resX; ResY = _resY;

		// The view size changed, so we have to rebuild the targets
		FRHIResourceCreateInfo CreateInfo;

		RaytracingTarget = RHICreateTexture2D(ResX/2,ResY/2,PF_A32B32G32R32F,1,1,TexCreate_ShaderResource | TexCreate_UAV,CreateInfo);
		UpsampledTarget = RHICreateTexture2D(ResX,ResY,PF_A32B32G32R32F,1,1,TexCreate_ShaderResource | TexCreate_UAV,CreateInfo);

		RaytracingTargetSRV = RHICreateShaderResourceView(RaytracingTarget,0);
		UpsampledTargetSRV = RHICreateShaderResourceView(UpsampledTarget,0);

		RaytracingTargetUAV = RHICreateUnorderedAccessView(RaytracingTarget);
		UpsampledTargetUAV = RHICreateUnorderedAccessView(UpsampledTarget);
	}
}

void FApproximateHybridRaytracer::VoxelizeScene(FRHICommandListImmediate& RHICmdList)
{
	SCOPED_DRAW_EVENT(RHICmdList,AHRVoxelizeScene, DEC_SCENE_ITEMS);

	// Voxelize the objects to the binary grid
	// For now (8/11/2014) the grid is fixed to the origin, and both static and dynamic objects get voxelized every frame
}

///
/// Tracing
///
void FApproximateHybridRaytracer::TraceScene(FRHICommandListImmediate& RHICmdList,FViewInfo& View)
{
	SCOPED_DRAW_EVENT(RHICmdList,AHRTraceScene, DEC_SCENE_ITEMS);

	// Dispatch a compute shader that computes indirect illumination on a half resolution buffer
}


///
/// Upsampling and composite
///
// Constant buffers
BEGIN_UNIFORM_BUFFER_STRUCT( AHRUpsampleConstantBuffer, )
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER( float, ScreenX )
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER( float, ScreenY )
END_UNIFORM_BUFFER_STRUCT( AHRUpsampleConstantBuffer )
IMPLEMENT_UNIFORM_BUFFER_STRUCT(AHRUpsampleConstantBuffer,TEXT("AHRUpCB"));
typedef TUniformBuffer<AHRUpsampleConstantBuffer> FAHRUpsampleCB;

class AHRUpsampleCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(AHRUpsampleCS, Global);

	static bool ShouldCache(EShaderPlatform Platform)
	{
		return IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(EShaderPlatform Platform, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Platform, OutEnvironment);
		//OutEnvironment.CompilerFlags.Add( CFLAG_StandardOptimization ); <- Just seen this once. Shall I use it?
	}

	/** Default constructor. */
	AHRUpsampleCS() {}

public:

	/** Initialization constructor. */
	AHRUpsampleCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		GIBufferTexture.Bind(Initializer.ParameterMap, TEXT("tGI"));
		LinearSampler.Bind(Initializer.ParameterMap, TEXT("samLinear"));
		OutBuff.Bind( Initializer.ParameterMap, TEXT("output") );
	}

	void SetCS(FRHICommandList& RHICmdList, const FViewInfo& View, const FShaderResourceViewRHIRef giSRV)
	{
		const FComputeShaderRHIParamRef ShaderRHI = GetComputeShader();
		FGlobalShader::SetParameters(RHICmdList, ShaderRHI, View);

		AHRUpsampleConstantBuffer cbparams;
		cbparams.ScreenX = View.Family->FamilySizeX;
		cbparams.ScreenY = View.Family->FamilySizeY;

		if ( !cb.IsInitialized() )
		{
			cb.InitResource();
		}
		cb.SetContents( cbparams );
		
		SetUniformBufferParameter(RHICmdList, ShaderRHI, GetUniformBufferParameter<AHRUpsampleConstantBuffer>(), cb );

		if(GIBufferTexture.IsBound())
			RHICmdList.SetShaderResourceViewParameter(ShaderRHI,GIBufferTexture.GetBaseIndex(),giSRV);
		if(LinearSampler.IsBound())
			RHICmdList.SetShaderSampler(ShaderRHI,LinearSampler.GetBaseIndex(),TStaticSamplerState<SF_Trilinear,AM_Wrap,AM_Wrap,AM_Wrap>::GetRHI());
	}
	
	void BindOutput(FRHICommandList& RHICmdList,FUnorderedAccessViewRHIParamRef targetUAV)
	{
		const FComputeShaderRHIParamRef ShaderRHI = GetComputeShader();

		if(OutBuff.IsBound())
			RHICmdList.SetUAVParameter(ShaderRHI, OutBuff.GetBaseIndex(), targetUAV);
	}
	void UnBindOutput(FRHICommandList& RHICmdList)
	{ 
		const FComputeShaderRHIParamRef ShaderRHI = GetComputeShader();

		if(OutBuff.IsBound())
			RHICmdList.SetUAVParameter(ShaderRHI, OutBuff.GetBaseIndex(), FUnorderedAccessViewRHIParamRef());
	}

	// FShader interface.
	virtual bool Serialize(FArchive& Ar)
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << GIBufferTexture;
		Ar << LinearSampler;
		Ar << OutBuff;
		return bShaderHasOutdatedParameters;
	}

	FShaderResourceParameter GIBufferTexture;
	FShaderResourceParameter LinearSampler;
	FShaderResourceParameter OutBuff;
	FAHRUpsampleCB cb;
};
IMPLEMENT_SHADER_TYPE(,AHRUpsampleCS,TEXT("AHRUpsample"),TEXT("main"),SF_Compute);

void FApproximateHybridRaytracer::Upsample(FRHICommandListImmediate& RHICmdList,FViewInfo& View)
{
	SCOPED_DRAW_EVENT(RHICmdList,AHRUpsample, DEC_SCENE_ITEMS);
	
	TShaderMapRef<AHRUpsampleCS> ComputeShader(View.ShaderMap);
	RHICmdList.SetComputeShader(ComputeShader->GetComputeShader());
	//SetRenderTarget(RHICmdList, FTextureRHIRef(), FTextureRHIRef());

	// Bind the full resolution target as output
	ComputeShader->BindOutput(RHICmdList,UpsampledTargetUAV);

	// Bind the targets
	ComputeShader->SetCS(RHICmdList,View,RaytracingTargetSRV);

	// Dispatch a compute shader that does a depth aware blur
	DispatchComputeShader(RHICmdList, *ComputeShader, ceil(float(View.Family->FamilySizeX) / 16.0f), ceil(float(View.Family->FamilySizeY) / 16.0f), 1);

	// un-set destination
	ComputeShader->UnBindOutput(RHICmdList);
}

class AHRCompositeVS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(AHRCompositeVS,Global);
public:

	static bool ShouldCache(EShaderPlatform Platform)
	{
		return IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM5);
	}

	AHRCompositeVS()	{}
	AHRCompositeVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
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
IMPLEMENT_SHADER_TYPE(,AHRCompositeVS,TEXT("AHRComposite"),TEXT("VS"),SF_Vertex);

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
IMPLEMENT_SHADER_TYPE(,AHRCompositePS,TEXT("AHRComposite"),TEXT("PS"),SF_Pixel);

void FApproximateHybridRaytracer::Composite(FRHICommandListImmediate& RHICmdList,FViewInfo& View)
{
	SCOPED_DRAW_EVENT(RHICmdList,AHRComposite, DEC_SCENE_ITEMS);

	// Simply render a full screen quad and sample the upsampled buffer. Use additive blending to mix it with the light accumulation buffer
	// Only one view at a time for now (1/11/2014)

	// Set additive blending
	RHICmdList.SetBlendState(TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One>::GetRHI());

	// Set the viewport, raster state and depth stencil
	RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);
	RHICmdList.SetRasterizerState(TStaticRasterizerState<FM_Solid, CM_None>::GetRHI());
	RHICmdList.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());

	// Get the shaders
	TShaderMapRef<AHRCompositeVS> VertexShader(View.ShaderMap);
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