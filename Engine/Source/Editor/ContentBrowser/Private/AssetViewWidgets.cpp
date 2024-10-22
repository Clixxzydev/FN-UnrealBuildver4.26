// Copyright Epic Games, Inc. All Rights Reserved.


#include "AssetViewWidgets.h"
#include "UObject/UnrealType.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Engine/GameViewportClient.h"
#include "SlateOptMacros.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Images/SImage.h"
#include "Materials/Material.h"
#include "ISourceControlProvider.h"
#include "ISourceControlModule.h"
#include "SourceControlHelpers.h"
#include "EditorFramework/AssetImportData.h"
#include "Engine/Texture2D.h"
#include "ARFilter.h"
#include "AssetRegistryModule.h"
#include "CollectionManagerTypes.h"
#include "ICollectionManager.h"
#include "CollectionManagerModule.h"
#include "AssetViewTypes.h"
#include "SThumbnailEditModeTools.h"
#include "AutoReimport/AssetSourceFilenameCache.h"
#include "SAssetTagItem.h"
#include "CollectionViewUtils.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "DragDropHandler.h"
#include "Internationalization/BreakIterator.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Misc/EngineBuildSettings.h"
#include "ContentBrowserLog.h"
#include "ObjectTools.h"
#include "AssetThumbnail.h"
#include "Settings/ContentBrowserSettings.h"
#include "ContentBrowserModule.h"
#include "ContentBrowserUtils.h"
#include "ContentBrowserDataSource.h"

#define LOCTEXT_NAMESPACE "ContentBrowser"


///////////////////////////////
// FAssetViewModeUtils
///////////////////////////////

FReply FAssetViewModeUtils::OnViewModeKeyDown( const TSet< TSharedPtr<FAssetViewItem> >& SelectedItems, const FKeyEvent& InKeyEvent )
{
	// All asset views use Ctrl-C to copy references to assets
	if ( InKeyEvent.IsControlDown() && InKeyEvent.GetCharacter() == 'C' 
		&& !InKeyEvent.IsShiftDown() && !InKeyEvent.IsAltDown()
		)
	{
		TArray<FContentBrowserItem> SelectedFiles;
		for (const TSharedPtr<FAssetViewItem>& SelectedItem : SelectedItems)
		{
			if (SelectedItem->GetItem().IsFile())
			{
				SelectedFiles.Add(SelectedItem->GetItem());
			}
		}

		if (SelectedFiles.Num() > 0)
		{
			ContentBrowserUtils::CopyItemReferencesToClipboard(SelectedFiles);
		}

		return FReply::Handled();
	}

	return FReply::Unhandled();
}


///////////////////////////////
// FAssetViewModeUtils
///////////////////////////////

TSharedRef<SWidget> FAssetViewItemHelper::CreateListItemContents(SAssetListItem* const InListItem, const TSharedRef<SWidget>& InThumbnail, FName& OutItemShadowBorder)
{
	return CreateListTileItemContents(InListItem, InThumbnail, OutItemShadowBorder);
}

TSharedRef<SWidget> FAssetViewItemHelper::CreateTileItemContents(SAssetTileItem* const InTileItem, const TSharedRef<SWidget>& InThumbnail, FName& OutItemShadowBorder)
{
	return CreateListTileItemContents(InTileItem, InThumbnail, OutItemShadowBorder);
}

template <typename T>
TSharedRef<SWidget> FAssetViewItemHelper::CreateListTileItemContents(T* const InTileOrListItem, const TSharedRef<SWidget>& InThumbnail, FName& OutItemShadowBorder)
{
	TSharedRef<SOverlay> ItemContentsOverlay = SNew(SOverlay);

	if (InTileOrListItem->IsFolder())
	{
		OutItemShadowBorder = FName("NoBorder");

		// TODO: Allow items to customize their widget

		const bool bDeveloperFolder = ContentBrowserUtils::IsItemDeveloperContent(InTileOrListItem->AssetItem->GetItem());

		const bool bCollectionFolder = EnumHasAnyFlags(InTileOrListItem->AssetItem->GetItem().GetItemCategory(), EContentBrowserItemFlags::Category_Collection);
		ECollectionShareType::Type CollectionFolderShareType = ECollectionShareType::CST_All;
		if (bCollectionFolder)
		{
			ContentBrowserUtils::IsCollectionPath(InTileOrListItem->AssetItem->GetItem().GetVirtualPath().ToString(), nullptr, &CollectionFolderShareType);
		}

		const FSlateBrush* FolderBaseImage = bDeveloperFolder
			? FEditorStyle::GetBrush("ContentBrowser.ListViewDeveloperFolderIcon.Base") 
			: FEditorStyle::GetBrush("ContentBrowser.ListViewFolderIcon.Base");

		const FSlateBrush* FolderTintImage = bDeveloperFolder
			? FEditorStyle::GetBrush("ContentBrowser.ListViewDeveloperFolderIcon.Mask") 
			: FEditorStyle::GetBrush("ContentBrowser.ListViewFolderIcon.Mask");

		// Folder base
		ItemContentsOverlay->AddSlot()
		[
			SNew(SImage)
			.Image(FolderBaseImage)
			.ColorAndOpacity(InTileOrListItem, &T::GetAssetColor)
		];

		if (bCollectionFolder)
		{
			FLinearColor IconColor = FLinearColor::White;
			switch(CollectionFolderShareType)
			{
			case ECollectionShareType::CST_Local:
				IconColor = FColor(196, 15, 24);
				break;
			case ECollectionShareType::CST_Private:
				IconColor = FColor(192, 196, 0);
				break;
			case ECollectionShareType::CST_Shared:
				IconColor = FColor(0, 136, 0);
				break;
			default:
				break;
			}

			auto GetCollectionIconBoxSize = [InTileOrListItem]() -> FOptionalSize
			{
				return FOptionalSize(InTileOrListItem->GetThumbnailBoxSize().Get() * 0.3f);
			};

			auto GetCollectionIconBrush = [=]() -> const FSlateBrush*
			{
				const TCHAR* IconSizeSuffix = (GetCollectionIconBoxSize().Get() <= 16.0f) ? TEXT(".Small") : TEXT(".Large");
				return FEditorStyle::GetBrush(ECollectionShareType::GetIconStyleName(CollectionFolderShareType, IconSizeSuffix));
			};

			// Collection share type
			ItemContentsOverlay->AddSlot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride_Lambda(GetCollectionIconBoxSize)
				.HeightOverride_Lambda(GetCollectionIconBoxSize)
				[
					SNew(SImage)
					.Image_Lambda(GetCollectionIconBrush)
					.ColorAndOpacity(IconColor)
				]
			];
		}

		// Folder tint
		ItemContentsOverlay->AddSlot()
		[
			SNew(SImage)
			.Image(FolderTintImage)
		];
	}
	else
	{
		OutItemShadowBorder = FName("ContentBrowser.ThumbnailShadow");

		// The actual thumbnail
		ItemContentsOverlay->AddSlot()
		[
			InThumbnail
		];

		// Source control state
		ItemContentsOverlay->AddSlot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Top)
		[
			SNew(SBox)
			.MaxDesiredWidth(InTileOrListItem, &T::GetStateIconImageSize)
			.MaxDesiredHeight(InTileOrListItem, &T::GetStateIconImageSize)
			[
				SNew(SImage)
				.Image(InTileOrListItem, &T::GetSCCStateImage)
			]
		];

		// Extra external state hook
		ItemContentsOverlay->AddSlot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Top)
		[
			SNew(SBox)
			.MaxDesiredWidth(InTileOrListItem, &T::GetExtraStateIconMaxWidth)
			.MaxDesiredHeight(InTileOrListItem, &T::GetStateIconImageSize)
			[
				InTileOrListItem->GenerateExtraStateIconWidget(TAttribute<float>(InTileOrListItem, &T::GetExtraStateIconWidth))
			]
		];

		// Dirty state
		ItemContentsOverlay->AddSlot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Bottom)
		[
			SNew(SBox)
			.MaxDesiredWidth(InTileOrListItem, &T::GetStateIconImageSize)
			.MaxDesiredHeight(InTileOrListItem, &T::GetStateIconImageSize)
			[
				SNew(SImage)
				.Image(InTileOrListItem, &T::GetDirtyImage)
			]
		];

		// Tools for thumbnail edit mode
		ItemContentsOverlay->AddSlot()
		[
			SNew(SThumbnailEditModeTools, InTileOrListItem->AssetThumbnail)
			.SmallView(!InTileOrListItem->CanDisplayPrimitiveTools())
			.Visibility(InTileOrListItem, &T::GetThumbnailEditModeUIVisibility)
		];
	}

	return ItemContentsOverlay;
}


///////////////////////////////
// Asset view item tool tip
///////////////////////////////

class SAssetViewItemToolTip : public SToolTip
{
public:
	SLATE_BEGIN_ARGS(SAssetViewItemToolTip)
		: _AssetViewItem()
	{ }

