#include "ui_buttons.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

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
        capturedId.reset();
    }

    // Keep other children hidden if their trigger is removed. The definition
    // remains until Clear(), so reusing that stable trigger ID restores it.
    if (openFlyout == id) openFlyout.reset();

    entries.erase(found);

    // Keep ownership until release, even if the original target disappeared.
    return true;
}

void UiButtons::Clear()
{
    CancelInteraction();
    entries.clear();
    flyouts.clear();
    onlyShown.reset();
}

void UiButtons::ShowOnly(std::optional<UiButtonId> id)
{
    if (id && !Find(*id))
    {
        throw std::invalid_argument("Visible UI button ID does not exist.");
    }
    if (onlyShown == id) return;

    CancelInteraction();
    onlyShown = id;
}

void UiButtons::AddFlyout(UiButtonId trigger, std::span<const UiButtonId> children)
{
    if (!Find(trigger) || FindFlyout(trigger) || children.empty())
        throw std::invalid_argument("Flyout requires a unique existing trigger and existing child buttons.");

    std::vector<UiButtonId> checked;
    for (UiButtonId child : children)
    {
        if (child == trigger || !Find(child) || FindFlyout(child) ||
            std::find(checked.begin(), checked.end(), child) != checked.end())
            throw std::invalid_argument("Flyout children must be unique and cannot be triggers.");
        for (const auto& menu : flyouts)
        {
            if (std::find(menu.children.begin(), menu.children.end(), child) != menu.children.end() ||
                std::find(menu.children.begin(), menu.children.end(), trigger) != menu.children.end())
                throw std::invalid_argument("A button can belong to only one first-level flyout.");
        }
        checked.push_back(child);
    }
    flyouts.push_back({trigger, std::move(checked), {}});
}

void UiButtons::SetFlyoutBounds(UiButtonId trigger, UiRect bounds)
{
    if (!std::isfinite(bounds.x) || !std::isfinite(bounds.y) || !std::isfinite(bounds.w) ||
        !std::isfinite(bounds.h) || bounds.w < 0 || bounds.h < 0)
        throw std::invalid_argument("Flyout bounds must be finite and nonnegative in size.");
    for (auto& menu : flyouts)
    {
        if (menu.trigger == trigger) { menu.bounds = bounds; return; }
    }
    throw std::invalid_argument("Flyout trigger does not exist.");
}

const UiButtons::Flyout* UiButtons::FindFlyout(UiButtonId trigger) const
{
    for (const auto& menu : flyouts)
        if (menu.trigger == trigger) return &menu;
    return nullptr;
}

const UiButtons::Flyout* UiButtons::ActiveFlyout() const
{
    if (!openFlyout || onlyShown) return nullptr;
    const auto* trigger = Find(*openFlyout);
    return trigger && trigger->IsVisible() && trigger->IsEnabled() ? FindFlyout(*openFlyout) : nullptr;
}

bool UiButtons::HasOpenFlyout() const { return ActiveFlyout() != nullptr; }

bool UiButtons::IsInActiveFlyout(UiButtonId id) const
{
    const auto* menu = ActiveFlyout();
    return menu && (menu->trigger == id ||
        std::find(menu->children.begin(), menu->children.end(), id) != menu->children.end());
}

bool UiButtons::IsShown(const Entry& entry) const
{
    if (!entry.button->IsVisible() || (onlyShown && entry.id != *onlyShown)) return false;
    for (const auto& menu : flyouts)
    {
        if (std::find(menu.children.begin(), menu.children.end(), entry.id) != menu.children.end())
            return ActiveFlyout() == &menu;
    }
    return true;
}

const UiButtons::Entry* UiButtons::TopButton(float x, float y) const
{
    for (auto i = entries.rbegin(); i != entries.rend(); ++i)
    {
        if (IsShown(*i) && i->button->HitTest(x, y))
        {
            return &*i;
        }
    }
    return nullptr;
}

bool UiButtons::BlocksPointer(float x, float y) const
{
    const auto* menu = ActiveFlyout();
    return TopButton(x, y) != nullptr || (menu && menu->bounds.Contains(x, y));
}

void UiButtons::PointerMove(float x, float y)
{
    if (openFlyout && !ActiveFlyout())
    {
        // A trigger may have been hidden/disabled/removed while held. Keep the
        // release owned by UI, but never turn it into a stale button click.
        for (const auto& entry : entries) entry.button->CancelPress();
        openFlyout.reset();
        capturedId.reset();
        flyoutGesture = false;
    }
    const auto* top = TopButton(x, y);

    // Only a menu gesture may transfer a held press to another button. Ordinary
    // buttons keep their original capture and release-outside cancellation.
    if (capturing && flyoutGesture && top && IsInActiveFlyout(top->id) &&
        top->button->IsEnabled() && capturedId != top->id)
    {
        if (capturedId)
            if (auto* previous = Find(*capturedId)) previous->CancelPress();
        capturedId = top->id;
        top->button->PointerDown(x, y);
    }

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

    if (const auto* menu = ActiveFlyout(); menu && (!top || !IsInActiveFlyout(top->id)))
    {
        if (!menu->bounds.Contains(x, y))
        {
            CancelInteraction();
            if (!top)
            {
                // The outside click dismisses the menu; its release must not
                // become a world/minimap click or a later button activation.
                capturing = true;
                return {true, {}};
            }
        }
        else if (!top)
        {
            capturing = true;
            capturedId.reset();
            flyoutGesture = true;
            return {true, {}};
        }
    }

    if (!top) 
    { 
        PointerMove(x, y); 
        return {}; 
    }  

    capturing = true;
    capturedId = top->id;

    if (top->button->IsEnabled() && FindFlyout(top->id)) openFlyout = top->id;
    flyoutGesture = IsInActiveFlyout(top->id);

    top->button->PointerDown(x, y);
    PointerMove(x, y);

    return {true, {}};
}

UiButtonResult UiButtons::PointerUp(float x, float y)
{
    if (!capturing) { PointerMove(x, y); return {}; }

    UiButtonResult result{true, {}};

    // A release can carry a new position even if no motion event was received.
    PointerMove(x, y);
    auto* target = capturedId ? Find(*capturedId) : nullptr;
    const auto* top = TopButton(x, y);

    if (target)
    {
        const bool clicked = target->PointerUp(x, y);
        if (clicked && top && top->id == capturedId) 
        {
            result.clicked = *capturedId;
        }
    }

    capturing = false;
    capturedId.reset();
    if (flyoutGesture && !result.clicked) openFlyout.reset();
    flyoutGesture = false;
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
    capturedId.reset();
    openFlyout.reset();
    flyoutGesture = false;

    for (const auto& entry : entries) 
    {
        entry.button->CancelPress();
    }
}
