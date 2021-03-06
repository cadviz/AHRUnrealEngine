// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#pragma once


/**
 * Widget that visualizes the contents of a FReflectorNode.
 */
class SLATEREFLECTOR_API SReflectorTreeWidgetItem
	: public SMultiColumnTableRow<TSharedPtr<FReflectorNode>>
{
public:

	SLATE_BEGIN_ARGS(SReflectorTreeWidgetItem)
		: _WidgetInfoToVisualize()
		, _SourceCodeAccessor()
		, _AssetAccessor()
	{ }

		SLATE_ARGUMENT(TSharedPtr<FReflectorNode>, WidgetInfoToVisualize)
		SLATE_ARGUMENT(FAccessSourceCode, SourceCodeAccessor)
		SLATE_ARGUMENT(FAccessAsset, AssetAccessor)

	SLATE_END_ARGS()

public:

	/**
	 * Construct child widgets that comprise this widget.
	 *
	 * @param InArgs Declaration from which to construct this widget.
	 */
	void Construct( const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView )
	{
		this->WidgetInfo = InArgs._WidgetInfoToVisualize;
		this->OnAccessSourceCode = InArgs._SourceCodeAccessor;
		this->OnAccessAsset = InArgs._AssetAccessor;

		SMultiColumnTableRow< TSharedPtr<FReflectorNode> >::Construct( SMultiColumnTableRow< TSharedPtr<FReflectorNode> >::FArguments().Padding(1), InOwnerTableView );
	}

public:

	// SMultiColumnTableRow overrides

	virtual TSharedRef<SWidget> GenerateWidgetForColumn( const FName& ColumnName ) override;

protected:

	/** @return String representation of the widget we are visualizing */
	FText GetWidgetType() const
	{
		return WidgetInfo.Get()->Widget.IsValid()
			? FText::FromString(WidgetInfo.Get()->Widget.Pin()->GetTypeAsString())
			: NSLOCTEXT("SWidgetReflector", "NullWidget", "Null Widget");
	}
	
	virtual FString GetReadableLocation() const override;

	FText GetReadableLocationAsText() const
	{
		return FText::FromString(GetReadableLocation());
	}

	FText GetWidgetFile() const
	{
		return WidgetInfo.Get()->Widget.IsValid()
			? FText::FromString(WidgetInfo.Get()->Widget.Pin()->GetCreatedInFile())
			: FText::GetEmpty();
	}

	int32 GetWidgetLineNumber() const
	{
		return WidgetInfo.Get()->Widget.IsValid()
			? WidgetInfo.Get()->Widget.Pin()->GetCreatedInLineNumber()
			: 0;
	}

	FText GetVisibilityAsString() const
	{
		TSharedPtr<SWidget> TheWidget = WidgetInfo.Get()->Widget.Pin();
		return TheWidget.IsValid()
			? FText::FromString(TheWidget->GetVisibility().ToString())
			: FText::GetEmpty();
	}

	/** @return The tint of the reflector node */
	FSlateColor GetTint() const
	{
		return WidgetInfo.Get()->Tint;
	}

	void HandleHyperlinkNavigate();

private:

	/** The info about the widget that we are visualizing. */
	TAttribute< TSharedPtr<FReflectorNode> > WidgetInfo;

	FAccessSourceCode OnAccessSourceCode;

	FAccessAsset OnAccessAsset;
};