		SLATE_ARGUMENT(TSharedPtr<SAssetViewItem>, AssetViewItem)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		AssetViewItem = InArgs._AssetViewItem;

		SToolTip::Construct(
			SToolTip::FArguments()
			.TextMargin(1.0f)
			.BorderImage(FEditorStyle::GetBrush("ContentBrowser.TileViewTooltip.ToolTipBorder"))
			);
	}

	// IToolTip interface
	virtual bool IsEmpty() const override
	{
		return !AssetViewItem.IsValid();
	}

	virtual void OnOpening() override
	{
		TSharedPtr<SAssetViewItem> AssetViewItemPin = AssetViewItem.Pin();
		if (AssetViewItemPin.IsValid())
		{
			SetContentWidget(AssetViewItemPin->CreateToolTipWidget());
		}
	}

	virtual void OnClosed() override
	{
		SetContentWidget(SNullWidget::NullWidget);
	}

private:
	TWeakPtr<SAssetViewItem> AssetViewItem;
};


///////////////////////////////
// Asset view modes
///////////////////////////////

FReply SAssetTileView::OnKeyDown( const FGeometry& InGeometry, const FKeyEvent& InKeyEvent )
{
	FReply Reply = FAssetViewModeUtils::OnViewModeKeyDown(SelectedItems, InKeyEvent);

	if ( Reply.IsEventHandled() )
	{
		return Reply;
	}
	else
	{
		return STileView<TSharedPtr<FAssetViewItem>>::OnKeyDown(InGeometry, InKeyEvent);
	}
}

void SAssetTileView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	// Regreshing an asset view is an intensive task. Do not do this while a user
	// is dragging arround content for maximum responsiveness.
	// Also prevents a re-entrancy crash caused by potentially complex thumbnail generators.
	if (!FSlateApplication::Get().IsDragDropping())
	{
		STileView<TSharedPtr<FAssetViewItem>>::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	}
}

FReply SAssetListView::OnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	FReply Reply = FAssetViewModeUtils::OnViewModeKeyDown(SelectedItems, InKeyEvent);

	if ( Reply.IsEventHandled() )
	{
		return Reply;
	}
	else
	{
		return SListView<TSharedPtr<FAssetViewItem>>::OnKeyDown(InGeometry, InKeyEvent);
	}
}

void SAssetListView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	// Regreshing an asset view is an intensive task. Do not do this while a user
	// is dragging arround content for maximum responsiveness.
	// Also prevents a re-entrancy crash caused by potentially complex thumbnail generators.
	if (!FSlateApplication::Get().IsDragDropping())
	{
		SListView<TSharedPtr<FAssetViewItem>>::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	}
}

FReply SAssetColumnView::OnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	FReply Reply = FAssetViewModeUtils::OnViewModeKeyDown(SelectedItems, InKeyEvent);

	if ( Reply.IsEventHandled() )
	{
		return Reply;
	}
	else
	{
		return SListView<TSharedPtr<FAssetViewItem>>::OnKeyDown(InGeometry, InKeyEvent);
	}
}


void SAssetColumnView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	// Regreshing an asset view is an intensive task. Do not do this while a user
	// is dragging arround content for maximum responsiveness.
	// Also prevents a re-entrancy crash caused by potentially complex thumbnail generators.
	if (!FSlateApplication::Get().IsDragDropping())
	{
		return SListView<TSharedPtr<FAssetViewItem>>::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	}
}

///////////////////////////////
// SAssetViewItem
///////////////////////////////

SAssetViewItem::~SAssetViewItem()
{
	if (AssetItem.IsValid())
	{
		AssetItem->OnItemDataChanged().RemoveAll(this);
	}

	OnItemDestroyed.ExecuteIfBound(AssetItem);
}

void SAssetViewItem::Construct( const FArguments& InArgs )
{
	AssetItem = InArgs._AssetItem;
	OnRenameBegin = InArgs._OnRenameBegin;
	OnRenameCommit = InArgs._OnRenameCommit;
	OnVerifyRenameCommit = InArgs._OnVerifyRenameCommit;
	OnItemDestroyed = InArgs._OnItemDestroyed;
	ShouldAllowToolTip = InArgs._ShouldAllowToolTip;
	ThumbnailEditMode = InArgs._ThumbnailEditMode;
	HighlightText = InArgs._HighlightText;
	OnIsAssetValidForCustomToolTip = InArgs._OnIsAssetValidForCustomToolTip;
	OnGetCustomAssetToolTip = InArgs._OnGetCustomAssetToolTip;
	OnVisualizeAssetToolTip = InArgs._OnVisualizeAssetToolTip;
	OnAssetToolTipClosing = InArgs._OnAssetToolTipClosing;

	bDraggedOver = false;

	bItemDirty = false;
	OnAssetDataChanged();

	AssetItem->OnItemDataChanged().AddSP(this, &SAssetViewItem::OnAssetDataChanged);

	AssetDirtyBrush = FEditorStyle::GetBrush("ContentBrowser.ContentDirty");
	SCCStateBrush = nullptr;

	// Set our tooltip - this will refresh each time it's opened to make sure it's up-to-date
	SetToolTip(SNew(SAssetViewItemToolTip).AssetViewItem(SharedThis(this)));

	SourceControlStateDelay = 0.0f;
	bSourceControlStateRequested = false;

	ISourceControlModule::Get().RegisterProviderChanged(FSourceControlProviderChanged::FDelegate::CreateSP(this, &SAssetViewItem::HandleSourceControlProviderChanged));
	SourceControlStateChangedDelegateHandle = ISourceControlModule::Get().GetProvider().RegisterSourceControlStateChanged_Handle(FSourceControlStateChanged::FDelegate::CreateSP(this, &SAssetViewItem::HandleSourceControlStateChanged));

	// Source control state may have already been cached, make sure the control is in sync with 
	// cached state as the delegate is not going to be invoked again until source control state 
	// changes. This will be necessary any time the widget is destroyed and recreated after source 
	// control state has been cached; for instance when the widget is killed via FWidgetGenerator::OnEndGenerationPass 
	// or a view is refreshed due to user filtering/navigating):
	HandleSourceControlStateChanged();
}

void SAssetViewItem::Tick( const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime )
{
	const float PrevSizeX = LastGeometry.Size.X;

	LastGeometry = AllottedGeometry;

	// Set cached wrap text width based on new "LastGeometry" value. 
	// We set this only when changed because binding a delegate to text wrapping attributes is expensive
	if( PrevSizeX != AllottedGeometry.Size.X && InlineRenameWidget.IsValid() )
	{
		InlineRenameWidget->SetWrapTextAt( GetNameTextWrapWidth() );
	}

	UpdateDirtyState();

	UpdateSourceControlState((float)InDeltaTime);
}

TSharedPtr<IToolTip> SAssetViewItem::GetToolTip()
{
	return ShouldAllowToolTip.Get() ? SCompoundWidget::GetToolTip() : NULL;
}

void SAssetViewItem::OnDragEnter( const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent )
{
	bDraggedOver = AssetItem && DragDropHandler::HandleDragEnterItem(AssetItem->GetItem(), DragDropEvent);
}
	
void SAssetViewItem::OnDragLeave( const FDragDropEvent& DragDropEvent )
{
	if (AssetItem)
	{
		DragDropHandler::HandleDragLeaveItem(AssetItem->GetItem(), DragDropEvent);
	}
	bDraggedOver = false;
}

FReply SAssetViewItem::OnDragOver( const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent )
{
	bDraggedOver = AssetItem && DragDropHandler::HandleDragOverItem(AssetItem->GetItem(), DragDropEvent);
	return (bDraggedOver) ? FReply::Handled() : FReply::Unhandled();
}

