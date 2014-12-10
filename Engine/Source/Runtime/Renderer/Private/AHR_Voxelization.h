// @RyanTorant
#pragma once

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
		const FSceneView& View,
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
* Hair drawing policy to populate K buffer
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
								ERHIFeatureLevel::Type InFeatureLevel
	) : FMeshDrawingPolicy(InVertexFactory,InMaterialRenderProxy,InMaterialResource,bOverrideWithShaderComplexity)
	{

	}

	void DrawShared( const FSceneView* View,const FMeshBatch& Mesh ) const;
	FBoundShaderStateInput CreateBoundShaderState( const FMeshBatch& Mesh ) const;
	void SetMeshRenderState(
		const FSceneView& View,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const FMeshBatch& Mesh,
		int32 BatchElementIndex,
		bool bBackFace
		) const;
	void DrawMesh( const FMeshBatch& Mesh, int32 BatchElementIndex ) const;

	static const FLightSceneInfo* PolicyLightSceneInfo;
	static const FProjectedShadowInfo* PolicyShadowInfo;
private:
	FAHRVoxelizerDrawingPolicyFactory::ContextType* context;
};