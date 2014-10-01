// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#include "UMGPrivatePCH.h"

#define LOCTEXT_NAMESPACE "UMG"

/////////////////////////////////////////////////////
// UWrapBox

UWrapBox::UWrapBox(const FPostConstructInitializeProperties& PCIP)
	: Super(PCIP)
{
	bIsVariable = false;

	SWrapBox::FArguments Defaults;
	Visiblity = UWidget::ConvertRuntimeToSerializedVisiblity( Defaults._Visibility.Get() );
}

void UWrapBox::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);

	MyWrapBox.Reset();
}

UClass* UWrapBox::GetSlotClass() const
{
	return UWrapBoxSlot::StaticClass();
}

void UWrapBox::OnSlotAdded(UPanelSlot* Slot)
{
	// Add the child to the live canvas if it already exists
	if ( MyWrapBox.IsValid() )
	{
		Cast<UWrapBoxSlot>(Slot)->BuildSlot(MyWrapBox.ToSharedRef());
	}
}

void UWrapBox::OnSlotRemoved(UPanelSlot* Slot)
{
	// Remove the widget from the live slot if it exists.
	if ( MyWrapBox.IsValid() )
	{
		TSharedPtr<SWidget> Widget = Slot->Content->GetCachedWidget();
		if ( Widget.IsValid() )
		{
			MyWrapBox->RemoveSlot(Widget.ToSharedRef());
		}
	}
}

UWrapBoxSlot* UWrapBox::AddChildWrapBox(UWidget* Content)
{
	return Cast<UWrapBoxSlot>(Super::AddChild(Content));
}

TSharedRef<SWidget> UWrapBox::RebuildWidget()
{
	MyWrapBox = SNew(SWrapBox)
		.UseAllottedWidth(true);

	for ( UPanelSlot* Slot : Slots )
	{
		if ( UWrapBoxSlot* TypedSlot = Cast<UWrapBoxSlot>(Slot) )
		{
			TypedSlot->Parent = this;
			TypedSlot->BuildSlot(MyWrapBox.ToSharedRef());
		}
	}

	return BuildDesignTimeWidget( MyWrapBox.ToSharedRef() );
}

void UWrapBox::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	MyWrapBox->SetInnerSlotPadding(InnerSlotPadding);
}

#if WITH_EDITOR

const FSlateBrush* UWrapBox::GetEditorIcon()
{
	return FUMGStyle::Get().GetBrush("Widget.WrapBox");
}

const FText UWrapBox::GetPaletteCategory()
{
	return LOCTEXT("Panel", "Panel");
}

#endif

/////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE