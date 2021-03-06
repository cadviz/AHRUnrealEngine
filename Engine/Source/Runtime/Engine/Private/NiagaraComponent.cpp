// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "EnginePrivate.h"
#include "Components/NiagaraComponent.h"
#include "Engine/NiagaraScript.h"
#include "VectorVM.h"
#include "ParticleHelper.h"
#include "Particles/ParticleResources.h"
#include "Engine/NiagaraEffectRenderer.h"
#include "Engine/NiagaraEffect.h"
#include "Engine/NiagaraSimulation.h"
#include "MeshBatch.h"
#include "SceneUtils.h"
#include "ComponentReregisterContext.h"

DECLARE_CYCLE_STAT(TEXT("Gen Verts"),STAT_NiagaraGenerateVertices,STATGROUP_Niagara);
DECLARE_DWORD_COUNTER_STAT(TEXT("NumParticles"),STAT_NiagaraNumParticles,STATGROUP_Niagara);



FNiagaraSceneProxy::FNiagaraSceneProxy(const UNiagaraComponent* InComponent)
		:	FPrimitiveSceneProxy(InComponent)
{
	UpdateEffectRenderers(InComponent->GetEffectInstance());
}

void FNiagaraSceneProxy::UpdateEffectRenderers(FNiagaraEffectInstance *InEffect)
{
	EffectRenderers.Empty();
	if (InEffect)
	{
		for (TSharedPtr<FNiagaraSimulation>Emitter : InEffect->GetEmitters())
		{
			AddEffectRenderer(Emitter->GetEffectRenderer());
		}
	}
}

FNiagaraSceneProxy::~FNiagaraSceneProxy()
{
	ReleaseRenderThreadResources();
}

/** Called on render thread to assign new dynamic data */
void FNiagaraSceneProxy::SetDynamicData_RenderThread(FNiagaraDynamicDataBase* NewDynamicData)
{
	for (NiagaraEffectRenderer *Renderer : EffectRenderers)
	{
		if (Renderer)
		{
			Renderer->SetDynamicData_RenderThread(NewDynamicData);
		}
	}
	return;
}


void FNiagaraSceneProxy::ReleaseRenderThreadResources()
{
	for (NiagaraEffectRenderer *Renderer : EffectRenderers)
	{
		if (Renderer)
		{
			Renderer->ReleaseRenderThreadResources();
		}
	}
	return;
}

// FPrimitiveSceneProxy interface.
void FNiagaraSceneProxy::CreateRenderThreadResources()
{
	for (NiagaraEffectRenderer *Renderer : EffectRenderers)
	{
		if (Renderer)
		{
			Renderer->CreateRenderThreadResources();
		}
	}
	return;
}

void FNiagaraSceneProxy::OnActorPositionChanged()
{
	//WorldSpacePrimitiveUniformBuffer.ReleaseResource();
}

void FNiagaraSceneProxy::OnTransformChanged()
{
	//WorldSpacePrimitiveUniformBuffer.ReleaseResource();
}

FPrimitiveViewRelevance FNiagaraSceneProxy::GetViewRelevance(const FSceneView* View)
{
	FPrimitiveViewRelevance Relevance;
	Relevance.bDynamicRelevance = true;

	for (NiagaraEffectRenderer *Renderer : EffectRenderers)
	{
		if (Renderer)
		{
			Relevance |= Renderer->GetViewRelevance(View, this);
		}
	}
	return Relevance;
}


uint32 FNiagaraSceneProxy::GetMemoryFootprint() const
{ 
	return (sizeof(*this) + GetAllocatedSize()); 
}

uint32 FNiagaraSceneProxy::GetAllocatedSize() const
{ 
	uint32 DynamicDataSize = 0;
	for (NiagaraEffectRenderer *Renderer : EffectRenderers)
	{
		if (Renderer)
		{
			DynamicDataSize += Renderer->GetDynamicDataSize();
		}
	}
	return FPrimitiveSceneProxy::GetAllocatedSize() + DynamicDataSize;
}


void FNiagaraSceneProxy::GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const
{
	for (NiagaraEffectRenderer *Renderer : EffectRenderers)
	{
		if (Renderer)
		{
			Renderer->GetDynamicMeshElements(Views, ViewFamily, VisibilityMap, Collector, this);
		}
	}
}




namespace ENiagaraVectorAttr
{
	enum Type
	{
		Position,
		Velocity,
		Color,
		Rotation,
		RelativeTime,
		MaxVectorAttribs
	};
}



//////////////////////////////////////////////////////////////////////////

UNiagaraComponent::UNiagaraComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	bTickInEditor = true;
}


