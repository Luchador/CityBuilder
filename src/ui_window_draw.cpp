#include "ui_window.hpp"

#include "ui_image.hpp"

#include <algorithm>
#include <cmath>

namespace
{
    UiRect Intersection(UiRect a, UiRect b)
    {
        const float right = std::min(a.x + a.w, b.x + b.w);
        const float bottom = std::min(a.y + a.h, b.y + b.h);
        a.x = std::max(a.x, b.x);
        a.y = std::max(a.y, b.y);
        a.w = std::max(0.0f, right - a.x);
        a.h = std::max(0.0f, bottom - a.y);
        return a;
    }

    void Quad(std::vector<UiVertex>& mesh, UiRect r, SDL_FColor top, SDL_FColor bottom)
    {
        if (r.w <= 0 || r.h <= 0) 
        {
            return;
        }
    
        const UiVertex a{r.x, r.y, top.r, top.g, top.b, top.a};
        const UiVertex b{r.x + r.w, r.y, top.r, top.g, top.b, top.a};
        const UiVertex c{r.x + r.w, r.y + r.h, bottom.r, bottom.g, bottom.b, bottom.a};
        const UiVertex d{r.x, r.y + r.h, bottom.r, bottom.g, bottom.b, bottom.a};

        mesh.insert(mesh.end(), {a, b, c, a, c, d});
    }

    void Fill(std::vector<UiVertex>& mesh, UiRect r, SDL_FColor color) { Quad(mesh, r, color, color); }

    void Frame(std::vector<UiVertex>& mesh, UiRect r, SDL_FColor color)
    {
        if (r.w <= 0 || r.h <= 0) 
        {
            return;
        }
    
        Fill(mesh, {r.x, r.y, r.w, 1}, color);
        Fill(mesh, {r.x, r.y + r.h - 1, r.w, 1}, color);
        Fill(mesh, {r.x, r.y, 1, r.h}, color);
        Fill(mesh, {r.x + r.w - 1, r.y, 1, r.h}, color);
    }

    void Scissor(SDL_GPURenderPass* pass, UiRect r, float width, float height, Uint32 pixelsX, Uint32 pixelsY)
    {
        const float sx = static_cast<float>(pixelsX) / width, sy = static_cast<float>(pixelsY) / height;
        const int left = static_cast<int>(std::clamp(std::ceil(r.x * sx), 0.0f, static_cast<float>(pixelsX)));
        const int top = static_cast<int>(std::clamp(std::ceil(r.y * sy), 0.0f, static_cast<float>(pixelsY)));
        const int right = static_cast<int>(std::clamp(std::floor((r.x + r.w) * sx), 0.0f, static_cast<float>(pixelsX)));
        const int bottom = static_cast<int>(std::clamp(std::floor((r.y + r.h) * sy), 0.0f, static_cast<float>(pixelsY)));
        const SDL_Rect clip{left, top, std::max(0, right - left), std::max(0, bottom - top)};

        SDL_SetGPUScissor(pass, &clip);
    }
}

void UiWindow::AppendGeometry(std::vector<UiVertex>& mesh) const
{
    if (!shown) 
    {
        return;
    }

    const SDL_FColor edge{0.10f, 0.16f, 0.22f, 1}, light{0.66f, 0.77f, 0.86f, 1};
    Quad(mesh, bounds, {0.28f, 0.38f, 0.47f, 1}, {0.18f, 0.26f, 0.34f, 1});
    const UiRect bar{bounds.x + 1, bounds.y + 1, bounds.w - 2, TitleHeight - 1};
    Quad(mesh, bar, focused ? SDL_FColor{0.44f, 0.61f, 0.75f, 1} : SDL_FColor{0.34f, 0.44f, 0.53f, 1}, {0.22f, 0.34f, 0.46f, 1});
    Frame(mesh, bounds, edge);
    Frame(mesh, {bounds.x + 1, bounds.y + 1, bounds.w - 2, bounds.h - 2}, light);
    Fill(mesh, {bounds.x + 1, bounds.y + TitleHeight, bounds.w - 2, 1}, edge);
    const auto content = GetContentBounds();

    for (const auto& item : controls)
    {
        const auto& control = *item;
        const auto rectangle = BoundsOf(control);
        const auto clip = control.footer ? bounds : content;
        const bool active = focused && focusedControl == &control;
    
        if (control.kind == Kind::TextBox)
        {
            Fill(mesh, Intersection(rectangle, clip), active ? light : edge);
            Fill(mesh, Intersection({rectangle.x + 1, rectangle.y + 1, rectangle.w - 2, rectangle.h - 2}, clip), {0.07f, 0.12f, 0.17f, 1});

            const UiRect inner = Intersection({rectangle.x + 8, rectangle.y + 3, std::max(0.0f, rectangle.w - 16), std::max(0.0f, rectangle.h - 6)}, clip);
            if (active && control.selectionEnd > control.selectionStart)
            {
                Fill(mesh, Intersection({rectangle.x + 8 + control.selectionStart - control.scroll, inner.y, control.selectionEnd - control.selectionStart, inner.h}, inner), {0.25f, 0.43f, 0.63f, 1});
            }

            if (active && caretVisible)
            {
                Fill(mesh, Intersection({rectangle.x + 8 + control.caretX - control.scroll, inner.y + 2, 1, std::max(0.0f, inner.h - 4)}, inner), {1, 1, 1, 1});
            }
        }
        else if (active && control.button)
        {
            // An outer focus ring remains visible around the button artwork.
            Frame(mesh, Intersection({rectangle.x - 2, rectangle.y - 2, rectangle.w + 4, rectangle.h + 4}, clip), light);
        }
    }
}

void UiWindow::DrawImages(UiImageRenderer& images, SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass,
                          float width, float height, Uint32 pixelWidth, Uint32 pixelHeight) const
{
    if (!shown) 
    {
        return;
    }

    Scissor(pass, bounds, width, height, pixelWidth, pixelHeight);
    images.Draw(commands, pass, *closeButton.GetTexture(), closeButton.GetBounds(), width, height);

    for (const auto& control : controls)
    {
        Scissor(pass, control->footer ? bounds : GetContentBounds(), width, height, pixelWidth, pixelHeight);
        auto rectangle = BoundsOf(*control);
        const Texture* image = control->image;
    
        if (control->button)
        {
            image = control->button->GetTexture();
    
            if (control->kind == Kind::Radio)
            {
                rectangle.y += (rectangle.h - 24) * 0.5f;
                rectangle.w = rectangle.h = 24;
            }
        }
    
        if (image) 
        {
            images.Draw(commands, pass, *image, rectangle, width, height);
        }
    }

    const SDL_Rect full{0, 0, static_cast<int>(pixelWidth), static_cast<int>(pixelHeight)};
    SDL_SetGPUScissor(pass, &full);
}