FReply SAssetViewItem::OnDrop( const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent )
{
	if (AssetItem && DragDropHandler::HandleDragDropOnItem(AssetItem->GetItem(), DragDropEvent, AsShared()))
	{
		bDraggedOver = false;
		return FReply::Handled();
	}

	if (bDraggedOver)
	{
		// We were able to handle this operation, but could not due to another error - still report this drop as handled so it doesn't fall through to other widgets
		bDraggedOver = false;
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

bool SAssetViewItem::IsNameReadOnly() const
{
	if (ThumbnailEditMode.Get())
	{
		// Read-only while editing thumbnails
		return true;
	}

	if (!AssetItem.IsValid())
	{
		// Read-only if no valid asset item
		return true;
	}

	if (AssetItem->GetItem().IsTemporary())
	{
		// Temporary items can always be renamed (required for creation/duplication, etc)
		return false;
	}

	// Read-only if we can't be renamed
	return !AssetItem->GetItem().CanRename(nullptr);
}

void SAssetViewItem::HandleBeginNameChange( const FText& OriginalText )
{
	OnRenameBegin.ExecuteIfBound(AssetItem, OriginalText.ToString(), LastGeometry.GetLayoutBoundingRect());
}

void SAssetViewItem::HandleNameCommitted( const FText& NewText, ETextCommit::Type CommitInfo )
{
	OnRenameCommit.ExecuteIfBound(AssetItem, NewText.ToString(), LastGeometry.GetLayoutBoundingRect(), CommitInfo);
}

bool  SAssetViewItem::HandleVerifyNameChanged( const FText& NewText, FText& OutErrorMessage )
{
	return !OnVerifyRenameCommit.IsBound() || OnVerifyRenameCommit.Execute(AssetItem, NewText, LastGeometry.GetLayoutBoundingRect(), OutErrorMessage);
}

void SAssetViewItem::OnAssetDataChanged()
{
	UpdateDirtyState();

	if ( InlineRenameWidget.IsValid() )
	{
		InlineRenameWidget->SetText( GetNameText() );
	}

	CacheDisplayTags();
}

void SAssetViewItem::DirtyStateChanged()
{
}

FText SAssetViewItem::GetAssetClassText() const
{
	if (!AssetItem)
	{
		return FText();
	}
	
	if (AssetItem->IsFolder())
	{
		return LOCTEXT("FolderName", "Folder");
	}

	FContentBrowserItemDataAttributeValue DisplayNameAttributeValue = AssetItem->GetItem().GetItemAttribute(ContentBrowserItemAttributes::ItemTypeDisplayName);
	if (!DisplayNameAttributeValue.IsValid())
	{
		DisplayNameAttributeValue = AssetItem->GetItem().GetItemAttribute(ContentBrowserItemAttributes::ItemTypeName);
	}
	return DisplayNameAttributeValue.IsValid() ? DisplayNameAttributeValue.GetValue<FText>() : FText();
}

const FSlateBrush* SAssetViewItem::GetSCCStateImage() const
{
	return ThumbnailEditMode.Get() ? FEditorStyle::GetNoBrush() : SCCStateBrush;
}

void SAssetViewItem::HandleSourceControlProviderChanged(ISourceControlProvider& OldProvider, ISourceControlProvider& NewProvider)
{
	OldProvider.UnregisterSourceControlStateChanged_Handle(SourceControlStateChangedDelegateHandle);
	SourceControlStateChangedDelegateHandle = NewProvider.RegisterSourceControlStateChanged_Handle(FSourceControlStateChanged::FDelegate::CreateSP(this, &SAssetViewItem::HandleSourceControlStateChanged));
	
	// Reset this so the state will be queried from the new provider on the next Tick
	SourceControlStateDelay = 0.0f;
	bSourceControlStateRequested = false;
	SCCStateBrush = nullptr;
	
	HandleSourceControlStateChanged();
}

void SAssetViewItem::HandleSourceControlStateChanged()
{
	if (AssetItem && AssetItem->IsFile() && !AssetItem->IsTemporary() && ISourceControlModule::Get().IsEnabled())
	{
		FString AssetFilename;
		if (AssetItem->GetItem().GetItemPhysicalPath(AssetFilename))
		{
			FSourceControlStatePtr SourceControlState = ISourceControlModule::Get().GetProvider().GetState(AssetFilename, EStateCacheUsage::Use);
			if (SourceControlState)
			{
				SCCStateBrush = FEditorStyle::GetBrush(SourceControlState->GetIconName());
			}
		}
	}
}

const FSlateBrush* SAssetViewItem::GetDirtyImage() const
{
	return IsDirty() ? AssetDirtyBrush : NULL;
}

TSharedRef<SWidget> SAssetViewItem::GenerateExtraStateIconWidget(TAttribute<float> InMaxExtraStateIconWidth) const
{
	const TArray<FAssetViewExtraStateGenerator>& Generators = FModuleManager::GetModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser")).GetAllAssetViewExtraStateGenerators();
	if (AssetItem && AssetItem->IsFile() && Generators.Num() > 0)
	{
		FAssetData ItemAssetData;
		if (AssetItem->GetItem().Legacy_TryGetAssetData(ItemAssetData))
		{
			// Add extra state icons
			TSharedPtr<SHorizontalBox> Content = SNew(SHorizontalBox);

			for (const FAssetViewExtraStateGenerator& Generator : Generators)
			{
				if (Generator.IconGenerator.IsBound())
				{
					Content->AddSlot()
						.HAlign(HAlign_Left)
						.MaxWidth(InMaxExtraStateIconWidth)
						[
							Generator.IconGenerator.Execute(ItemAssetData)
						];
				}
			}
			return Content.ToSharedRef();
		}
	}
	return SNullWidget::NullWidget;
}

TSharedRef<SWidget> SAssetViewItem::GenerateExtraStateTooltipWidget() const
{
	const TArray<FAssetViewExtraStateGenerator>& Generators = FModuleManager::GetModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser")).GetAllAssetViewExtraStateGenerators();
	if (AssetItem && AssetItem->IsFile() && Generators.Num() > 0)
	{
		FAssetData ItemAssetData;
		if (AssetItem->GetItem().Legacy_TryGetAssetData(ItemAssetData))
		{
			TSharedPtr<SVerticalBox> Content = SNew(SVerticalBox);
			for (const auto& Generator : Generators)
			{
				if (Generator.ToolTipGenerator.IsBound() && Generator.IconGenerator.IsBound())
				{
					Content->AddSlot()
						.Padding(FMargin(0, 2.0F))
						.AutoHeight()
						[
							SNew(SHorizontalBox)

							+SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(FMargin(0, 0, 2.0f, 0))
							[
								Generator.IconGenerator.Execute(ItemAssetData)
							]

							+SHorizontalBox::Slot()
							.VAlign(VAlign_Center)
							[
								Generator.ToolTipGenerator.Execute(ItemAssetData)
							]
						];
				}
			}
			return Content.ToSharedRef();
		}
	}
	return SNullWidget::NullWidget;
}

EVisibility SAssetViewItem::GetThumbnailEditModeUIVisibility() const
{
	return !IsFolder() && ThumbnailEditMode.Get() ? EVisibility::Visible : EVisibility::Collapsed;
}

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
TSharedRef<SWidget> SAssetViewItem::CreateToolTipWidget() const
{
	if ( AssetItem.IsValid() )
	{
		// Legacy custom asset tooltips
		if (OnGetCustomAssetToolTip.IsBound())
		{
			FAssetData ItemAssetData;
			if (AssetItem->GetItem().Legacy_TryGetAssetData(ItemAssetData))
			{
				const bool bTryCustomAssetToolTip = !OnIsAssetValidForCustomToolTip.IsBound() || OnIsAssetValidForCustomToolTip.Execute(ItemAssetData);
				if (bTryCustomAssetToolTip)
				{
					return OnGetCustomAssetToolTip.Execute(ItemAssetData);
				}
			}
		}

		// TODO: Remove this special caseness so that folders can also have visible attributes
		if(AssetItem->IsFile())
		{
			// The tooltip contains the name, class, path, and asset registry tags
			const FText NameText = GetNameText();
			const FText ClassText = FText::Format(LOCTEXT("ClassName", "({0})"), GetAssetClassText());

			// Create a box to hold every line of info in the body of the tooltip
			TSharedRef<SVerticalBox> InfoBox = SNew(SVerticalBox);

			FAssetData ItemAssetData;
			AssetItem->GetItem().Legacy_TryGetAssetData(ItemAssetData);

			// TODO: Always use the virtual path?
			if (ItemAssetData.IsValid())
			{
				AddToToolTipInfoBox(InfoBox, LOCTEXT("TileViewTooltipPath", "Path"), FText::FromName(ItemAssetData.PackagePath), false);
			}
			else
			{
				AddToToolTipInfoBox(InfoBox, LOCTEXT("TileViewTooltipPath", "Path"), FText::FromName(AssetItem->GetItem().GetVirtualPath()), false);
			}

			if (ItemAssetData.IsValid() && ItemAssetData.PackageName != NAME_None)
			{
				int32 PackageNameLengthForCooking = ContentBrowserUtils::GetPackageLengthForCooking(ItemAssetData.PackageName.ToString(), FEngineBuildSettings::IsInternalBuild());

				int32 MaxCookPathLen = ContentBrowserUtils::GetMaxCookPathLen();
				AddToToolTipInfoBox(InfoBox, LOCTEXT("TileViewTooltipPathLengthForCookingKey", "Cooking Filepath Length"), FText::Format(LOCTEXT("TileViewTooltipPathLengthForCookingValue", "{0} / {1}"),
					FText::AsNumber(PackageNameLengthForCooking), FText::AsNumber(MaxCookPathLen)), PackageNameLengthForCooking > MaxCookPathLen ? true : false);
			}

			// Add tags
			for (const auto& DisplayTagItem : CachedDisplayTags)
			{
				AddToToolTipInfoBox(InfoBox, DisplayTagItem.DisplayKey, DisplayTagItem.DisplayValue, DisplayTagItem.bImportant);
			}

			// Add asset source files
			if (ItemAssetData.IsValid())
			{
				TOptional<FAssetImportInfo> ImportInfo = FAssetSourceFilenameCache::ExtractAssetImportInfo(ItemAssetData);
				if (ImportInfo.IsSet())
				{
					for (const auto& File : ImportInfo->SourceFiles)
					{
						FText SourceLabel = LOCTEXT("TileViewTooltipSourceFile", "Source File");
						if (File.DisplayLabelName.Len() > 0)
						{
							SourceLabel = FText::FromString(FText(LOCTEXT("TileViewTooltipSourceFile", "Source File")).ToString() + TEXT(" (") + File.DisplayLabelName + TEXT(")"));
						}
						AddToToolTipInfoBox(InfoBox, SourceLabel, FText::FromString(File.RelativeFilename), false);
					}
				}
			}

			TSharedRef<SVerticalBox> OverallTooltipVBox = SNew(SVerticalBox);

			// Top section (asset name, type, is checked out)
			OverallTooltipVBox->AddSlot()
				.AutoHeight()
				.Padding(0, 0, 0, 4)
				[
					SNew(SBorder)
					.Padding(6)
					.BorderImage(FEditorStyle::GetBrush("ContentBrowser.TileViewTooltip.ContentBorder"))
					[
						SNew(SVerticalBox)

						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SHorizontalBox)

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(0, 0, 4, 0)
							[
								SNew(STextBlock)
								.Text(NameText)
								.Font(FEditorStyle::GetFontStyle("ContentBrowser.TileViewTooltip.NameFont"))
							]

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(ClassText)
								.HighlightText(HighlightText)
							]
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
							.Visibility(this, &SAssetViewItem::GetCheckedOutByOtherTextVisibility)
							.Text(this, &SAssetViewItem::GetCheckedOutByOtherText)
							.ColorAndOpacity(FLinearColor(0.1f, 0.5f, 1.f, 1.f))
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							GenerateExtraStateTooltipWidget()
						]
					]
				];

			// Middle section (user description, if present)
			FText UserDescription = GetAssetUserDescription();
			if (!UserDescription.IsEmpty())
			{
				OverallTooltipVBox->AddSlot()
					.AutoHeight()
					.Padding(0, 0, 0, 4)
					[
						SNew(SBorder)
						.Padding(6)
						.BorderImage(FEditorStyle::GetBrush("ContentBrowser.TileViewTooltip.ContentBorder"))
						[
							SNew(STextBlock)
							.WrapTextAt(300.0f)
							.Font(FEditorStyle::GetFontStyle("ContentBrowser.TileViewTooltip.AssetUserDescriptionFont"))
							.Text(UserDescription)
						]
					];
			}

			// Bottom section (asset registry tags)
			OverallTooltipVBox->AddSlot()
				.AutoHeight()
				[
					SNew(SBorder)
					.Padding(6)
					.BorderImage(FEditorStyle::GetBrush("ContentBrowser.TileViewTooltip.ContentBorder"))
					[
						InfoBox
					]
				];

			// Final section (collection pips)
			if (ItemAssetData.IsValid())
			{
				ICollectionManager& CollectionManager = FCollectionManagerModule::GetModule().Get();

				TArray<FCollectionNameType> CollectionsContainingObject;
				CollectionManager.GetCollectionsContainingObject(ItemAssetData.ObjectPath, CollectionsContainingObject);

				if (CollectionsContainingObject.Num() > 0)
				{
					TSharedRef<SWrapBox> CollectionPipsWrapBox = SNew(SWrapBox)
						.PreferredSize(700.0f);

					for (const FCollectionNameType& CollectionContainingObject : CollectionsContainingObject)
					{
						FCollectionStatusInfo CollectionStatusInfo;
						if (CollectionManager.GetCollectionStatusInfo(CollectionContainingObject.Name, CollectionContainingObject.Type, CollectionStatusInfo))
						{
							CollectionPipsWrapBox->AddSlot()
							.Padding(0, 4, 4, 0)
							[
								// TODO: Honor or remove GetDefault<UContentBrowserSettings>()->bShowFullCollectionNameInToolTip
								SNew(SAssetTagItem)
								.ViewMode(EAssetTagItemViewMode::Compact)
								.BaseColor(CollectionViewUtils::ResolveColor(CollectionContainingObject.Name, CollectionContainingObject.Type))
								.DisplayName(FText::FromName(CollectionContainingObject.Name))
								.CountText(FText::AsNumber(CollectionStatusInfo.NumObjects))
							];
						}
					}

					OverallTooltipVBox->AddSlot()
						.AutoHeight()
						.Padding(0, 4, 0, 0)
						[
							SNew(SBorder)
							.Padding(FMargin(6, 2, 6, 6))
							.BorderImage(FEditorStyle::GetBrush("ContentBrowser.TileViewTooltip.ContentBorder"))
							[
								CollectionPipsWrapBox
							]
						];
				}
			}

			return SNew(SBorder)
				.Padding(6)
				.BorderImage( FEditorStyle::GetBrush("ContentBrowser.TileViewTooltip.NonContentBorder") )
				[
					OverallTooltipVBox
				];
		}
		else
		{
			const FText FolderName = GetNameText();
			const FText FolderPath = FText::FromName(AssetItem->GetItem().GetVirtualPath());

			// Create a box to hold every line of info in the body of the tooltip
			TSharedRef<SVerticalBox> InfoBox = SNew(SVerticalBox);

			AddToToolTipInfoBox( InfoBox, LOCTEXT("TileViewTooltipPath", "Path"), FolderPath, false );

			return SNew(SBorder)
				.Padding(6)
				.BorderImage( FEditorStyle::GetBrush("ContentBrowser.TileViewTooltip.NonContentBorder") )
				[
					SNew(SVerticalBox)

					+SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0, 0, 0, 4)
					[
						SNew(SBorder)
						.Padding(6)
						.BorderImage( FEditorStyle::GetBrush("ContentBrowser.TileViewTooltip.ContentBorder") )
						[
							SNew(SVerticalBox)

							+SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(SHorizontalBox)

								+SHorizontalBox::Slot()
								.AutoWidth()
								.VAlign(VAlign_Center)
								.Padding(0, 0, 4, 0)
								[
									SNew(STextBlock)
									.Text( FolderName )
									.Font( FEditorStyle::GetFontStyle("ContentBrowser.TileViewTooltip.NameFont") )
								]

								+SHorizontalBox::Slot()
								.AutoWidth()
								.VAlign(VAlign_Center)
								[
									SNew(STextBlock) 
									.Text( LOCTEXT("FolderNameBracketed", "(Folder)") )
								]
							]
						]
					]

					+SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SBorder)
						.Padding(6)
						.BorderImage( FEditorStyle::GetBrush("ContentBrowser.TileViewTooltip.ContentBorder") )
						[
							InfoBox
						]
					]
				];
		}
	}
	else
	{
		// Return an empty tooltip since the asset item wasn't valid
		return SNullWidget::NullWidget;
	}
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

