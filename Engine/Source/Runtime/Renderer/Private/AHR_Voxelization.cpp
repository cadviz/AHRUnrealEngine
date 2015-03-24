// @RyanTorant
#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "ApproximateHybridRaytracing.h"
#include "SceneUtils.h"
#include "AHR_Voxelization.h"
#include "RHI.h"
#include "AHR_Voxelization_Shaders.h"

IMPLEMENT_MATERIAL_SHADER_TYPE(,FAHRVoxelizationVertexShader,TEXT("AHRVoxelizationVS"),TEXT("Main"),SF_Vertex);
IMPLEMENT_MATERIAL_SHADER_TYPE(,FAHRVoxelizationGeometryShader,TEXT("AHRVoxelizationGS"),TEXT("Main"),SF_Geometry);
IMPLEMENT_MATERIAL_SHADER_TYPE(,FAHRVoxelizationPixelShader,TEXT("AHRVoxelizationPS"),TEXT("Main"),SF_Pixel);

IMPLEMENT_UNIFORM_BUFFER_STRUCT(AHRVoxelizationCB,TEXT("AHRVoxelizationCB"));

#if 0
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
	INC_DWORD_STAT_BY( STAT_DynamicPathMeshDrawCalls, Mesh.Elements.Num( ) );
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
#endif

bool FAHRVoxelizerDrawingPolicyFactory::DrawDynamicMesh(
		FRHICommandList& RHICmdList, 
		const FViewInfo& View,
		ContextType DrawingContext,
		const FMeshBatch& Mesh,
		bool bBackFace,
		bool bPreFog,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		FHitProxyId HitProxyId
		)
{
	auto featureLevel = View.GetFeatureLevel();
	const FMaterial* Material = Mesh.MaterialRenderProxy->GetMaterial(featureLevel);

	FAHRVoxelizerDrawingPolicy DrawingPolicy( Mesh.VertexFactory,
											  Mesh.MaterialRenderProxy, *Material,
											  featureLevel,
											  &DrawingContext );

	RHICmdList.BuildAndSetLocalBoundShaderState(DrawingPolicy.GetBoundShaderStateInput(View.GetFeatureLevel()));
	DrawingPolicy.SetSharedState(RHICmdList, &View, FAHRVoxelizerDrawingPolicy::ContextDataType());

	for( int32 BatchElementIndex = 0, Num = Mesh.Elements.Num(); BatchElementIndex < Num; BatchElementIndex++ )
	{
		DrawingPolicy.SetMeshRenderState( RHICmdList, 
										  View,
										  PrimitiveSceneProxy,
										  Mesh,
										  BatchElementIndex,
										  bBackFace,
										  FAHRVoxelizerDrawingPolicy::ElementDataType(),
										  FMeshDrawingPolicy::ContextDataType() );

		DrawingPolicy.DrawMesh(RHICmdList, Mesh, BatchElementIndex);
	}

	// Unbind
	RHICmdList.SetRenderTargets(0,nullptr,FTextureRHIRef(),0,nullptr);
	return true;
}

/*
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
}*/