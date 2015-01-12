// @RyanTorant
#pragma once
#include "AHR_Voxelization_Shaders.h"

/** A primitive draw interface which adds the drawn elements to the view's batched elements. */
template<typename DrawingPolicyFactoryType>
class TAHRVoxelizerElementPDI : public FPrimitiveDrawInterface
{
public:

	TAHRVoxelizerElementPDI(
		const FViewInfo* InView,
		const typename DrawingPolicyFactoryType::ContextType& InDrawingContext
		) :
		FPrimitiveDrawInterface( InView ),
		View( InView ),
		DrawingContext( InDrawingContext ),
		PrimitiveSceneProxy( NULL )
	{}

	void SetPrimitive( const FPrimitiveSceneProxy* NewPrimitiveSceneProxy );

	// FPrimitiveDrawInterface interface.
	virtual bool IsHitTesting( ) override;
	virtual void SetHitProxy( HHitProxy* HitProxy ) override;
	virtual void RegisterDynamicResource( FDynamicPrimitiveResource* DynamicResource ) override;
	virtual void AddReserveLines(uint8 DepthPriorityGroup, int32 NumLines, bool bDepthBiased = false, bool bThickLines = false) override;
	virtual void DrawSprite(
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
		uint8 BlendMode = SE_BLEND_Masked
		) override;
	virtual void DrawLine(
		const FVector& Start,
		const FVector& End,
		const FLinearColor& Color,
		uint8 DepthPriorityGroup,
		float Thickness = 0.0f,
		float DepthBias = 0.0f,
		bool bScreenSpace = false
		) override;
	virtual void DrawPoint(
		const FVector& Position,
		const FLinearColor& Color,
		float PointSize,
		uint8 DepthPriorityGroup
		) override;
	virtual int32 DrawMesh( const FMeshBatch& Mesh ) override;

	bool IsDirty( ) const
	{
		return bDirty;
	}
	void ClearDirtyFlag( )
	{
		bDirty = false;
	}

private:

	/** The view which is being rendered. */
	const FViewInfo* const View;

	/** The drawing context passed to the drawing policy for the mesh elements rendered by this drawer. */
	typename DrawingPolicyFactoryType::ContextType DrawingContext;

	/** The primitive being rendered. */
	const FPrimitiveSceneProxy* PrimitiveSceneProxy;

	/** The current hit proxy ID being rendered. */
	FHitProxyId HitProxyId;

	/** The batched simple elements. */
	FBatchedElements BatchedElements;

	/** Tracks whether any elements have been rendered by this drawer. */
	uint32 bDirty : 1;
};


/**
* A drawing policy factory for voxelizing
*/
class FAHRVoxelizerDrawingPolicyFactory 
{
public:

	enum { bAllowSimpleElements = false };
	struct ContextType
	{
		ContextType(FRHICommandListImmediate& _RHICmdList)
		{
			RHICmdList = &_RHICmdList;
		}

		FRHICommandListImmediate* RHICmdList;
	};

	static bool DrawDynamicMesh(
		const FViewInfo& View,
		ContextType DrawingContext,
		const FMeshBatch& Mesh,
		bool bBackFace,
		bool bPreFog,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		FHitProxyId HitProxyId
		);

	static bool IsMaterialIgnored( const FMaterialRenderProxy* MaterialRenderProxy ) { return true; }
};

/**
* drawing policy to voxelize a mesh
*/
class FAHRVoxelizerDrawingPolicy : public FMeshDrawingPolicy
{
public:
	/** The data the drawing policy uses for each mesh element. */
	class ElementDataType
	{
	public:
		/** Default constructor. */
		ElementDataType()
		{}
	};

	FAHRVoxelizerDrawingPolicy( const FVertexFactory* InVertexFactory,
							    const FMaterialRenderProxy* InMaterialRenderProxy,
							    const FMaterial& InMaterialResource,
								ERHIFeatureLevel::Type InFeatureLevel,
								FAHRVoxelizerDrawingPolicyFactory::ContextType* _context) : FMeshDrawingPolicy(InVertexFactory,InMaterialRenderProxy,InMaterialResource,bOverrideWithShaderComplexity)
	{
		context = _context;

		// Get the shaders
		VertexShader = InMaterialResource.GetShader<FAHRVoxelizationVertexShader>(InVertexFactory->GetType());
		GeometryShader = InMaterialResource.GetShader<FAHRVoxelizationGeometryShader>(InVertexFactory->GetType());
		PixelShader = InMaterialResource.GetShader<FAHRVoxelizationPixelShader>(InVertexFactory->GetType());
	}