EVisibility SAssetViewItem::GetCheckedOutByOtherTextVisibility() const
{
	return GetCheckedOutByOtherText().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
}

FText SAssetViewItem::GetCheckedOutByOtherText() const
{
	if (AssetItem && AssetItem->IsFile() && !AssetItem->IsTemporary() && ISourceControlModule::Get().IsEnabled())
	{
		FString AssetFilename;
		if (AssetItem->GetItem().GetItemPhysicalPath(AssetFilename))
		{
			FSourceControlStatePtr SourceControlState = ISourceControlModule::Get().GetProvider().GetState(AssetFilename, EStateCacheUsage::Use);
			if (SourceControlState && (SourceControlState->IsCheckedOutOther() || SourceControlState->IsCheckedOutOrModifiedInOtherBranch()))
			{
				return SourceControlState->GetDisplayTooltip();
			}
		}
	}

	return FText::GetEmpty();
}

FText SAssetViewItem::GetAssetUserDescription() const
{
	if (AssetItem && AssetItem->IsFile())
	{
		FContentBrowserItemDataAttributeValue DescriptionAttributeValue = AssetItem->GetItem().GetItemAttribute(ContentBrowserItemAttributes::ItemDescription);
		if (DescriptionAttributeValue.IsValid())
		{
			return DescriptionAttributeValue.GetValue<FText>();
		}
	}

	return FText::GetEmpty();
}

void SAssetViewItem::AddToToolTipInfoBox(const TSharedRef<SVerticalBox>& InfoBox, const FText& Key, const FText& Value, bool bImportant) const
{
	FWidgetStyle ImportantStyle;
	ImportantStyle.SetForegroundColor(FLinearColor(1, 0.5, 0, 1));

	InfoBox->AddSlot()
	.AutoHeight()
	.Padding(0, 1)
	[
		SNew(SHorizontalBox)

		+SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0, 0, 4, 0)
		[
			SNew(STextBlock) .Text( FText::Format(LOCTEXT("AssetViewTooltipFormat", "{0}:"), Key ) )
			.ColorAndOpacity(bImportant ? ImportantStyle.GetSubduedForegroundColor() : FSlateColor::UseSubduedForeground())
		]

		+SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(STextBlock) .Text( Value )
			.ColorAndOpacity(bImportant ? ImportantStyle.GetForegroundColor() : FSlateColor::UseForeground())
			.HighlightText((Key.ToString() == TEXT("Path")) ? HighlightText : FText())
			.WrapTextAt(700.0f)
		]
	];
}

