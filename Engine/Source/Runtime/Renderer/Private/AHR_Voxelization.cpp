// @RyanTorant
#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "ApproximateHybridRaytracing.h"
#include "SceneUtils.h"
#include "AHR_Voxelization.h"
#include "RHI.h"
#include "AHR_Voxelization_Shaders.h"

IMPLEMENT_SHADER_TYPE(,FAHRVoxelizationVertexShader,TEXT("AHRVoxelizationVS"),TEXT("Main"),SF_Vertex);

template<typename DrawingPolicyFactoryType>
void TAHRVoxelizerElementPDI<DrawingPolicyFactoryType>::SetPrimitive( const FPrimitiveSceneProxy* NewPrimitiveSceneProxy )
{
	PrimitiveSceneProxy = NewPrimitiveSceneProxy;
	if( NewPrimitiveSceneProxy )
	{
		HitProxyId = PrimitiveSceneProxy->GetPrimitiveSceneInfo( )->DefaultDynamicHitProxyId;
	}
}

template<typename DrawingPolicyFactoryType>
bool TAHRVoxelizerElementPDI<DrawingPolicyFactoryType>::IsHitTesting( )
{
	return false;
}

template<typename DrawingPolicyFactoryType>
void TAHRVoxelizerElementPDI<DrawingPolicyFactoryType>::SetHitProxy( HHitProxy* HitProxy )
{
}

template<typename DrawingPolicyFactoryType>
void TAHRVoxelizerElementPDI<DrawingPolicyFactoryType>::RegisterDynamicResource( FDynamicPrimitiveResource* DynamicResource )
{
}

template<typename DrawingPolicyFactoryType>
void TAHRVoxelizerElementPDI<DrawingPolicyFactoryType>::DrawSprite(
	const FVector& Position,
	float SizeX,
	float SizeY,
	const FTexture* Sprite,
	const FLinearColor& Color,
	uint8 DepthPriorityGroup,
	float U,
	float UL,
	float V,
	float VL,
	uint8 BlendMode
	)
{
}

template<typename DrawingPolicyFactoryType>
void TAHRVoxelizerElementPDI<DrawingPolicyFactoryType>::AddReserveLines( uint8 DepthPriorityGroup, int32 NumLines, bool bDepthBiased, bool bThickLines )
{
}

template<typename DrawingPolicyFactoryType>
void TAHRVoxelizerElementPDI<DrawingPolicyFactoryType>::DrawLine(
	const FVector& Start,
	const FVector& End,
	const FLinearColor& Color,
	uint8 DepthPriorityGroup,
	float Thickness,
	float DepthBias,
	bool bScreenSpace
	)
{
}

template<typename DrawingPolicyFactoryType>
void TAHRVoxelizerElementPDI<DrawingPolicyFactoryType>::DrawPoint(
	const FVector& Position,
	const FLinearColor& Color,
	float PointSize,
	uint8 DepthPriorityGroup
	)
{
}

template<typename DrawingPolicyFactoryType>
int32 TAHRVoxelizerElementPDI<DrawingPolicyFactoryType>::DrawMesh( const FMeshBatch& Mesh )
{
	int32 NumPassesRendered = 0;

	check( Mesh.GetNumPrimitives( ) > 0 );
	//INC_DWORD_STAT_BY( STAT_DynamicPathMeshDrawCalls, Mesh.Elements.Num( ) );
	const bool DrawDirty = DrawingPolicyFactoryType::DrawDynamicMesh(
		*View,
		DrawingContext,
		Mesh,
		false,
		false,
		PrimitiveSceneProxy,
		HitProxyId
		);
	bDirty |= DrawDirty;

	NumPassesRendered += DrawDirty;
	return NumPassesRendered;
}

bool FAHRVoxelizerDrawingPolicyFactory::DrawDynamicMesh(
	const FSceneView& View,
	ContextType DrawingContext,
	const FMeshBatch& Mesh,
	bool bBackFace,
	bool bPreFog,
	const FPrimitiveSceneProxy* PrimitiveSceneProxy,
	FHitProxyId HitProxyId
	)
{

	FAHRVoxelizerDrawingPolicy DrawingPolicy( false,&DrawingContext );
	DrawingPolicy.DrawShared( &View, Mesh);

	int32 BatchElementIndex = 0;
	uint64 Mask = ( Mesh.Elements.Num( ) == 1 ) ? 1 : ( 1 << Mesh.Elements.Num( ) ) - 1;

	do
	{
		if( Mask & 1 )
		{
			DrawingPolicy.SetMeshRenderState( View, PrimitiveSceneProxy, Mesh, BatchElementIndex, bBackFace );
			DrawingPolicy.DrawMesh( Mesh, BatchElementIndex );
		}

		Mask >>= 1;
		BatchElementIndex++;

	} while( Mask );

	return true;
}

FAHRVoxelizerDrawingPolicy::FAHRVoxelizerDrawingPolicy( bool bIsWireframe,FAHRVoxelizerDrawingPolicyFactory::ContextType* _context )
{
	context = _context;
}

// Set shared states here
void FAHRVoxelizerDrawingPolicy::DrawShared( const FSceneView* View, const FMeshBatch& Mesh) const
{
	context->RHICmdList->BuildAndSetLocalBoundShaderState(CreateBoundShaderState(Mesh));
}

