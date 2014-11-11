// @RyanTorant
#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "ApproximateHybridRaytracing.h"
#include "SceneUtils.h"
#include "AHR_Voxelization.h"


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

	FAHRVoxelizerDrawingPolicy DrawingPolicy( false );
	DrawingPolicy.DrawShared( &View, DrawingPolicy.CreateBoundShaderState( Mesh, 0 ) );

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

FAHRVoxelizerDrawingPolicy::FHairKBufferDrawingPolicy( bool bIsWireframe )
{
}

// Set shared states here
void FAHRVoxelizerDrawingPolicy::DrawShared( const FSceneView* View, FBoundShaderStateRHIParamRef BoundShaderState ) const
{
	RHISetBoundShaderState( BoundShaderState );
}