void SAssetViewItem::UpdateDirtyState()
{
	bool bNewIsDirty = false;

	// Only update the dirty state for non-temporary items
	if (AssetItem && !AssetItem->IsTemporary())
	{
		bNewIsDirty = AssetItem->GetItem().IsDirty();
	}

	if (bNewIsDirty != bItemDirty)
	{
		bItemDirty = bNewIsDirty;
		DirtyStateChanged();
	}
}

bool SAssetViewItem::IsDirty() const
{
	return bItemDirty;
}

void SAssetViewItem::UpdateSourceControlState(float InDeltaTime)
{
	SourceControlStateDelay += InDeltaTime;

	if (AssetItem && AssetItem->IsFile() && !AssetItem->IsTemporary() && !bSourceControlStateRequested && SourceControlStateDelay > 1.0f && ISourceControlModule::Get().IsEnabled())
	{
		FString AssetFilename;
		if (AssetItem->GetItem().GetItemPhysicalPath(AssetFilename))
		{
			ISourceControlModule::Get().QueueStatusUpdate(AssetFilename);
			bSourceControlStateRequested = true;
		}
	}
}

void SAssetViewItem::CacheDisplayTags()
{
	CachedDisplayTags.Reset();

	const FContentBrowserItemDataAttributeValues AssetItemAttributes = AssetItem->GetItem().GetItemAttributes(/*bIncludeMetaData*/true);
	
	FAssetData ItemAssetData;
	AssetItem->GetItem().Legacy_TryGetAssetData(ItemAssetData);

	// Add all visible attributes
	for (const auto& AssetItemAttributePair : AssetItemAttributes)
	{
		const FName AttributeName = AssetItemAttributePair.Key;
		const FContentBrowserItemDataAttributeValue& AttributeValue = AssetItemAttributePair.Value;
		const FContentBrowserItemDataAttributeMetaData& AttributeMetaData = AttributeValue.GetMetaData();

		if (AttributeMetaData.AttributeType == UObject::FAssetRegistryTag::TT_Hidden)
		{
			continue;
		}
	
		// Build the display value for this attribute
		FText DisplayValue;
		if (AttributeValue.GetValueType() == EContentBrowserItemDataAttributeValueType::Text)
		{
			DisplayValue = AttributeValue.GetValueText();
		}
		else
		{
			const FString AttributeValueStr = AttributeValue.GetValue<FString>();

			auto ReformatNumberStringForDisplay = [](const FString& InNumberString) -> FText
			{
				// Respect the number of decimal places in the source string when converting for display
				int32 NumDecimalPlaces = 0;
				{
					int32 DotIndex = INDEX_NONE;
					if (InNumberString.FindChar(TEXT('.'), DotIndex))
					{
						NumDecimalPlaces = InNumberString.Len() - DotIndex - 1;
					}
				}
	
				if (NumDecimalPlaces > 0)
				{
					// Convert the number as a double
					double Num = 0.0;
					LexFromString(Num, *InNumberString);
	
					const FNumberFormattingOptions NumFormatOpts = FNumberFormattingOptions()
						.SetMinimumFractionalDigits(NumDecimalPlaces)
						.SetMaximumFractionalDigits(NumDecimalPlaces);
	
					return FText::AsNumber(Num, &NumFormatOpts);
				}
				else
				{
					const bool bIsSigned = InNumberString.Len() > 0 && (InNumberString[0] == TEXT('-') || InNumberString[0] == TEXT('+'));
	
					if (bIsSigned)
					{
						// Convert the number as a signed int
						int64 Num = 0;
						LexFromString(Num, *InNumberString);
	
						return FText::AsNumber(Num);
					}
					else
					{
						// Convert the number as an unsigned int
						uint64 Num = 0;
						LexFromString(Num, *InNumberString);
	
						return FText::AsNumber(Num);
					}
				}
	
				return FText::GetEmpty();
			};
	
			bool bHasSetDisplayValue = false;
	
			// Numerical tags need to format the specified number based on the display flags
			if (!bHasSetDisplayValue && AttributeMetaData.AttributeType == UObject::FAssetRegistryTag::TT_Numerical && AttributeValueStr.IsNumeric())
			{
				bHasSetDisplayValue = true;
	
				const bool bAsMemory = !!(AttributeMetaData.DisplayFlags & UObject::FAssetRegistryTag::TD_Memory);
	
				if (bAsMemory)
				{
					// Memory should be a 64-bit unsigned number of bytes
					uint64 NumBytes = 0;
					LexFromString(NumBytes, *AttributeValueStr);
	
					DisplayValue = FText::AsMemory(NumBytes);
				}
				else
				{
					DisplayValue = ReformatNumberStringForDisplay(AttributeValueStr);
				}
			}
	
			// Dimensional tags need to be split into their component numbers, with each component number re-format
			if (!bHasSetDisplayValue && AttributeMetaData.AttributeType == UObject::FAssetRegistryTag::TT_Dimensional)
			{
				TArray<FString> NumberStrTokens;
				AttributeValueStr.ParseIntoArray(NumberStrTokens, TEXT("x"), true);
	
				if (NumberStrTokens.Num() > 0 && NumberStrTokens.Num() <= 3)
				{
					bHasSetDisplayValue = true;
	
					switch (NumberStrTokens.Num())
					{
					case 1:
						DisplayValue = ReformatNumberStringForDisplay(NumberStrTokens[0]);
						break;
	
					case 2:
						DisplayValue = FText::Format(LOCTEXT("DisplayTag2xFmt", "{0} \u00D7 {1}"), ReformatNumberStringForDisplay(NumberStrTokens[0]), ReformatNumberStringForDisplay(NumberStrTokens[1]));
						break;
	
					case 3:
						DisplayValue = FText::Format(LOCTEXT("DisplayTag3xFmt", "{0} \u00D7 {1} \u00D7 {2}"), ReformatNumberStringForDisplay(NumberStrTokens[0]), ReformatNumberStringForDisplay(NumberStrTokens[1]), ReformatNumberStringForDisplay(NumberStrTokens[2]));
						break;
	
					default:
						break;
					}
				}
			}
	
			// Chronological tags need to format the specified timestamp based on the display flags
			if (!bHasSetDisplayValue && AttributeMetaData.AttributeType == UObject::FAssetRegistryTag::TT_Chronological)
			{
				bHasSetDisplayValue = true;
	
				FDateTime Timestamp;
				if (FDateTime::Parse(AttributeValueStr, Timestamp))
				{
					const bool bDisplayDate = !!(AttributeMetaData.DisplayFlags & UObject::FAssetRegistryTag::TD_Date);
					const bool bDisplayTime = !!(AttributeMetaData.DisplayFlags & UObject::FAssetRegistryTag::TD_Time);
					const FString TimeZone = (AttributeMetaData.DisplayFlags & UObject::FAssetRegistryTag::TD_InvariantTz) ? FText::GetInvariantTimeZone() : FString();
	
					if (bDisplayDate && bDisplayTime)
					{
						DisplayValue = FText::AsDateTime(Timestamp, EDateTimeStyle::Short, EDateTimeStyle::Short, TimeZone);
					}
					else if (bDisplayDate)
					{
						DisplayValue = FText::AsDate(Timestamp, EDateTimeStyle::Short, TimeZone);
					}
					else if (bDisplayTime)
					{
						DisplayValue = FText::AsTime(Timestamp, EDateTimeStyle::Short, TimeZone);
					}
				}
			}
	
			// The tag value might be localized text, so we need to parse it for display
			if (!bHasSetDisplayValue && FTextStringHelper::IsComplexText(*AttributeValueStr))
			{
				bHasSetDisplayValue = FTextStringHelper::ReadFromBuffer(*AttributeValueStr, DisplayValue) != nullptr;
			}
	
			// Do our best to build something valid from the string value
			if (!bHasSetDisplayValue)
			{
				bHasSetDisplayValue = true;
	
				// Since all we have at this point is a string, we can't be very smart here.
				// We need to strip some noise off class paths in some cases, but can't load the asset to inspect its UPROPERTYs manually due to performance concerns.
				FString ValueString = FPackageName::ExportTextPathToObjectPath(AttributeValueStr);
	
				const TCHAR StringToRemove[] = TEXT("/Script/");
				if (ValueString.StartsWith(StringToRemove))
				{
					// Remove the class path for native classes, and also remove Engine. for engine classes
					const int32 SizeOfPrefix = UE_ARRAY_COUNT(StringToRemove) - 1;
					ValueString.MidInline(SizeOfPrefix, ValueString.Len() - SizeOfPrefix, false);
					ValueString.ReplaceInline(TEXT("Engine."), TEXT(""));
				}
	
				if (ItemAssetData.IsValid())
				{
					if (const UClass* AssetClass = ItemAssetData.GetClass())
					{
						if (const FProperty* TagField = FindFProperty<FProperty>(AssetClass, AttributeName))
						{
							const FProperty* TagProp = nullptr;
							const UEnum* TagEnum = nullptr;
							if (const FByteProperty* ByteProp = CastField<FByteProperty>(TagField))
							{
								TagProp = ByteProp;
								TagEnum = ByteProp->Enum;
							}
							else if (const FEnumProperty* EnumProp = CastField<FEnumProperty>(TagField))
							{
								TagProp = EnumProp;
								TagEnum = EnumProp->GetEnum();
							}

							// Strip off enum prefixes if they exist
							if (TagProp)
							{
								if (TagEnum)
								{
									const FString EnumPrefix = TagEnum->GenerateEnumPrefix();
									if (EnumPrefix.Len() && ValueString.StartsWith(EnumPrefix))
									{
										ValueString.RightChopInline(EnumPrefix.Len() + 1, false);	// +1 to skip over the underscore
									}
								}

								ValueString = FName::NameToDisplayString(ValueString, false);
							}
						}
					}
				}
	
				DisplayValue = FText::AsCultureInvariant(MoveTemp(ValueString));
			}
			
			// Add suffix to the value, if one is defined for this tag
			if (!AttributeMetaData.Suffix.IsEmpty())
			{
				DisplayValue = FText::Format(LOCTEXT("DisplayTagSuffixFmt", "{0} {1}"), DisplayValue, AttributeMetaData.Suffix);
			}
		}
	
		if (!DisplayValue.IsEmpty())
		{
			CachedDisplayTags.Add(FTagDisplayItem(AttributeName, AttributeMetaData.DisplayName, DisplayValue, AttributeMetaData.bIsImportant));
		}
	}
}

