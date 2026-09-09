// ============================================================================
//  BitmapFont.h
//    문자를 미리 그려 넣은 텍스처(폰트 시트)에서 글리프를 잘라 그린다.
//
//    스프라이트시트와 원리가 완전히 같다.
//    다른 점은 "몇 번째 칸을 그릴까" 를 애니메이션 프레임이 아니라
//    문자 코드로 정한다는 것뿐이다.
//
//      font_8x14.png (128×84)  —  8×14 셀, 16 열 × 6 행
//         ' ' 부터 '~' 까지 ASCII 95 자
//
//         인덱스 = 문자코드 - firstChar
//         열     = 인덱스 % columns
//         행     = 인덱스 / columns
//
//    ---- 왜 TrueType 을 직접 쓰지 않는가 ----
//      런타임에 TrueType 을 래스터화하면 안티에일리어싱이 들어가 도트가 흐려진다.
//      미리 비트맵으로 구워두면 항상 픽셀 단위로 또렷하고, 확대도 정수배로 깔끔하다.
//      대신 글자 종류가 시트에 있는 것으로 고정된다(지금은 ASCII 만).
// ============================================================================
#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <string_view>

#include <DirectXMath.h>
#include <DirectXColors.h>

class Renderer;

class BitmapFont
{
public:
    bool Load(Renderer& renderer, const wchar_t* path,
              int cellWidth, int cellHeight, int columns, char firstChar = ' ');

    bool Ready() const { return m_texture != nullptr; }

    // 좌상단 (x, y) 기준. '\n' 으로 줄바꿈.
    // ★ scale 은 정수만. 소수를 넣으면 픽셀이 들쭉날쭉해진다.
    void Draw(Renderer& renderer, std::string_view text, float x, float y,
              DirectX::FXMVECTOR color = DirectX::Colors::White, int scale = 1) const;

    // 가로 가운데 정렬. centerX 를 중심으로 그린다.
    void DrawCentered(Renderer& renderer, std::string_view text, float centerX, float y,
                      DirectX::FXMVECTOR color = DirectX::Colors::White, int scale = 1) const;

    // 레이아웃 계산용. 여러 줄이면 가장 긴 줄의 폭을 돌려준다.
    float MeasureWidth(std::string_view text, int scale = 1) const;
    float MeasureHeight(std::string_view text, int scale = 1) const;

    int CellWidth()  const { return m_cellW; }
    int CellHeight() const { return m_cellH; }

private:
    RECT GlyphRect(char c) const;

    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_texture;
    int  m_cellW     = 0;
    int  m_cellH     = 0;
    int  m_columns   = 0;
    char m_firstChar = ' ';
};
