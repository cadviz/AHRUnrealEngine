// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SCompoundWidget.h"
#include "SNodePanel.h"
#include "SDesignSurface.h"
#include "DesignerExtension.h"
#include "IUMGDesigner.h"

#include "SPaintSurface.h"

namespace EDesignerMessage
{
	enum Type
	{
		None,
		MoveFromParent,
	};
}

class FDesignerExtension;
class UPanelWidget;
class UUserWidget;
class SRuler;

/**
 * The designer for widgets.  Allows for laying out widgets in a drag and drop environment.
 */
class SDesignerView : public SDesignSurface, public FGCObject, public IUMGDesigner
{
public:

	SLATE_BEGIN_ARGS( SDesignerView ) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedPtr<class FWidgetBlueprintEditor> InBlueprintEditor);
	virtual ~SDesignerView();

	TSharedRef<SWidget> CreateOverlayUI();

	// SWidget interface
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;

	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	virtual FReply OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual void OnDragLeave(const FDragDropEvent& DragDropEvent) override;
	virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	// End of SWidget interface

	void Register(TSharedRef<FDesignerExtension> Extension);

	// IUMGDesigner interface
	virtual float GetPreviewScale() const override;
	virtual const TSet<FWidgetReference>& GetSelectedWidgets() const override;
	virtual FWidgetReference GetSelectedWidget() const override;
	virtual ETransformMode::Type GetTransformMode() const override;
	virtual FGeometry GetDesignerGeometry() const override;
	virtual bool GetWidgetGeometry(const FWidgetReference& Widget, FGeometry& Geometry) const override;
	virtual bool GetWidgetGeometry(const UWidget* PreviewWidget, FGeometry& Geometry) const override;
	virtual bool GetWidgetParentGeometry(const FWidgetReference& Widget, FGeometry& Geometry) const override;
	virtual FGeometry MakeGeometryWindowLocal(const FGeometry& WidgetGeometry) const override;
	virtual void MarkDesignModifed(bool bRequiresRecompile) override;
	// End of IUMGDesigner interface

	// FGCObject interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	// End of FGCObject interface

private:
	/** Establishes the resolution and aspect ratio to use on construction from config settings */
	void SetStartupResolution();

	/** The width of the preview screen for the UI */
	FOptionalSize GetPreviewWidth() const;

	/** The height of the preview screen for the UI */
	FOptionalSize GetPreviewHeight() const;

	/** Gets the DPI scale that would be applied given the current preview width and height */
	float GetPreviewDPIScale() const;

	virtual FSlateRect ComputeAreaBounds() const override;

	/** Adds any pending selected widgets to the selection set */
	void ResolvePendingSelectedWidgets();

	/** Updates the designer to display the latest preview widget */
	void UpdatePreviewWidget(bool bForceUpdate);

	void ClearExtensionWidgets();
	void CreateExtensionWidgetsForSelection();

	EVisibility GetInfoBarVisibility() const;
	FText GetInfoBarText() const;

	/** Displays the context menu when you right click */
	void ShowContextMenu(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent);

	void OnEditorSelectionChanged();

	/** Gets the blueprint being edited by the designer */
	UWidgetBlueprint* GetBlueprint() const;

	/** Called whenever the blueprint is recompiled */
	void OnBlueprintReinstanced();

	void PopulateWidgetGeometryCache(FArrangedWidget& Root);

	/** @return Formatted text for the given resolution params */
	FText GetResolutionText(int32 Width, int32 Height, const FString& AspectRatio) const;

	FText GetCurrentResolutionText() const;
	FText GetCurrentDPIScaleText() const;
	FSlateColor GetResolutionTextColorAndOpacity() const;
	EVisibility GetResolutionTextVisibility() const;

	TOptional<int32> GetCustomResolutionWidth() const;
	TOptional<int32> GetCustomResolutionHeight() const;
	void OnCustomResolutionWidthChanged(int32 InValue);
	void OnCustomResolutionHeightChanged(int32 InValue);
	EVisibility GetCustomResolutionEntryVisibility() const;
	
	// Handles selecting a common screen resolution.
	void HandleOnCommonResolutionSelected(int32 Width, int32 Height, FString AspectRatio);
	bool HandleIsCommonResolutionSelected(int32 Width, int32 Height) const;
	void AddScreenResolutionSection(FMenuBuilder& MenuBuilder, const TArray<FPlayScreenResolution>& Resolutions, const FText& SectionName);
	bool HandleIsCustomResolutionSelected() const;
	void HandleOnCustomResolutionSelected();
	TSharedRef<SWidget> GetAspectMenu();

	EVisibility PIENotification() const;

	// Handles drawing selection and other effects a SPaintSurface widget injected into the hierarchy.
	int32 HandleEffectsPainting(const FOnPaintHandlerParams& PaintArgs);
	FReply HandleDPISettingsClicked();

	UUserWidget* GetDefaultWidget() const;

	void BeginTransaction(const FText& SessionName);
	bool InTransaction() const;
	void EndTransaction(bool bCancel);