const FSlateBrush* SAssetViewItem::GetBorderImage() const
{
	return bDraggedOver ? FEditorStyle::GetBrush("Menu.Background") : FEditorStyle::GetBrush("NoBorder");
}

bool SAssetViewItem::IsFolder() const
{
	return AssetItem && AssetItem->IsFolder();
}

FText SAssetViewItem::GetNameText() const
{
	return AssetItem
		? AssetItem->GetItem().GetDisplayName()
		: FText();
}

FSlateColor SAssetViewItem::GetAssetColor() const
{
	if (AssetItem)
	{
		FContentBrowserItemDataAttributeValue ColorAttributeValue = AssetItem->GetItem().GetItemAttribute(ContentBrowserItemAttributes::ItemColor);
		if (ColorAttributeValue.IsValid())
		{
			const FString ColorStr = ColorAttributeValue.GetValue<FString>();

			FLinearColor Color;
			if (Color.InitFromString(ColorStr))
			{
				return Color;
			}
		}
		else if (AssetItem->GetItem().IsFolder())
		{
			const bool bCollectionFolder = EnumHasAnyFlags(AssetItem->GetItem().GetItemCategory(), EContentBrowserItemFlags::Category_Collection);
			if (bCollectionFolder)
			{
				FName CollectionName;
				ECollectionShareType::Type CollectionFolderShareType = ECollectionShareType::CST_All;
				ContentBrowserUtils::IsCollectionPath(AssetItem->GetItem().GetVirtualPath().ToString(), &CollectionName, &CollectionFolderShareType);
				
				if (TOptional<FLinearColor> Color = CollectionViewUtils::GetCustomColor(CollectionName, CollectionFolderShareType))
				{
					return Color.GetValue();
				}
			}
			else
			{
				if (TSharedPtr<FLinearColor> Color = ContentBrowserUtils::LoadColor(AssetItem->GetItem().GetVirtualPath().ToString()))
				{
					return *Color;
				}
			}
		}
	}
	return ContentBrowserUtils::GetDefaultColor();
}

bool SAssetViewItem::OnVisualizeTooltip(const TSharedPtr<SWidget>& TooltipContent)
{
	if(OnVisualizeAssetToolTip.IsBound() && TooltipContent.IsValid() && AssetItem && AssetItem->IsFile())
	{
		FAssetData ItemAssetData;
		if (AssetItem->GetItem().Legacy_TryGetAssetData(ItemAssetData))
		{
			return OnVisualizeAssetToolTip.Execute(TooltipContent, ItemAssetData);
		}
	}

	// No custom behavior, return false to allow slate to visualize the widget
	return false;
}

void SAssetViewItem::OnToolTipClosing()
{
	OnAssetToolTipClosing.ExecuteIfBound();
}

///////////////////////////////
// SAssetListItem
///////////////////////////////

SAssetListItem::~SAssetListItem()
{

}

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void SAssetListItem::Construct( const FArguments& InArgs )
{
	SAssetViewItem::Construct( SAssetViewItem::FArguments()
		.AssetItem(InArgs._AssetItem)
		.OnRenameBegin(InArgs._OnRenameBegin)
		.OnRenameCommit(InArgs._OnRenameCommit)
		.OnVerifyRenameCommit(InArgs._OnVerifyRenameCommit)
		.OnItemDestroyed(InArgs._OnItemDestroyed)
		.ShouldAllowToolTip(InArgs._ShouldAllowToolTip)
		.ThumbnailEditMode(InArgs._ThumbnailEditMode)
		.HighlightText(InArgs._HighlightText)
		.OnIsAssetValidForCustomToolTip(InArgs._OnIsAssetValidForCustomToolTip)
		.OnGetCustomAssetToolTip(InArgs._OnGetCustomAssetToolTip)
		.OnVisualizeAssetToolTip(InArgs._OnVisualizeAssetToolTip)
		.OnAssetToolTipClosing( InArgs._OnAssetToolTipClosing )
		);

	AssetThumbnail = InArgs._AssetThumbnail;
	ItemHeight = InArgs._ItemHeight;

	const float ThumbnailPadding = InArgs._ThumbnailPadding;

	TSharedPtr<SWidget> Thumbnail;
	if ( AssetItem.IsValid() && AssetThumbnail.IsValid() )
	{
		FAssetThumbnailConfig ThumbnailConfig;
		ThumbnailConfig.bAllowFadeIn = true;
		ThumbnailConfig.bAllowHintText = InArgs._AllowThumbnailHintLabel;
		ThumbnailConfig.bForceGenericThumbnail = AssetItem->GetItem().GetItemTemporaryReason() == EContentBrowserItemFlags::Temporary_Creation;
		ThumbnailConfig.bAllowAssetSpecificThumbnailOverlay = !ThumbnailConfig.bForceGenericThumbnail;
		ThumbnailConfig.ThumbnailLabel = InArgs._ThumbnailLabel;
		ThumbnailConfig.HighlightedText = InArgs._HighlightText;
		ThumbnailConfig.HintColorAndOpacity = InArgs._ThumbnailHintColorAndOpacity;

		{
			FContentBrowserItemDataAttributeValue ColorAttributeValue = AssetItem->GetItem().GetItemAttribute(ContentBrowserItemAttributes::ItemColor);
			if (ColorAttributeValue.IsValid())
			{
				const FString ColorStr = ColorAttributeValue.GetValue<FString>();

				FLinearColor Color;
				if (Color.InitFromString(ColorStr))
				{
					ThumbnailConfig.AssetTypeColorOverride = Color;
				}
			}
		}

		Thumbnail = AssetThumbnail->MakeThumbnailWidget(ThumbnailConfig);
	}
	else
	{
		Thumbnail = SNew(SImage) .Image( FEditorStyle::GetDefaultBrush() );
	}

	FName ItemShadowBorderName;
	TSharedRef<SWidget> ItemContents = FAssetViewItemHelper::CreateListItemContents(this, Thumbnail.ToSharedRef(), ItemShadowBorderName);

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(this, &SAssetViewItem::GetBorderImage)
		.Padding(0)
		.AddMetaData<FTagMetaData>(FTagMetaData(AssetItem->GetItem().GetVirtualPath()))
		[
			SNew(SHorizontalBox)

			// Viewport
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew( SBox )
				.Padding(ThumbnailPadding - 4.f)
				.WidthOverride( this, &SAssetListItem::GetThumbnailBoxSize )
				.HeightOverride( this, &SAssetListItem::GetThumbnailBoxSize )
				[
					// Drop shadow border
					SNew(SBorder)
					.Padding(4.f)
					.BorderImage(FEditorStyle::GetBrush(ItemShadowBorderName))
					[
						ItemContents
					]
				]
			]

			+SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(6, 0, 0, 0)
			.VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)

				+SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 1)
				[
					SAssignNew(InlineRenameWidget, SInlineEditableTextBlock)
					.Font(FEditorStyle::GetFontStyle("ContentBrowser.AssetTileViewNameFont"))
					.Text( GetNameText() )
					.OnBeginTextEdit(this, &SAssetListItem::HandleBeginNameChange)
					.OnTextCommitted(this, &SAssetListItem::HandleNameCommitted)
					.OnVerifyTextChanged(this, &SAssetListItem::HandleVerifyNameChanged)
					.HighlightText(InArgs._HighlightText)
					.IsSelected(InArgs._IsSelected)
					.IsReadOnly(this, &SAssetListItem::IsNameReadOnly)
				]

				+SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 1)
				[
					// Class
					SAssignNew(ClassText, STextBlock)
					.Font(FEditorStyle::GetFontStyle("ContentBrowser.AssetListViewClassFont"))
					.Text(GetAssetClassText())
					.HighlightText(InArgs._HighlightText)
				]
			]
		]
	];

	if(AssetItem.IsValid())
	{
		AssetItem->OnRenameRequested().BindSP(InlineRenameWidget.Get(), &SInlineEditableTextBlock::EnterEditingMode);
		AssetItem->OnRenameCanceled().BindSP(InlineRenameWidget.Get(), &SInlineEditableTextBlock::ExitEditingMode);
	}
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SAssetListItem::OnAssetDataChanged()
{
	SAssetViewItem::OnAssetDataChanged();

	if (ClassText.IsValid())
	{
		ClassText->SetText(GetAssetClassText());
	}

	if (AssetThumbnail)
	{
		bool bSetThumbnail = false;
		if (AssetItem)
		{
			bSetThumbnail = AssetItem->GetItem().UpdateThumbnail(*AssetThumbnail);
		}
		if (!bSetThumbnail)
		{
			AssetThumbnail->SetAsset(FAssetData());
		}
	}
}

