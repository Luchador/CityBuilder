#include "ui_buttons.hpp"

#include <algorithm>
#include <stdexcept>

UiButton& UiButtons::Add(UiButtonId id, UiButtonTextures textures)
{
    if (Find(id)) 
    {
        throw std::invalid_argument("Duplicate UI button ID.");
    }

    entries.push_back({id, std::make_unique<UiButton>(textures)});

    return *entries.back().button;
}

UiButton* UiButtons::Find(UiButtonId id)
{
    for (const auto& entry : entries) 
    {
        if (entry.id == id) 
        {
            return entry.button.get();
        }
    }

    return nullptr;
}

const UiButton* UiButtons::Find(UiButtonId id) const
{
    for (const auto& entry : entries) 
    {
        if (entry.id == id) 
        {
            return entry.button.get();
        }
    }

    return nullptr;
}

bool UiButtons::Remove(UiButtonId id)
{
    const auto found = std::find_if(entries.begin(), entries.end(), [id](const Entry& entry) { return entry.id == id; });

    if (found == entries.end()) 
    {
        return false;
    }

    if (capturing && capturedId == id) 
    {
        found->button->CancelPress();
    }

    entries.erase(found);

    // Keep ownership until release, even if the original target disappeared.
    return true;
}

void UiButtons::Clear()
{
    CancelInteraction();
    entries.clear();
}

const UiButtons::Entry* UiButtons::TopButton(float x, float y) const
{
    for (auto i = entries.rbegin(); i != entries.rend(); ++i)
    {
        if (i->button->HitTest(x, y)) 
        {
            return &*i;
        }
    }
    return nullptr;
}

bool UiButtons::BlocksPointer(float x, float y) const
{
    return TopButton(x, y) != nullptr;
}

void UiButtons::PointerMove(float x, float y)
{
    const auto* top = TopButton(x, y);

    for (const auto& entry : entries)
    {
        const bool target = top && top->id == entry.id && (!capturing || capturedId == entry.id);
    
        if (target) 
        {
            entry.button->PointerMove(x, y);
        }
        else 
        {
            entry.button->PointerLeave();
        }
    }
}

UiButtonResult UiButtons::PointerDown(float x, float y)
{
    if (capturing) 
    {
        return {true, {}};
    }

    const auto* top = TopButton(x, y);

    if (!top) 
    { 
        PointerMove(x, y); 
        return {}; 
    }  

    capturing = true;
    capturedId = top->id;

    top->button->PointerDown(x, y);
    PointerMove(x, y);

    return {true, {}};
}

UiButtonResult UiButtons::PointerUp(float x, float y)
{
    if (!capturing) { PointerMove(x, y); return {}; }

    UiButtonResult result{true, {}};

    auto* target = Find(capturedId);
    const auto* top = TopButton(x, y);

    if (target)
    {
        const bool clicked = target->PointerUp(x, y);
        if (clicked && top && top->id == capturedId) 
        {
            result.clicked = capturedId;
        }
    }

    capturing = false;
    PointerMove(x, y);

    return result;
}

void UiButtons::SyncPointer(float x, float y, bool leftButtonDown)
{
    if (capturing && !leftButtonDown) 
    {
        CancelInteraction();
    }

    PointerMove(x, y);
}

void UiButtons::CancelInteraction()
{
    capturing = false;

    for (const auto& entry : entries) 
    {
        entry.button->CancelPress();
    }
}
