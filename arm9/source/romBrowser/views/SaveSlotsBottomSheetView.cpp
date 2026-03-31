#include "common.h"
#include "gui/GraphicsContext.h"
#include "gui/input/InputProvider.h"
#include "themes/material/MaterialColorScheme.h"
#include "themes/IFontRepository.h"
#include "../IRomBrowserController.h"
#include "SaveSlotsBottomSheetView.h"

#define TITLE_LABEL_X           20
#define TITLE_LABEL_Y           16
#define DESCRIPTION_LABEL_X     20
#define DESCRIPTION_LABEL_Y     36
#define CHIP_ROW_Y              86
#define CHIP_GAP                8

SaveSlotsBottomSheetView::SaveSlotsBottomSheetView(
    IRomBrowserController* romBrowserController,
    const MaterialColorScheme* materialColorScheme,
    const IFontRepository* fontRepository)
    : _romBrowserController(romBrowserController)
    , _titleLabel(100, 16, 16, fontRepository->GetFont(FontType::Medium11))
    , _descriptionLabel(216, 16, 30, fontRepository->GetFont(FontType::Regular10))
    , _slot1Chip(md::sys::color::surfaceContainerLow, materialColorScheme, fontRepository)
    , _slot2Chip(md::sys::color::surfaceContainerLow, materialColorScheme, fontRepository)
    , _slot3Chip(md::sys::color::surfaceContainerLow, materialColorScheme, fontRepository)
{
    _titleLabel.SetText("Save Slot");
    _descriptionLabel.SetText("Choose which .sav file to use");
    _slot1Chip.SetText(u"Slot 1");
    _slot2Chip.SetText(u"Slot 2");
    _slot3Chip.SetText(u"Slot 3");

    AddChildTail(&_titleLabel);
    AddChildTail(&_descriptionLabel);
    AddChildTail(&_slot1Chip);
    AddChildTail(&_slot2Chip);
    AddChildTail(&_slot3Chip);
}

void SaveSlotsBottomSheetView::InitVram(const VramContext& vramContext)
{
    BottomSheetView::InitVram(vramContext);
}

void SaveSlotsBottomSheetView::Update()
{
    _titleLabel.SetPosition(TITLE_LABEL_X, _position.y + TITLE_LABEL_Y);
    _descriptionLabel.SetPosition(DESCRIPTION_LABEL_X, _position.y + DESCRIPTION_LABEL_Y);

    u32 saveSlot = _romBrowserController->GetTriggerSaveSlot();
    _slot1Chip.SetSelected(saveSlot == 1);
    _slot2Chip.SetSelected(saveSlot == 2);
    _slot3Chip.SetSelected(saveSlot == 3);

    int totalWidth = _slot1Chip.GetWidth() + _slot2Chip.GetWidth() + _slot3Chip.GetWidth() + 2 * CHIP_GAP;
    int x = (256 - totalWidth) / 2;
    _slot1Chip.SetPosition(x, _position.y + CHIP_ROW_Y);
    x += _slot1Chip.GetWidth() + CHIP_GAP;
    _slot2Chip.SetPosition(x, _position.y + CHIP_ROW_Y);
    x += _slot2Chip.GetWidth() + CHIP_GAP;
    _slot3Chip.SetPosition(x, _position.y + CHIP_ROW_Y);

    BottomSheetView::Update();
}

void SaveSlotsBottomSheetView::Draw(GraphicsContext& graphicsContext)
{
    graphicsContext.SetClipArea(GetBounds());
    u32 oldPrio = graphicsContext.SetPriority(1);
    {
        BottomSheetView::Draw(graphicsContext);
    }
    graphicsContext.SetPriority(oldPrio);
    graphicsContext.ResetClipArea();
}

void SaveSlotsBottomSheetView::Focus(FocusManager& focusManager)
{
    switch (_romBrowserController->GetTriggerSaveSlot())
    {
        case 2:
            focusManager.Focus(&_slot2Chip);
            break;
        case 3:
            focusManager.Focus(&_slot3Chip);
            break;
        case 1:
        default:
            focusManager.Focus(&_slot1Chip);
            break;
    }
}

View* SaveSlotsBottomSheetView::MoveFocus(View* currentFocus,
    FocusMoveDirection direction, View* source)
{
    if (currentFocus == &_slot1Chip && direction == FocusMoveDirection::Right)
        return &_slot2Chip;
    else if (currentFocus == &_slot2Chip)
    {
        if (direction == FocusMoveDirection::Left)
            return &_slot1Chip;
        else if (direction == FocusMoveDirection::Right)
            return &_slot3Chip;
    }
    else if (currentFocus == &_slot3Chip && direction == FocusMoveDirection::Left)
        return &_slot2Chip;
    return nullptr;
}

bool SaveSlotsBottomSheetView::HandleInput(const InputProvider& inputProvider, FocusManager& focusManager)
{
    if (inputProvider.Triggered(InputKey::A))
    {
        auto currentFocus = focusManager.GetCurrentFocus();
        u32 saveSlot = 1;
        if (currentFocus == &_slot2Chip)
            saveSlot = 2;
        else if (currentFocus == &_slot3Chip)
            saveSlot = 3;

        _romBrowserController->SetTriggerSaveSlot(saveSlot);
        _romBrowserController->HideGameInfo();
        return true;
    }
    else if (inputProvider.Triggered(InputKey::B | InputKey::X))
    {
        _romBrowserController->HideGameInfo();
        return true;
    }
    return false;
}
