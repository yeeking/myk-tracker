#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <cmath>
#include <initializer_list>

class Segment14Geometry
{
public:
    struct Vec2 { float x{}, y{}; };
    struct Vertex
    {
        float x, y, z;     // position
        float nx, ny, nz;  // normal (set to +Z)
    };

    struct Mesh
    {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
    };

    // 14 segments + optional dot (DP)
    enum Segment : uint8_t
    {
        A=0,  // top horizontal
        B,    // upper right vertical
        C,    // lower right vertical
        D,    // bottom horizontal
        E,    // lower left vertical
        F,    // upper left vertical
        G1,   // middle left horizontal
        G2,   // middle right horizontal
        H,    // upper left diagonal (toward center)
        I,    // upper right diagonal (toward center)
        J,    // lower left diagonal (toward center)
        K,    // lower right diagonal (toward center)
        L,    // center vertical (upper half)
        M,    // center vertical (lower half)
        DP    // dot / decimal point
    };

    struct Params
    {
        float cellW = 1.0f;
        float cellH = 1.6f;
        float thickness = 0.14f;
        float inset = 0.12f;
        float gap = 0.06f;
        float advance = 1.12f;
        float slant = 0.0f;
        bool  includeDot = true;
        float z = 0.0f;
    };

    Segment14Geometry() : params()
    {
        buildDefaultMap();
        buildSegmentRects();
    }

    explicit Segment14Geometry(const Params& p) : params(p)
    {
        buildDefaultMap();
        buildSegmentRects();
    }

    void setParams(const Params& p)
    {
        params = p;
        buildSegmentRects();
    }

    Mesh buildStringMesh(const std::string& s) const
    {
        Mesh out;
        out.vertices.reserve(s.size() * 15 * 4);
        out.indices.reserve(s.size() * 15 * 6);

        float penX = 0.0f;

        for (char ch : s)
        {
            const uint16_t mask = charMap[static_cast<unsigned char>(ch)];

            for (int seg = 0; seg < 15; ++seg)
            {
                if (seg == DP && !params.includeDot) continue;
                if ((mask & (1u << seg)) == 0) continue;
                appendSegmentQuad(out, static_cast<Segment>(seg), penX);
            }

            penX += params.advance;
        }

        return out;
    }

    Mesh buildCharMesh(char ch) const { return buildStringMesh(std::string(1, ch)); }

    void setCharMask(char ch, uint16_t mask)
    {
        charMap[static_cast<unsigned char>(ch)] = mask;
    }

    static constexpr uint16_t bits(std::initializer_list<Segment> segs)
    {
        uint16_t m = 0;
        for (auto s : segs) m |= (1u << static_cast<int>(s));
        return m;
    }

private:
    Params params{};
    std::array<uint16_t, 256> charMap{};

    struct Quad { Vec2 p0, p1, p2, p3; };
    std::array<Quad, 15> segmentQuads{};

private:
    Vec2 transform(Vec2 p, float penX) const
    {
        const float sx = p.x + params.slant * (p.y - params.cellH * 0.5f);
        return { sx + penX, p.y };
    }

    void appendSegmentQuad(Mesh& m, Segment seg, float penX) const
    {
        const auto& q = segmentQuads[static_cast<int>(seg)];
        const uint32_t base = static_cast<uint32_t>(m.vertices.size());

        const Vec2 t0 = transform(q.p0, penX);
        const Vec2 t1 = transform(q.p1, penX);
        const Vec2 t2 = transform(q.p2, penX);
        const Vec2 t3 = transform(q.p3, penX);

        constexpr float nx=0.0f, ny=0.0f, nz=1.0f;

        m.vertices.push_back({ t0.x, t0.y, params.z, nx, ny, nz });
        m.vertices.push_back({ t1.x, t1.y, params.z, nx, ny, nz });
        m.vertices.push_back({ t2.x, t2.y, params.z, nx, ny, nz });
        m.vertices.push_back({ t3.x, t3.y, params.z, nx, ny, nz });

        m.indices.push_back(base + 0);
        m.indices.push_back(base + 1);
        m.indices.push_back(base + 2);
        m.indices.push_back(base + 2);
        m.indices.push_back(base + 3);
        m.indices.push_back(base + 0);
    }