// Obtain your shaders here and create the Bound Shader State
FBoundShaderStateInput FAHRVoxelizerDrawingPolicy::CreateBoundShaderState( const FMeshBatch& Mesh ) const
{
	/*
	TShaderMapRef<FAHRVoxelizationVertexShader> VertexShader( GetGlobalShaderMap( ) );
	TShaderMapRef<FAHRVoxelizationGeometryShader> GeometryShader( GetGlobalShaderMap( ) );
	TShaderMapRef<FAHRVoxelizationPixelShader> PixelShader( GetGlobalShaderMap( ) );

	return FBoundShaderStateInput(
		Mesh.VertexFactory->GetDeclaration(),	// Vertex Declaration
		VertexShader,		// Vertex Shader
		FHullShaderRHIRef( ),					// Hull Shader
		FDomainShaderRHIRef( ),					// Domain Shader
		PixelShader,			// Pixel Shader
		GeometryShader				// Geometry Shader
		);*/
	
	TShaderMapRef<FAHRVoxelizationVertexShader> VertexShader( GetGlobalShaderMap(ERHIFeatureLevel::SM5) );
	return FBoundShaderStateInput(
		Mesh.VertexFactory->GetDeclaration(),	// Vertex Declaration
		VertexShader->GetVertexShader(),		// Vertex Shader
		FHullShaderRHIRef( ),					// Hull Shader
		FDomainShaderRHIRef( ),					// Domain Shader
		FPixelShaderRHIRef(),			// Pixel Shader
		FGeometryShaderRHIRef()				// Geometry Shader
		);
}

// Set specific states here
void FAHRVoxelizerDrawingPolicy::SetMeshRenderState(
	const FSceneView& View,
	const FPrimitiveSceneProxy* PrimitiveSceneProxy,
	const FMeshBatch& Mesh,
	int32 BatchElementIndex,
	bool bBackFace
	) const
{
	/*FHairBatchElementParams* Params = (FHairBatchElementParams*) Mesh.Elements[ BatchElementIndex ].UserData;
	FHairLightUniformPixelShaderParameters LightParams;

	LightParams.LightPos = FVector( 0, 0, 0 );
	LightParams.LightColor = FLinearColor( 0, 0, 0, 0 );
	LightParams.ViewProjLight = FMatrix( );

	float MinDist = 0.0f;
	float MaxDist = 0.0f;

	if( PolicyLightSceneInfo )
	{
		LightParams.LightPos = PolicyLightSceneInfo->Proxy->GetPosition( );
		LightParams.LightColor = PolicyLightSceneInfo->Proxy->GetColor( );

		if( PolicyShadowInfo )
		{
			LightParams.ViewProjLight = PolicyShadowInfo->GetScreenToShadowMatrix( View );
			LightParams.ShadowFadeFraction = PolicyShadowInfo->FadeAlphas[ 0 ];
			LightParams.ShadowSharpen = PolicyShadowInfo->LightSceneInfo->Proxy->GetShadowSharpen( ) * 7.0f + 1.0f;
		}
	}

	TShaderMapRef<FHairKBufferVertexShader> VertexShader( GetGlobalShaderMap( ) );
	TShaderMapRef<FHairKBufferPixelShader> PixelShader( GetGlobalShaderMap( ) );

	PixelShader->SetParameters(
		View,
		PolicyShadowInfo,
		Params->HairUniformBuffer,
		TUniformBufferRef<FHairLightUniformPixelShaderParameters>::CreateUniformBufferImmediate( LightParams, UniformBuffer_SingleUse ),
		Params->HeadPPLLSRV,
		Params->PPLLSRV,
		GSceneRenderTargets.ShadowDepthZ->GetRenderTargetItem().TargetableTexture
		);*/
	TShaderMapRef<FAHRVoxelizationVertexShader> VertexShader( GetGlobalShaderMap(ERHIFeatureLevel::SM5) );

	context->RHICmdList->SetRasterizerState(TStaticRasterizerState<FM_Solid,CM_None,false,false>::GetRHI());
	Mesh.VertexFactory->Set(*context->RHICmdList);

	VertexShader->SetMesh(context->RHICmdList,Mesh.VertexFactory,View,PrimitiveSceneProxy,Mesh.Elements[0]);

	// Bind the voxels UAV and bind a null depth-stencil buffer
	FUnorderedAccessViewRHIParamRef uavs[] = { AHREngine.GetSceneVolumeUAV() };
	context->RHICmdList->SetRenderTargets(0,nullptr,nullptr,1,uavs);
}

// Draw Mesh
void FAHRVoxelizerDrawingPolicy::DrawMesh( const FMeshBatch& Mesh, int32 BatchElementIndex ) const
{
	const FMeshBatchElement& BatchElement = Mesh.Elements[ BatchElementIndex ];

	context->RHICmdList->DrawPrimitive(
		Mesh.Type,
		0,
		2,
		1
		);

	//TShaderMapRef<FHairKBufferPixelShader> PixelShader( GetGlobalShaderMap( ) );
	//PixelShader->UnbindParameters( );
}