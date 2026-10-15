#pragma once

#include "ui.hpp"

class UiMinimap
{
public:
    void SetLayout(const UiLayout& layout);

    const UiRect& GetBounds() const;
    const UiRect& GetContentBounds() const;

    bool Contains(float x, float y) const;

private:
    UiRect bounds{};
    UiRect contentBounds{};
};