    void buildDefaultMap()
    {
        charMap.fill(0);

        // ---------- Digits ----------
        charMap['0'] = bits({A,B,C,D,E,F});
        charMap['1'] = bits({B,C});
        charMap['2'] = bits({A,B,D,E,G1,G2});
        charMap['3'] = bits({A,B,C,D,G1,G2});
        charMap['4'] = bits({B,C,F,G1,G2});
        charMap['5'] = bits({A,C,D,F,G1,G2});
        charMap['6'] = bits({A,C,D,E,F,G1,G2});
        charMap['7'] = bits({A,B,C});
        charMap['8'] = bits({A,B,C,D,E,F,G1,G2});
        charMap['9'] = bits({A,B,C,D,F,G1,G2});

        // ---------- Punctuation (the set you already had) ----------
        charMap['-'] = bits({G1,G2});
        charMap['_'] = bits({D});
        charMap['.'] = bits({DP});
        charMap[':'] = bits({DP});     // (single dot; if you want true colon, add a second dot segment later)
        charMap['/'] = bits({I,K});    // diagonal-ish
        charMap['\\']= bits({H,J});
        charMap['@'] = bits({A,B,C,D,E,F,G1,G2,H,I,J,K,L,M,DP});
        charMap[' '] = 0;

        // ---------- Uppercase A–Z ----------
        // These are sensible “14-seg” defaults optimized for readability at small sizes.
        // You can tweak any glyph with setCharMask().
        charMap['A'] = bits({A,B,C,E,F,G1,G2});
        charMap['B'] = bits({A,F,B,G1,G2,E,C,D});                 // “B” approximation
        charMap['C'] = bits({A,D,E,F});
        // charMap['D'] = bits({E,C,D,G1,G2,B});                 // “D” approximation
        charMap['D'] = bits({A,B,C,D,E,F});
        charMap['d'] = bits({E,C,D,G1,G2,B});                 // “D” approximation
        charMap['E'] = bits({A,D,E,F,G1,G2});
        charMap['F'] = bits({A,E,F,G1,G2});
        charMap['G'] = bits({A,C,D,E,F,G2});                      // readable “G”
        charMap['H'] = bits({B,C,E,F,G1,G2});
        charMap['I'] = bits({A,D,L,M});
        charMap['J'] = bits({B,C,D,E});
        charMap['K'] = bits({F,E,G1,I,K});                       // diagonals + center stem
        charMap['L'] = bits({D,E,F});
        charMap['M'] = bits({B,C,E,F,H,I});                       // top diagonals
        charMap['N'] = bits({B,C,E,F,H,K});                       // diagonals
        charMap['O'] = bits({A,B,C,D,E,F});
        charMap['P'] = bits({A,B,E,F,G1,G2});
        charMap['Q'] = bits({A,B,C,D,E,F,K});                     // tail
        charMap['R'] = bits({A,B,E,F,G1,G2,K});                   // P + tail
        charMap['S'] = bits({A,C,D,F,G1,G2});
        charMap['T'] = bits({A,L,M});
        charMap['U'] = bits({B,C,D,E,F});
        charMap['V'] = bits({H,K,C,B});                           // lower diagonals
        charMap['W'] = bits({B,C,E,F,J,K});                       // bottom diagonals
        charMap['X'] = bits({H,I,J,K});                           // diagonals
        charMap['Y'] = bits({H,I,M});                             // upper diagonals + lower center
        charMap['Z'] = bits({A,D,I,J});                           // diagonals

        // ---------- Lowercase a–z ----------
        // Default policy: map lowercase to uppercase for maximum legibility.
        // Override any lowercase you want to look distinct using setCharMask('a', ...).
        for (char c='a'; c<='z'; ++c)
            charMap[static_cast<unsigned char>(c)] =
                charMap[static_cast<unsigned char>(c - 'a' + 'A')];
    }

    void buildSegmentRects()
    {
        const float cellW = params.cellW;
        const float cellH = params.cellH;

        const float t = params.thickness;
        const float in = params.inset;
        const float g = params.gap;

        const float left   = in;
        const float right  = cellW - in;
        const float top    = cellH - in;
        const float bottom = in;

        const float midY   = cellH * 0.5f;
        const float midX   = cellW * 0.5f;

        auto rect = [](float x0,float y0,float x1,float y1)->Quad {
            return Quad{ {x0,y0},{x1,y0},{x1,y1},{x0,y1} };
        };

        segmentQuads[A]  = rect(left + g, top - t, right - g, top);
        segmentQuads[D]  = rect(left + g, bottom, right - g, bottom + t);
        segmentQuads[G1] = rect(left + g, midY - t*0.5f, midX - g, midY + t*0.5f);
        segmentQuads[G2] = rect(midX + g, midY - t*0.5f, right - g, midY + t*0.5f);

        segmentQuads[F]  = rect(left, midY + g, left + t, top - g);
        segmentQuads[E]  = rect(left, bottom + g, left + t, midY - g);
        segmentQuads[B]  = rect(right - t, midY + g, right, top - g);
        segmentQuads[C]  = rect(right - t, bottom + g, right, midY - g);

        segmentQuads[L]  = rect(midX - t*0.5f, midY + g, midX + t*0.5f, top - g);
        segmentQuads[M]  = rect(midX - t*0.5f, bottom + g, midX + t*0.5f, midY - g);

        auto diag = [&](Vec2 a, Vec2 b) -> Quad
        {
            const float dx = b.x - a.x;
            const float dy = b.y - a.y;
            const float len = std::sqrt(dx*dx + dy*dy);
            const float ux = (len > 1e-6f) ? dx/len : 1.0f;
            const float uy = (len > 1e-6f) ? dy/len : 0.0f;

            const float px = -uy;
            const float py =  ux;

            const float half = t * 0.45f;            

            Vec2 p0{ a.x + px*half, a.y + py*half };
            Vec2 p1{ b.x + px*half, b.y + py*half };
            Vec2 p2{ b.x - px*half, b.y - py*half };
            Vec2 p3{ a.x - px*half, a.y - py*half };
            const float cross = (p1.x - p0.x) * (p2.y - p0.y) - (p1.y - p0.y) * (p2.x - p0.x);
            if (cross < 0.0f)
                std::swap(p1, p3);
            return Quad{ p0,p1,p2,p3 };
        };

        // const float diagInsetX = in + t*0.225f;
        // const float diagInsetY = in + t*0.225f;
        
        const float diagInsetX = 0.0f;
        const float diagInsetY = 0.0f;
        

        const float diagGap = g * 0.3f;

        segmentQuads[H] = diag({ left + diagInsetX,  top - diagInsetY - t },
                               { midX - diagGap,     midY + diagGap });

        segmentQuads[I] = diag({ right - diagInsetX, top - diagInsetY - t },
                               { midX + diagGap,     midY + diagGap });

        segmentQuads[J] = diag({ left + diagInsetX,  bottom + diagInsetY + t },
                               { midX - diagGap,     midY - diagGap });

        segmentQuads[K] = diag({ right - diagInsetX, bottom + diagInsetY + t },
                               { midX + diagGap,     midY - diagGap });

        const float dp = t * 0.9f;
segmentQuads[DP] = rect(right - dp, bottom, right, bottom + dp);
    }
};