private:
	struct FWidgetHitResult
	{
	public:
		FWidgetReference Widget;
		FArrangedWidget WidgetArranged;

		FName NamedSlot;

	public:
		FWidgetHitResult();
	};

	/** @returns Gets the widget under the cursor based on a mouse pointer event. */
	bool FindWidgetUnderCursor(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, FWidgetHitResult& HitResult);

private:
	FReply HandleZoomToFitClicked();
	EVisibility GetRulerVisibility() const;

private:
	static const FString ConfigSectionName;
	static const uint32 DefaultResolutionWidth;
	static const uint32 DefaultResolutionHeight;
	static const FString DefaultAspectRatio;

	/** Extensions for the designer to allow for custom widgets to be inserted onto the design surface as selection changes. */
	TArray< TSharedRef<FDesignerExtension> > DesignerExtensions;

private:
	void BindCommands();

	void SetTransformMode(ETransformMode::Type InTransformMode);
	bool CanSetTransformMode(ETransformMode::Type InTransformMode) const;
	bool IsTransformModeActive(ETransformMode::Type InTransformMode) const;

	UWidget* ProcessDropAndAddWidget(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent, bool bIsPreview);

	FVector2D GetExtensionPosition(TSharedRef<FDesignerSurfaceElement> ExtensionElement) const;

	FVector2D GetExtensionSize(TSharedRef<FDesignerSurfaceElement> ExtensionElement) const;
	
private:
	/** A reference to the BP Editor that owns this designer */
	TWeakPtr<FWidgetBlueprintEditor> BlueprintEditor;

	/** The designer command list */
	TSharedPtr<FUICommandList> CommandList;

	/** The transaction used to commit undoable actions from resize, move...etc */
	FScopedTransaction* ScopedTransaction;

	/** The current preview widget */
	UUserWidget* PreviewWidget;

	/** The current preview widget's slate widget */
	TWeakPtr<SWidget> PreviewSlateWidget;
	
	UWidget* DropPreviewWidget;
	UPanelWidget* DropPreviewParent;

	TSharedPtr<class SZoomPan> PreviewHitTestRoot;
	TSharedPtr<SDPIScaler> PreviewSurface;
	TSharedPtr<SCanvas> ExtensionWidgetCanvas;
	TSharedPtr<SPaintSurface> EffectsLayer;

	/** The currently selected preview widgets in the preview GUI, just a cache used to determine changes between selection changes. */
	TSet< FWidgetReference > SelectedWidgetsCache;

	/** The location in selected widget local space where the context menu was summoned. */
	FVector2D SelectedWidgetContextMenuLocation;

	/**
	 * Holds onto a temporary widget that the user may be getting ready to select, or may just 
	 * be the widget that got hit on the initial mouse down before moving the parent.
	 */
	FWidgetReference PendingSelectedWidget;

	/** The position in screen space where the user began dragging a widget */
	FVector2D DraggingStartPositionScreenSpace;

	/** An existing widget is being moved in its current container, or in to a new container. */
	bool bMovingExistingWidget;

	/** The configured Width of the preview area, simulates screen size. */
	int32 PreviewWidth;

	/** The configured Height of the preview area, simulates screen size. */
	int32 PreviewHeight;

	// Resolution Info
	FString PreviewAspectRatio;

	/** Curve to handle fading of the resolution */
	FCurveSequence ResolutionTextFade;

	/**  */
	FWeakWidgetPath SelectedWidgetPath;

	/** The ruler bar at the top of the designer. */
	TSharedPtr<SRuler> TopRuler;

	/** The ruler bar on the left side of the designer. */
	TSharedPtr<SRuler> SideRuler;

	/** */
	EDesignerMessage::Type DesignerMessage;

	/** */
	ETransformMode::Type TransformMode;

	/**  */
	TMap<TSharedRef<SWidget>, FArrangedWidget> CachedWidgetGeometry;

	/**  */
	FGeometry CachedDesignerGeometry;
};