void UNiagaraComponent::TickComponent(float DeltaSeconds, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
//	EmitterAge += DeltaSeconds;

	if (EffectInstance)
	{ 

		//Todo, open this up to the UI and setting via code and BPs.
		static FName Const_Zero(TEXT("ZERO"));
		EffectInstance->SetConstant(Const_Zero, FVector4(0.0f, 0.0f, 0.0f, 0.0f));	// zero constant
		static FName Const_DeltaTime(TEXT("Delta Time"));
		EffectInstance->SetConstant(Const_DeltaTime, FVector4(DeltaSeconds, DeltaSeconds, DeltaSeconds, DeltaSeconds));
		static FName Const_EmitterPos(TEXT("Emitter Position"));
		EffectInstance->SetConstant(Const_EmitterPos, FVector4(ComponentToWorld.GetTranslation()));
		static FName Const_EmitterX(TEXT("Emitter X Axis"));
		EffectInstance->SetConstant(Const_EmitterX, FVector4(ComponentToWorld.GetUnitAxis(EAxis::X)));
		static FName Const_EmitterY(TEXT("Emitter Y Axis"));
		EffectInstance->SetConstant(Const_EmitterY, FVector4(ComponentToWorld.GetUnitAxis(EAxis::Y)));
		static FName Const_EmitterZ(TEXT("Emitter Z Axis"));
		EffectInstance->SetConstant(Const_EmitterZ, FVector4(ComponentToWorld.GetUnitAxis(EAxis::Z)));
		static FName Const_EmitterTransform(TEXT("Emitter Transform"));
		EffectInstance->SetConstant(Const_EmitterTransform, ComponentToWorld.ToMatrixWithScale());
		EffectInstance->Tick(DeltaSeconds);
	}

	UpdateComponentToWorld();
	MarkRenderDynamicDataDirty();
}

void UNiagaraComponent::OnRegister()
{
	Super::OnRegister();
	if (Asset)
	{
		if (!EffectInstance)
		{
			EffectInstance = new FNiagaraEffectInstance(Asset, this);
		}
		{
			EffectInstance->Init(this);

			// initialize all render modules
			ENQUEUE_UNIQUE_RENDER_COMMAND_TWOPARAMETER(
				FChangeNiagaraRenderModule,
				FNiagaraEffectInstance*, InEffect, this->EffectInstance,
				UNiagaraComponent*, InComponent, this,
				{
					for (TSharedPtr<FNiagaraSimulation> Emitter : InEffect->GetEmitters())
					{
						Emitter->SetRenderModuleType(Emitter->GetProperties()->RenderModuleType, InComponent->GetWorld()->FeatureLevel);
					}
				}
			);
		}
		VectorVM::Init();
	}
}


void UNiagaraComponent::OnUnregister()
{
	Super::OnUnregister();
}

void UNiagaraComponent::SendRenderDynamicData_Concurrent()
{
	if (EffectInstance && SceneProxy)
	{
		FNiagaraSceneProxy *NiagaraProxy = static_cast<FNiagaraSceneProxy*>(SceneProxy);
		for (int32 i = 0; i < EffectInstance->GetEmitters().Num(); i++)
		{
			TSharedPtr<FNiagaraSimulation>Emitter = EffectInstance->GetEmitters()[i];
			NiagaraEffectRenderer *Renderer = Emitter->GetEffectRenderer();
			if (Renderer)
			{
				FNiagaraDynamicDataBase* DynamicData = Renderer->GenerateVertexData(Emitter->GetData());

				ENQUEUE_UNIQUE_RENDER_COMMAND_TWOPARAMETER(
					FSendNiagaraDynamicData,
					NiagaraEffectRenderer*, EffectRenderer, Emitter->GetEffectRenderer(),
					FNiagaraDynamicDataBase*, DynamicData, DynamicData,
					{
					EffectRenderer->SetDynamicData_RenderThread(DynamicData);
				});
			}
		}
	}

}

int32 UNiagaraComponent::GetNumMaterials() const
{
	return 0;
}


FBoxSphereBounds UNiagaraComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	FBox SimBounds(ForceInit);

	/*
	if (Effect)
	{
		for (FNiagaraSimulation Sim : Effect->Emitters)
		{
			SimBounds += Sim->GetBounds();
		}
	}
	*/
	{
		SimBounds.Min = FVector(-HALF_WORLD_MAX,-HALF_WORLD_MAX,-HALF_WORLD_MAX);
		SimBounds.Max = FVector(+HALF_WORLD_MAX,+HALF_WORLD_MAX,+HALF_WORLD_MAX);
	}
	return FBoxSphereBounds(SimBounds);
}

FPrimitiveSceneProxy* UNiagaraComponent::CreateSceneProxy()
{
	FNiagaraSceneProxy *Proxy = new FNiagaraSceneProxy(this);
	Proxy->UpdateEffectRenderers(EffectInstance);
	return Proxy;
}

#if WITH_EDITOR
void UNiagaraComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	FComponentReregisterContext ReregisterContext(this);
}
#endif // WITH_EDITOR



void UNiagaraComponent::SetAsset(UNiagaraEffect *InAsset)
{
	Asset = InAsset;

	EffectInstance = new FNiagaraEffectInstance(Asset, this);
}
