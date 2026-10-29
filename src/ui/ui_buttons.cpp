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
    if (const auto menu = std::find(openFlyouts.begin(), openFlyouts.end(), id); menu != openFlyouts.end())
    {
        openFlyouts.erase(menu, openFlyouts.end());
    }

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

    if (onlyShown == id) 
    {
        return;
    }

    CancelInteraction();
    onlyShown = id;
}

void UiButtons::AddFlyout(UiButtonId trigger, std::span<const UiButtonId> children)
{
    if (!Find(trigger) || FindFlyout(trigger) || children.empty())
    {
        throw std::invalid_argument("Flyout requires a unique existing trigger and existing child buttons.");
    }

    std::vector<UiButtonId> checked;

    for (UiButtonId child : children)
    {
        if (!Find(child) || ParentFlyout(child) || std::find(checked.begin(), checked.end(), child) != checked.end())
        {
            throw std::invalid_argument("Flyout children must exist and have only one parent.");
        }

        // A child can itself be a trigger, but cannot be this trigger or one of
        // its ancestors. This works regardless of menu registration order.
        for (UiButtonId ancestor = trigger;;)
        {
            if (child == ancestor)
            {
                throw std::invalid_argument("Flyout hierarchy cannot contain cycles.");
            }
            const auto* parent = ParentFlyout(ancestor);
            if (!parent) break;
            ancestor = parent->trigger;
        }
        checked.push_back(child);
    }

    flyouts.push_back({trigger, std::move(checked), {}});
}

void UiButtons::SetFlyoutBounds(UiButtonId trigger, UiRect bounds)
{
    if (!std::isfinite(bounds.x) || !std::isfinite(bounds.y) || !std::isfinite(bounds.w) || !std::isfinite(bounds.h) || bounds.w < 0 || bounds.h < 0)
    {
        throw std::invalid_argument("Flyout bounds must be finite and nonnegative in size.");
    }

    for (auto& menu : flyouts)
    {
        if (menu.trigger == trigger) 
        { 
            menu.bounds = bounds; 
            return; 
        }
    }

    throw std::invalid_argument("Flyout trigger does not exist.");
}

const UiButtons::Flyout* UiButtons::FindFlyout(UiButtonId trigger) const
{
    for (const auto& menu : flyouts)
    {
        if (menu.trigger == trigger) 
        {
            return &menu;
        }
    }
    return nullptr;
}

const UiButtons::Flyout* UiButtons::ParentFlyout(UiButtonId child) const
{
    for (const auto& menu : flyouts)
    {
        if (std::find(menu.children.begin(), menu.children.end(), child) != menu.children.end()) return &menu;
    }
    return nullptr;
}

std::size_t UiButtons::ActiveFlyoutCount() const
{
    if (onlyShown) return 0;
    std::size_t count = 0;
    for (UiButtonId id : openFlyouts)
    {
        const auto* button = Find(id);
        const auto* parent = ParentFlyout(id);
        if (!button || !button->IsVisible() || !button->IsEnabled() || !FindFlyout(id) ||
            (count == 0 ? parent != nullptr : !parent || parent->trigger != openFlyouts[count - 1])) break;
        ++count;
    }
    return count;
}

bool UiButtons::IsFlyoutOpen(UiButtonId trigger) const
{
    const auto end = openFlyouts.begin() + ActiveFlyoutCount();
    return std::find(openFlyouts.begin(), end, trigger) != end;
}

bool UiButtons::HasOpenFlyout() const
{ 
    return ActiveFlyoutCount() != 0;
}

bool UiButtons::IsInActiveFlyout(UiButtonId id) const
{
    const auto* parent = ParentFlyout(id);
    return IsFlyoutOpen(id) || (parent && IsFlyoutOpen(parent->trigger));
}

bool UiButtons::ContainsFlyoutPoint(float x, float y) const
{
    const auto count = ActiveFlyoutCount();
    for (std::size_t i = 0; i < count; ++i)
        if (FindFlyout(openFlyouts[i])->bounds.Contains(x, y)) return true;
    return false;
}

