#pragma once
#include "memory.hpp"

namespace gbc
{
  class TileData
  {
  public:
    static const int TILE_W = 8;
    static const int TILE_H = 8;

    TileData(const uint8_t* tile, const uint8_t* pattern,
             const uint8_t* attr, bool sign, bool cgb)
      : m_tile_base(tile), m_patt_base(pattern), m_attr_base(attr),
        m_signed(sign), m_is_cgb(cgb) {}

    int   tile_id(int tx, int ty);
    int   tile_attr(int tx, int ty);
    int   pattern(int t, int tattr, int dx, int dy) const;
    int   pattern(const uint8_t* base, int tattr, int t, int dx, int dy) const;
    void  set_tilebase(const uint8_t* new_base) { m_tile_base = new_base; }

  private:
    const uint8_t* m_tile_base;
    const uint8_t* m_patt_base;
    const uint8_t* m_attr_base;
    const bool m_signed;
    const bool m_is_cgb;
  };

  inline int TileData::tile_id(int x, int y)
  {
    if (this->m_signed)
        return 128 + (int8_t) m_tile_base[y * 32 + x];
    return m_tile_base[y * 32 + x];
  }
  inline int TileData::tile_attr(int x, int y)
  {
    if (m_is_cgb == false) return 0;
    return m_attr_base[y * 32 + x];
  }

  inline int TileData::pattern(const uint8_t* base, int tid, int tattr,
                               int tx, int ty) const
  {
    assert(tx >= 0 && tx < 8);
    assert(ty >= 0 && ty < 8);
    if (this->m_is_cgb) {
      if (tattr & 0x20) tx = 7 - tx;
      if (tattr & 0x40) ty = 7 - ty;
      if (tattr & 0x08) base += 0x2000;
    }
    const int offset = 16*tid + ty * 2;
    //printf("Offset: 16*%d + %d*2 = %d\n", tid, ty, offset);
    // get 16-bit c0, c1
    uint8_t c0 = base[offset];
    uint8_t c1 = base[offset + 1];
    // return combined 4-bits, right to left
    const int bit = 7 - tx;
    const int v0 = (c0 >> bit) & 0x1;
    const int v1 = (c1 >> bit) & 0x1;
    return v0 | (v1 << 1);
  } // pattern(...)
  inline int TileData::pattern(int tid, int tattr, int tx, int ty) const
  {
    return pattern(m_patt_base, tid, tattr, tx, ty);
  }
}