float SAssetListItem::GetExtraStateIconWidth() const
{
	return GetStateIconImageSize().Get();
}

FOptionalSize SAssetListItem::GetExtraStateIconMaxWidth() const
{
	return GetThumbnailBoxSize().Get() * 0.7;
}

FOptionalSize SAssetListItem::GetStateIconImageSize() const
{
	float IconSize = GetThumbnailBoxSize().Get() * 0.3;
	return IconSize > 12 ? IconSize : 12;
}

FOptionalSize SAssetListItem::GetThumbnailBoxSize() const
{
	return FOptionalSize( ItemHeight.Get() );
}

///////////////////////////////
// SAssetTileItem
///////////////////////////////

SAssetTileItem::~SAssetTileItem()
{

}

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void SAssetTileItem::Construct( const FArguments& InArgs )
{
	SAssetViewItem::Construct( SAssetViewItem::FArguments()
		.AssetItem(InArgs._AssetItem)
		.OnRenameBegin(InArgs._OnRenameBegin)
		.OnRenameCommit(InArgs._OnRenameCommit)
		.OnVerifyRenameCommit(InArgs._OnVerifyRenameCommit)
		.OnItemDestroyed(InArgs._OnItemDestroyed)
		.ShouldAllowToolTip(InArgs._ShouldAllowToolTip)
		.ThumbnailEditMode(InArgs._ThumbnailEditMode)
		.HighlightText(InArgs._HighlightText)
		.OnIsAssetValidForCustomToolTip(InArgs._OnIsAssetValidForCustomToolTip)
		.OnGetCustomAssetToolTip(InArgs._OnGetCustomAssetToolTip)
		.OnVisualizeAssetToolTip(InArgs._OnVisualizeAssetToolTip)
		.OnAssetToolTipClosing( InArgs._OnAssetToolTipClosing )
		);

	AssetThumbnail = InArgs._AssetThumbnail;
	ItemWidth = InArgs._ItemWidth;
	ThumbnailPadding = IsFolder() ? InArgs._ThumbnailPadding + 5.0f : InArgs._ThumbnailPadding;

	TSharedPtr<SWidget> Thumbnail;
	if ( AssetItem.IsValid() && AssetThumbnail.IsValid() )
	{
		FAssetThumbnailConfig ThumbnailConfig;
		ThumbnailConfig.bAllowFadeIn = true;
		ThumbnailConfig.bAllowHintText = InArgs._AllowThumbnailHintLabel;
		ThumbnailConfig.bForceGenericThumbnail = AssetItem->GetItem().GetItemTemporaryReason() == EContentBrowserItemFlags::Temporary_Creation;
		ThumbnailConfig.bAllowAssetSpecificThumbnailOverlay = !ThumbnailConfig.bForceGenericThumbnail;
		ThumbnailConfig.ThumbnailLabel = InArgs._ThumbnailLabel;
		ThumbnailConfig.HighlightedText = InArgs._HighlightText;
		ThumbnailConfig.HintColorAndOpacity = InArgs._ThumbnailHintColorAndOpacity;

		{
			FContentBrowserItemDataAttributeValue ColorAttributeValue = AssetItem->GetItem().GetItemAttribute(ContentBrowserItemAttributes::ItemColor);
			if (ColorAttributeValue.IsValid())
			{
				const FString ColorStr = ColorAttributeValue.GetValue<FString>();

				FLinearColor Color;
				if (Color.InitFromString(ColorStr))
				{
					ThumbnailConfig.AssetTypeColorOverride = Color;
				}
			}
		}

		Thumbnail = AssetThumbnail->MakeThumbnailWidget(ThumbnailConfig);
	}
	else
	{
		Thumbnail = SNew(SImage) .Image( FEditorStyle::GetDefaultBrush() );
	}

	FName ItemShadowBorderName;
	TSharedRef<SWidget> ItemContents = FAssetViewItemHelper::CreateTileItemContents(this, Thumbnail.ToSharedRef(), ItemShadowBorderName);

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(this, &SAssetViewItem::GetBorderImage)
		.Padding(0)
		.AddMetaData<FTagMetaData>(FTagMetaData(AssetItem->GetItem().GetVirtualPath()))
		[
			SNew(SVerticalBox)

			// Thumbnail
			+SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			[
				// The remainder of the space is reserved for the name.
				SNew(SBox)
				.Padding(ThumbnailPadding - 4.f)
				.WidthOverride(this, &SAssetTileItem::GetThumbnailBoxSize)
				.HeightOverride( this, &SAssetTileItem::GetThumbnailBoxSize )
				[
					// Drop shadow border
					SNew(SBorder)
					.Padding(4.f)
					.BorderImage(FEditorStyle::GetBrush(ItemShadowBorderName))
					[
						ItemContents
					]
				]
			]

			+SVerticalBox::Slot()
			.Padding(FMargin(1.f, 0))
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.FillHeight(1.f)
			[
				SAssignNew(InlineRenameWidget, SInlineEditableTextBlock)
					.Font( this, &SAssetTileItem::GetThumbnailFont )
					.Text( GetNameText() )
					.OnBeginTextEdit(this, &SAssetTileItem::HandleBeginNameChange)
					.OnTextCommitted(this, &SAssetTileItem::HandleNameCommitted)
					.OnVerifyTextChanged(this, &SAssetTileItem::HandleVerifyNameChanged)
					.HighlightText(InArgs._HighlightText)
					.IsSelected(InArgs._IsSelected)
					.IsReadOnly(this, &SAssetTileItem::IsNameReadOnly)
					.Justification(ETextJustify::Center)
					.LineBreakPolicy(FBreakIterator::CreateCamelCaseBreakIterator())
			]
		]
	
	];

	if(AssetItem.IsValid())
	{
		AssetItem->OnRenameRequested().BindSP(InlineRenameWidget.Get(), &SInlineEditableTextBlock::EnterEditingMode);
		AssetItem->OnRenameCanceled().BindSP(InlineRenameWidget.Get(), &SInlineEditableTextBlock::ExitEditingMode);
	}
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SAssetTileItem::OnAssetDataChanged()
{
	SAssetViewItem::OnAssetDataChanged();

	if (AssetThumbnail)
	{
		bool bSetThumbnail = false;
		if (AssetItem)
		{
			bSetThumbnail = AssetItem->GetItem().UpdateThumbnail(*AssetThumbnail);
		}
		if (!bSetThumbnail)
		{
			AssetThumbnail->SetAsset(FAssetData());
		}
	}
}

float SAssetTileItem::GetExtraStateIconWidth() const
{
	return GetStateIconImageSize().Get();
}

FOptionalSize SAssetTileItem::GetExtraStateIconMaxWidth() const
{
	return GetThumbnailBoxSize().Get() * 0.8;
}

FOptionalSize SAssetTileItem::GetStateIconImageSize() const
{
	float IconSize = GetThumbnailBoxSize().Get() * 0.2;
	return IconSize > 12 ? IconSize : 12;
}

FOptionalSize SAssetTileItem::GetThumbnailBoxSize() const
{
	return FOptionalSize(ItemWidth.Get());
}

FSlateFontInfo SAssetTileItem::GetThumbnailFont() const
{
	FOptionalSize ThumbSize = GetThumbnailBoxSize();
	if ( ThumbSize.IsSet() )
	{
		float Size = ThumbSize.Get();
		if ( Size < 50 )
		{
			const static FName SmallFontName("ContentBrowser.AssetTileViewNameFontVerySmall");
			return FEditorStyle::GetFontStyle(SmallFontName);
		}
		else if ( Size < 85 )
		{
			const static FName SmallFontName("ContentBrowser.AssetTileViewNameFontSmall");
			return FEditorStyle::GetFontStyle(SmallFontName);
		}
	}

	const static FName RegularFont("ContentBrowser.AssetTileViewNameFont");
	return FEditorStyle::GetFontStyle(RegularFont);
}