void UiButtons::OpenFlyout(UiButtonId trigger)
{
    const auto* button = Find(trigger);
    if (!button || !button->IsVisible() || !button->IsEnabled() || !FindFlyout(trigger) || onlyShown) return;
    std::size_t depth = 0;
    if (const auto* parent = ParentFlyout(trigger))
    {
        if (!IsFlyoutOpen(parent->trigger)) return;
        depth = static_cast<std::size_t>(std::find(openFlyouts.begin(), openFlyouts.end(), parent->trigger) - openFlyouts.begin()) + 1;
    }
    openFlyouts.resize(depth);
    openFlyouts.push_back(trigger);
}

void UiButtons::UpdateHoveredFlyout(UiButtonId id)
{
    const auto* parent = ParentFlyout(id);
    if (!parent || !IsFlyoutOpen(parent->trigger)) return; // Root menus require a press.
    const auto depth = static_cast<std::size_t>(std::find(openFlyouts.begin(), openFlyouts.end(), parent->trigger) - openFlyouts.begin()) + 1;
    // Keep an existing descendant branch when moving back over its trigger.
    if (depth < openFlyouts.size() && openFlyouts[depth] == id) return;
    openFlyouts.resize(depth); // A sibling without a submenu also closes the old branch.
    OpenFlyout(id);
}

bool UiButtons::IsShown(const Entry& entry) const
{
    if (!entry.button->IsVisible() || (onlyShown && entry.id != *onlyShown)) 
    {
        return false;
    }

    const auto* parent = ParentFlyout(entry.id);
    return !parent || IsFlyoutOpen(parent->trigger);
}

const UiButtons::Entry* UiButtons::TopButton(float x, float y) const
{
    for (auto i = entries.rbegin(); i != entries.rend(); ++i)
    {
        if (IsInActiveFlyout(i->id) && IsShown(*i) && i->button->HitTest(x, y))
        {
            return &*i;
        }
    }
    // Menu gaps belong to the menu, even if another button is underneath.
    if (ContainsFlyoutPoint(x, y)) return nullptr;
    for (auto i = entries.rbegin(); i != entries.rend(); ++i)
        if (IsShown(*i) && i->button->HitTest(x, y)) return &*i;
    return nullptr;
}

bool UiButtons::BlocksPointer(float x, float y) const
{
    return TopButton(x, y) != nullptr || ContainsFlyoutPoint(x, y);
}

const UiButton* UiButtons::ButtonAt(float x, float y) const
{
    const auto* entry = TopButton(x, y);
    return entry ? entry->button.get() : nullptr;
}

void UiButtons::PointerMove(float x, float y)
{
    const auto count = ActiveFlyoutCount();
    if (count != openFlyouts.size())
    {
        // A trigger may have been hidden/disabled/removed while held. Keep the
        // release owned by UI, but never turn it into a stale button click.
        openFlyouts.resize(count);
        if (capturedId && flyoutGesture && !IsInActiveFlyout(*capturedId))
        {
            if (auto* button = Find(*capturedId)) button->CancelPress();
            capturedId.reset();
        }
        flyoutGesture = flyoutGesture && HasOpenFlyout();
    }

    auto* top = TopButton(x, y);
    if (top && (!capturing || flyoutGesture))
    {
        UpdateHoveredFlyout(top->id);
        top = TopButton(x, y);
    }

    // Only a menu gesture may transfer a held press to another button. Ordinary
    // buttons keep their original capture and release-outside cancellation.
    if (capturing && flyoutGesture && top && IsInActiveFlyout(top->id) && top->button->IsEnabled() && capturedId != top->id)
    {
        if (capturedId)
        {
            if (auto* previous = Find(*capturedId)) 
            {
                previous->CancelPress();
            }
        }

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

    if (HasOpenFlyout() && (!top || !IsInActiveFlyout(top->id)))
    {
        if (!ContainsFlyoutPoint(x, y))
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

    if (top->button->IsEnabled() && FindFlyout(top->id)) 
    {
        OpenFlyout(top->id);
    }

    flyoutGesture = IsInActiveFlyout(top->id);

    top->button->PointerDown(x, y);
    PointerMove(x, y);

    return {true, {}};
}

UiButtonResult UiButtons::PointerUp(float x, float y)
{
    if (!capturing) 
    { 
        PointerMove(x, y); 
        return {}; 
    }

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

    if (flyoutGesture && !result.clicked) 
    {
        openFlyouts.clear();
    }

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
    openFlyouts.clear();
    flyoutGesture = false;

    for (const auto& entry : entries) 
    {
        entry.button->CancelPress();
    }
}
