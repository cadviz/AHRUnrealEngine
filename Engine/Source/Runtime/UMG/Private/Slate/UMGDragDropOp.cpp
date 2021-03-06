// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "UMGPrivatePCH.h"

#include "UMGDragDropOp.h"

//////////////////////////////////////////////////////////////////////////
// FUMGDragDropOp

FUMGDragDropOp::FUMGDragDropOp()
{
	StartTime = FSlateApplicationBase::Get().GetCurrentTime();
}

void FUMGDragDropOp::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(DragOperation);
}

void FUMGDragDropOp::OnDrop( bool bDropWasHandled, const FPointerEvent& MouseEvent )
{
	if ( DragOperation )
	{
		if ( bDropWasHandled )
		{
			DragOperation->Drop(MouseEvent);
		}
		else
		{
			if ( SourceUserWidget.IsValid() )
			{
				SourceUserWidget->OnDragCancelled(FDragDropEvent(MouseEvent, AsShared()), DragOperation);
			}

			DragOperation->DragCancelled(MouseEvent);
		}
	}
	
	FDragDropOperation::OnDrop(bDropWasHandled, MouseEvent);
}

void FUMGDragDropOp::OnDragged( const class FDragDropEvent& DragDropEvent )
{
	if ( DragOperation )
	{
		DragOperation->Dragged(DragDropEvent);
	}

	FVector2D CachedDesiredSize = DecoratorWidget->GetDesiredSize();

	FVector2D Position = DragDropEvent.GetScreenSpacePosition();
	Position += CachedDesiredSize * DragOperation->Offset;

	switch ( DragOperation->Pivot )
	{
	case EDragPivot::MouseDown:
		Position += MouseDownOffset;
		break;

	case EDragPivot::TopLeft:
		// Position is already Top Left.
		break;
	case EDragPivot::TopCenter:
		Position -= CachedDesiredSize * FVector2D(0.5f, 0);
		break;
	case EDragPivot::TopRight:
		Position -= CachedDesiredSize * FVector2D(1, 0);
		break;

	case EDragPivot::CenterLeft:
		Position -= CachedDesiredSize * FVector2D(0, 0.5f);
		break;
	case EDragPivot::CenterCenter:
		Position -= CachedDesiredSize * FVector2D(0.5f, 0.5f);
		break;
	case EDragPivot::CenterRight:
		Position -= CachedDesiredSize * FVector2D(1.0f, 0.5f);
		break;

	case EDragPivot::BottomLeft:
		Position -= CachedDesiredSize * FVector2D(0, 1);
		break;
	case EDragPivot::BottomCenter:
		Position -= CachedDesiredSize * FVector2D(0.5f, 1);
		break;
	case EDragPivot::BottomRight:
		Position -= CachedDesiredSize * FVector2D(1, 1);
		break;
	}

	const double AnimationTime = 0.150;

	double DeltaTime = FSlateApplicationBase::Get().GetCurrentTime() - StartTime;

	if ( DeltaTime < AnimationTime )
	{
		float T = DeltaTime / AnimationTime;
		FVector2D LerpPosition = ( Position - StartingScreenPos ) * T;
		CursorDecoratorWindow->MoveWindowTo(StartingScreenPos + LerpPosition);
	}
	else
	{
		CursorDecoratorWindow->MoveWindowTo(Position);
	}
}

TSharedPtr<SWidget> FUMGDragDropOp::GetDefaultDecorator() const
{
	return DecoratorWidget;
}

TSharedRef<FUMGDragDropOp> FUMGDragDropOp::New(UDragDropOperation* InOperation, const FVector2D &CursorPosition, const FVector2D &ScreenPositionOfDragee, TSharedPtr<SObjectWidget> SourceUserWidget)
{
	check(InOperation);

	TSharedRef<FUMGDragDropOp> Operation = MakeShareable(new FUMGDragDropOp());
	Operation->MouseDownOffset = ScreenPositionOfDragee - CursorPosition;
	Operation->StartingScreenPos = ScreenPositionOfDragee;
	Operation->SourceUserWidget = SourceUserWidget;

	Operation->DragOperation = InOperation;

	if ( InOperation->DefaultDragVisual == nullptr )
	{
		Operation->DecoratorWidget = SNew(STextBlock)
			.Text(FText::FromString(InOperation->Tag));
	}
	else
	{
		Operation->DecoratorWidget = InOperation->DefaultDragVisual->TakeWidget();
	}

	Operation->DecoratorWidget->SlatePrepass();

	Operation->Construct();

	return Operation;
}