///////////////////////////////
// SAssetColumnItem
///////////////////////////////

/** Custom box for the Name column of an asset */
class SAssetColumnItemNameBox : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS( SAssetColumnItemNameBox ) {}

		/** The color of the asset  */
		SLATE_ATTRIBUTE( FMargin, Padding )

		/** The widget content presented in the box */
		SLATE_DEFAULT_SLOT(FArguments, Content)

	SLATE_END_ARGS()

	~SAssetColumnItemNameBox() {}

	void Construct( const FArguments& InArgs, const TSharedRef<SAssetColumnItem>& InOwnerAssetColumnItem )
	{
		OwnerAssetColumnItem = InOwnerAssetColumnItem;

		ChildSlot
		[
			SNew(SBox)
			.Padding(InArgs._Padding)
			[
				InArgs._Content.Widget
			]
		];
	}

	virtual TSharedPtr<IToolTip> GetToolTip() override
	{
		if ( OwnerAssetColumnItem.IsValid() )
		{
			return OwnerAssetColumnItem.Pin()->GetToolTip();
		}

		return nullptr;
	}

	/** Forward the event to the view item that this name box belongs to */
	virtual void OnToolTipClosing() override
	{
		if ( OwnerAssetColumnItem.IsValid() )
		{
			OwnerAssetColumnItem.Pin()->OnToolTipClosing();
		}
	}

private:
	TWeakPtr<SAssetViewItem> OwnerAssetColumnItem;
};

void SAssetColumnItem::Construct( const FArguments& InArgs )
{
	SAssetViewItem::Construct( SAssetViewItem::FArguments()
		.AssetItem(InArgs._AssetItem)
		.OnRenameBegin(InArgs._OnRenameBegin)
		.OnRenameCommit(InArgs._OnRenameCommit)
		.OnVerifyRenameCommit(InArgs._OnVerifyRenameCommit)
		.OnItemDestroyed(InArgs._OnItemDestroyed)
		.HighlightText(InArgs._HighlightText)
		.OnIsAssetValidForCustomToolTip(InArgs._OnIsAssetValidForCustomToolTip)
		.OnGetCustomAssetToolTip(InArgs._OnGetCustomAssetToolTip)
		.OnVisualizeAssetToolTip(InArgs._OnVisualizeAssetToolTip)
		.OnAssetToolTipClosing(InArgs._OnAssetToolTipClosing)
		);
	
	HighlightText = InArgs._HighlightText;
}

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
TSharedRef<SWidget> SAssetColumnItem::GenerateWidgetForColumn( const FName& ColumnName, FIsSelected InIsSelected )
{
	TSharedPtr<SWidget> Content;

	// A little right padding so text from this column does not run directly into text from the next.
	static const FMargin ColumnItemPadding( 5, 0, 5, 0 );

	if ( ColumnName == "Name" )
	{
		const FSlateBrush* IconBrush;
		if(IsFolder())
		{
			if(ContentBrowserUtils::IsItemDeveloperContent(AssetItem->GetItem()))
			{
				IconBrush = FEditorStyle::GetBrush("ContentBrowser.ColumnViewDeveloperFolderIcon");
			}
			else
			{
				IconBrush = FEditorStyle::GetBrush("ContentBrowser.ColumnViewFolderIcon");
			}
		}
		else
		{
			IconBrush = FEditorStyle::GetBrush("ContentBrowser.ColumnViewAssetIcon");
		}

		// Make icon overlays (eg, SCC and dirty status) a reasonable size in relation to the icon size (note: it is assumed this icon is square)
		const float IconOverlaySize = IconBrush->ImageSize.X * 0.6f;

		Content = SNew(SHorizontalBox)
			.AddMetaData<FTagMetaData>(FTagMetaData(AssetItem->GetItem().GetVirtualPath()))
			// Icon
			+SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 4, 0)
			[
				SNew(SOverlay)
				
				// The actual icon
				+SOverlay::Slot()
				[
					SNew(SImage)
					.Image( IconBrush )
					.ColorAndOpacity(this, &SAssetColumnItem::GetAssetColor)
				]

				// Source control state
				+SOverlay::Slot()
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Top)
				[
					SNew(SBox)
					.WidthOverride(IconOverlaySize)
					.HeightOverride(IconOverlaySize)
					[
						SNew(SImage)
						.Image(this, &SAssetColumnItem::GetSCCStateImage)
					]
				]

				// Extra external state hook
				+ SOverlay::Slot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Top)
				[
					SNew(SBox)
					.HeightOverride(IconOverlaySize)
					.MaxDesiredWidth(IconOverlaySize)
					[
						GenerateExtraStateIconWidget(IconOverlaySize)
					]
				]

				// Dirty state
				+SOverlay::Slot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Bottom)
				[
					SNew(SBox)
					.WidthOverride(IconOverlaySize)
					.HeightOverride(IconOverlaySize)
					[
						SNew(SImage)
						.Image(this, &SAssetColumnItem::GetDirtyImage)
					]
				]
			]

			// Editable Name
			+SHorizontalBox::Slot()
			.AutoWidth()
			[
				SAssignNew(InlineRenameWidget, SInlineEditableTextBlock)
				.Text( GetNameText() )
				.OnBeginTextEdit(this, &SAssetColumnItem::HandleBeginNameChange)
				.OnTextCommitted(this, &SAssetColumnItem::HandleNameCommitted)
				.OnVerifyTextChanged(this, &SAssetColumnItem::HandleVerifyNameChanged)
				.HighlightText(HighlightText)
				.IsSelected(InIsSelected)
				.IsReadOnly(this, &SAssetColumnItem::IsNameReadOnly)
			];

		if(AssetItem.IsValid())
		{
			AssetItem->OnRenameRequested().BindSP(InlineRenameWidget.Get(), &SInlineEditableTextBlock::EnterEditingMode);
			AssetItem->OnRenameCanceled().BindSP(InlineRenameWidget.Get(), &SInlineEditableTextBlock::ExitEditingMode);
		}

		return SNew(SBorder)
			.BorderImage(this, &SAssetViewItem::GetBorderImage)
			.Padding(0)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Left)
			[
				SNew( SAssetColumnItemNameBox, SharedThis(this) )
				.Padding( ColumnItemPadding )
				[
					Content.ToSharedRef()
				]
			];
	}
	else if ( ColumnName == "Class" )
	{
		Content = SAssignNew(ClassText, STextBlock)
			.ToolTipText( this, &SAssetColumnItem::GetAssetClassText )
			.Text( GetAssetClassText() )
			.HighlightText( HighlightText );
	}
	else if ( ColumnName == "Path" )
	{
		Content = SAssignNew(PathText, STextBlock)
			.ToolTipText( this, &SAssetColumnItem::GetAssetPathText )
			.Text( GetAssetPathText() )
			.HighlightText( HighlightText );
	}
	else
	{
		Content = SNew(STextBlock)
			.ToolTipText( TAttribute<FText>::Create( TAttribute<FText>::FGetter::CreateSP(this, &SAssetColumnItem::GetAssetTagText, ColumnName) ) )
			.Text( TAttribute<FText>::Create( TAttribute<FText>::FGetter::CreateSP(this, &SAssetColumnItem::GetAssetTagText, ColumnName) ) );
	}

	return SNew(SBox)
		.Padding( ColumnItemPadding )
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Left)
		[
			Content.ToSharedRef()
		];
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SAssetColumnItem::OnAssetDataChanged()
{
	SAssetViewItem::OnAssetDataChanged();

	if ( ClassText.IsValid() )
	{
		ClassText->SetText( GetAssetClassText() );
	}

	if ( PathText.IsValid() )
	{
		PathText->SetText( GetAssetPathText() );
	}
}

FText SAssetColumnItem::GetAssetPathText() const
{
	return AssetItem
		? FText::AsCultureInvariant(AssetItem->GetItem().GetVirtualPath().ToString())
		: FText();
}

FText SAssetColumnItem::GetAssetTagText(FName AssetTag) const
{
	if (AssetItem)
	{
		// Check custom type
		{
			FText TagText;
			if (AssetItem->GetCustomColumnDisplayValue(AssetTag, TagText))
			{
				return TagText;
			}
		}

		// Check display tags
		{
			const FTagDisplayItem* FoundTagItem = CachedDisplayTags.FindByPredicate([AssetTag](const FTagDisplayItem& TagItem)
			{
				return TagItem.TagKey == AssetTag;
			});
			
			if (FoundTagItem)
			{
				return FoundTagItem->DisplayValue;
			}
		}

	}
	
	return FText();
}

#undef LOCTEXT_NAMESPACE
