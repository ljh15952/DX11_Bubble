#include "Graphics/BitmapFont.h"
#include "Graphics/Renderer.h"
#include "Core/Log.h"

namespace
{
    constexpr char kLastChar = '~';   // ASCII 126. 시트의 마지막 글자.
}


bool BitmapFont::Load(Renderer& renderer, const wchar_t* path,
                      int cellWidth, int cellHeight, int columns, char firstChar)
{
    m_texture = renderer.LoadTexture(path);
    if (!m_texture)
        return false;

    m_cellW     = cellWidth;
    m_cellH     = cellHeight;
    m_columns   = columns;
    m_firstChar = firstChar;
    return true;
}


RECT BitmapFont::GlyphRect(char c) const
{
    // 시트에 없는 문자는 '?' 로 대체한다.
    // 이렇게 해두면 이상한 글자가 나와도 화면이 깨지지 않고 눈에 띈다.
    if (c < m_firstChar || c > kLastChar)
        c = '?';

    const int index = static_cast<int>(c) - static_cast<int>(m_firstChar);
    const LONG left = static_cast<LONG>((index % m_columns) * m_cellW);
    const LONG top  = static_cast<LONG>((index / m_columns) * m_cellH);

    // right / bottom 은 배타적이므로 +cellW, +cellH 가 맞다
    return { left, top, left + m_cellW, top + m_cellH };
}


void BitmapFont::Draw(Renderer& renderer, std::string_view text, float x, float y,
                      DirectX::FXMVECTOR color, int scale) const
{
    if (!m_texture)
        return;
    if (scale < 1)
        scale = 1;

    const float advance = static_cast<float>(m_cellW * scale);
    const float lineH   = static_cast<float>(m_cellH * scale);

    float cx = x;
    float cy = y;

    for (const char c : text)
    {
        if (c == '\n')
        {
            cx = x;
            cy += lineH;
            continue;
        }

        // 공백은 그릴 필요가 없다. 드로우 콜을 아낀다.
        if (c != ' ')
        {
            const RECT src = GlyphRect(c);
            renderer.Sprites().Draw(
                m_texture.Get(),
                DirectX::XMFLOAT2(cx, cy),
                &src,
                color,
                0.0f,                              // 회전 없음
                DirectX::XMFLOAT2(0.0f, 0.0f),     // 원점 = 좌상단
                static_cast<float>(scale));
        }
        cx += advance;
    }
}


void BitmapFont::DrawCentered(Renderer& renderer, std::string_view text, float centerX, float y,
                              DirectX::FXMVECTOR color, int scale) const
{
    const float w = MeasureWidth(text, scale);

    // ★ 좌표를 정수로 맞춘다.
    //   0.5 픽셀에 그리면 캔버스 픽셀 격자와 어긋나 글자가 흐려진다.
    const float x = static_cast<float>(static_cast<int>(centerX - w * 0.5f));
    Draw(renderer, text, x, y, color, scale);
}


float BitmapFont::MeasureWidth(std::string_view text, int scale) const
{
    if (scale < 1)
        scale = 1;

    size_t longest = 0;
    size_t current = 0;
    for (const char c : text)
    {
        if (c == '\n')
        {
            if (current > longest) longest = current;
            current = 0;
        }
        else
        {
            ++current;
        }
    }
    if (current > longest)
        longest = current;

    return static_cast<float>(longest) * static_cast<float>(m_cellW * scale);
}


float BitmapFont::MeasureHeight(std::string_view text, int scale) const
{
    if (scale < 1)
        scale = 1;

    size_t lines = 1;
    for (const char c : text)
        if (c == '\n') ++lines;

    return static_cast<float>(lines) * static_cast<float>(m_cellH * scale);
}