	// FMeshDrawingPolicy interface.
	bool Matches(const FAHRVoxelizerDrawingPolicy& Other) const
	{
		return FMeshDrawingPolicy::Matches(Other) &&
			VertexShader == Other.VertexShader &&
			PixelShader == Other.PixelShader &&
			GeometryShader == Other.GeometryShader;
	}

	void SetSharedState(FRHICommandList& RHICmdList, const FViewInfo* View, const ContextDataType PolicyContext) const
	{
		// Set the shaders parameters
		VertexShader->SetParameters(RHICmdList, MaterialRenderProxy, VertexFactory, *MaterialResource, *View);
		GeometryShader->SetParameters(RHICmdList, MaterialRenderProxy, VertexFactory, *MaterialResource, *View);
		PixelShader->SetParameters(RHICmdList, MaterialRenderProxy,*MaterialResource,View);
	}

	FBoundShaderStateInput GetBoundShaderStateInput(ERHIFeatureLevel::Type InFeatureLevel)
	{
		return FBoundShaderStateInput(
			FMeshDrawingPolicy::GetVertexDeclaration(), 
			VertexShader->GetVertexShader(),
			FHullShaderRHIParamRef(), 
			FDomainShaderRHIParamRef(), 
			PixelShader->GetPixelShader(),
			GeometryShader->GetGeometryShader());
	}

	void SetMeshRenderState(
		FRHICommandList& RHICmdList, 
		const FViewInfo& View,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const FMeshBatch& Mesh,
		int32 BatchElementIndex,
		bool bBackFace,
		const ElementDataType& ElementData,
		const ContextDataType PolicyContext
		) const
	{
		const FMeshBatchElement& BatchElement = Mesh.Elements[BatchElementIndex];

		VertexShader->SetMesh(RHICmdList, VertexFactory,View,PrimitiveSceneProxy,BatchElement);
		GeometryShader->SetMesh(RHICmdList, VertexFactory,View,PrimitiveSceneProxy,BatchElement);
		PixelShader->SetMesh(RHICmdList, VertexFactory,View,PrimitiveSceneProxy,BatchElement);

		context->RHICmdList->SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());
		context->RHICmdList->SetRasterizerState(TStaticRasterizerState<FM_Solid,CM_None,false,false>::GetRHI());
		context->RHICmdList->SetViewport(0,0,0,AHRGetVoxelResolution()*2,AHRGetVoxelResolution()*2,1);
		Mesh.VertexFactory->Set(*context->RHICmdList);
	
		// Bind the voxels UAV and bind a null depth-stencil buffer
		FUnorderedAccessViewRHIParamRef uavs[] = { AHREngine.GetSceneVolumeUAV(),AHREngine.GetEmissiveVolumeUAV() };
		context->RHICmdList->SetRenderTargets(0,nullptr,nullptr,2,uavs);

		//FMeshDrawingPolicy::SetMeshRenderState(RHICmdList, View,PrimitiveSceneProxy,Mesh,BatchElementIndex,bBackFace,FMeshDrawingPolicy::ElementDataType(),PolicyContext);
	}

	friend int32 CompareDrawingPolicy(const FAHRVoxelizerDrawingPolicy& A,const FAHRVoxelizerDrawingPolicy& B)
	{
		COMPAREDRAWINGPOLICYMEMBERS(VertexShader);
		COMPAREDRAWINGPOLICYMEMBERS(PixelShader);
		COMPAREDRAWINGPOLICYMEMBERS(GeometryShader);

		return 0;
	}
private:
	FAHRVoxelizerDrawingPolicyFactory::ContextType* context;
	FAHRVoxelizationVertexShader* VertexShader;
	FAHRVoxelizationGeometryShader* GeometryShader;
	FAHRVoxelizationPixelShader* PixelShader;
};