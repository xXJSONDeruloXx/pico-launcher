#pragma once
#include "BottomSheetView.h"
#include "ChipView.h"
#include "gui/views/Label2DView.h"
#include "gui/FocusManager.h"

class IRomBrowserController;
class IFontRepository;
class MaterialColorScheme;

class SaveSlotsBottomSheetView : public BottomSheetView
{
public:
    SaveSlotsBottomSheetView(
        IRomBrowserController* romBrowserController,
        const MaterialColorScheme* materialColorScheme,
        const IFontRepository* fontRepository);

    void SetGraphics(const ChipView::VramToken& chipVramToken)
    {
        _slot1Chip.SetGraphics(chipVramToken);
        _slot2Chip.SetGraphics(chipVramToken);
        _slot3Chip.SetGraphics(chipVramToken);
    }

    void InitVram(const VramContext& vramContext) override;
    void Update() override;
    void Draw(GraphicsContext& graphicsContext) override;

    void Focus(FocusManager& focusManager) override;

    View* MoveFocus(View* currentFocus, FocusMoveDirection direction, View* source) override;
    bool HandleInput(const InputProvider& inputProvider, FocusManager& focusManager) override;

private:
    IRomBrowserController* _romBrowserController;
    Label2DView _titleLabel;
    Label2DView _descriptionLabel;
    ChipView _slot1Chip;
    ChipView _slot2Chip;
    ChipView _slot3Chip;
};
