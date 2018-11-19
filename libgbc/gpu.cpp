#include "gpu.hpp"

#include "machine.hpp"
#include "tiledata.hpp"
#include "sprite.hpp"
#include <cassert>
#include <unistd.h>

namespace gbc
{
  static const int TILES_W = GPU::SCREEN_W / TileData::TILE_W;
  static const int TILES_H = GPU::SCREEN_H / TileData::TILE_H;

  GPU::GPU(Machine& mach) noexcept
    : m_memory(mach.memory), m_io(mach.io)
  {
    this->reset();
  }

  void GPU::reset() noexcept
  {
    m_pixels.resize(SCREEN_W * SCREEN_H);
    this->m_video_offset = 0;
  }

  void GPU::simulate()
  {
    static const int SCANLINE_CYCLES = 456*4;
    static const int OAM_CYCLES      = 80*4;
    static const int VRAM_CYCLES     = 172*4;

    // nothing to do with LCD being off
    if ((io().reg(IO::REG_LCDC) & 0x80) == 0) {
      return;
    }

    auto& vblank   = io().vblank;
    auto& lcd_stat = io().lcd_stat;
    auto& reg_stat = io().reg(IO::REG_STAT);
    auto& reg_ly   = io().reg(IO::REG_LY);

    const uint64_t t = io().machine().now();
    const uint64_t period = t - lcd_stat.last_time;

    // scanline logic when screen on
    if (t >= lcd_stat.last_time + SCANLINE_CYCLES)
    {
      lcd_stat.last_time = t;
      // scanline LY increment logic
      static const int MAX_LINES = 154;
      reg_ly = (reg_ly + 1) % MAX_LINES;
      this->m_current_scanline = reg_ly;
      //printf("LY is now %#x\n", this->m_current_scanline);

      if (reg_ly == 0) {
        // start of new frame
      }
      else if (reg_ly == 144)
      {
        assert(this->is_vblank());
        // vblank interrupt
        io().trigger(vblank);
        // modify stat
        reg_stat &= 0xfc;
        reg_stat |= 0x1;
        // if STAT vblank interrupt is enabled
        if (reg_stat & 0x10) io().trigger(lcd_stat);
      }
    }
    // STAT coincidence bit
    if (reg_ly == io().reg(IO::REG_LYC)) {
      // STAT interrupt (if enabled)
      if ((reg_stat & 0x4) == 0
        && reg_stat & 0x40) io().trigger(lcd_stat);
      setflag(true, reg_stat, 0x4);
    }
    else {
      setflag(false, reg_stat, 0x4);
    }
    m_current_mode = reg_stat & 0x3;
    // STAT mode & scanline period modulation
    if (!this->is_vblank())
    {
      if (m_current_mode < 2 && period < OAM_CYCLES+VRAM_CYCLES)
      {
        // enable MODE 2: OAM search
        // check if OAM interrupt enabled
        if (reg_stat & 0x20) io().trigger(lcd_stat);
        reg_stat &= 0xfc;
        reg_stat |= 0x2;
      }
      else if (m_current_mode == 2 && period >= OAM_CYCLES)
      {
        // enable MODE 3: Scanline VRAM
        reg_stat &= 0xfc;
        reg_stat |= 0x3;
        // render a scanline
        this->render_scanline(m_current_scanline);
        // TODO: perform HDMA transfers here!
      }
      else if (m_current_mode == 3 && period >= OAM_CYCLES+VRAM_CYCLES)
      {
        // enable MODE 0: H-blank
        if (reg_stat & 0x8) io().trigger(lcd_stat);
        reg_stat &= 0xfc;
        reg_stat |= 0x0;
      }
      //printf("Current mode: %u -> %u period %lu\n",
      //        current_mode(), reg_stat & 0x3, period);
      m_current_mode = reg_stat & 0x3;
    }
  }

  bool GPU::is_vblank() const noexcept {
    return m_current_scanline >= 144;
  }
  bool GPU::is_hblank() const noexcept {
    return m_current_mode == 3;
  }

  void GPU::render_and_vblank()
  {
    for (int y = 0; y < SCREEN_H; y++) {
      this->render_scanline(y);
    }
    // call vblank handler directly
    io().vblank.callback(io().machine(), io().vblank);
  }

  void GPU::render_scanline(int scan_y)
  {
    const uint8_t scroll_y = memory().read8(IO::REG_SCY);
    const uint8_t scroll_x = memory().read8(IO::REG_SCX);
    const uint8_t pal  = memory().read8(IO::REG_BGP);

    // create tiledata object from LCDC register
    auto td = this->create_tiledata();
    // create sprite configuration structure
    auto sprconf = this->sprite_config();
    sprconf.scan_y = scan_y;
    // create list of sprites that are on this scanline
    auto sprites = this->find_sprites(sprconf);

    // render whole scanline
    for (int scan_x = 0; scan_x < SCREEN_W; scan_x++)
    {
      const int sx = (scan_x + scroll_x) % 256;
      const int sy = (scan_y + scroll_y) % 256;
      const int tx = sx / TileData::TILE_W;
      const int ty = sy / TileData::TILE_H;
      // get the tile id
      const int t = td.tile_id(tx, ty);
      // copy the 16-byte tile into buffer
      const int tidx = td.pattern(t, sx & 7, sy & 7);
      uint32_t color = this->colorize(pal, tidx);

      // render sprites within this x
      sprconf.scan_x = scan_x;
      for (const auto* sprite : sprites) {
        const uint8_t idx = sprite->pixel(sprconf);
        if (idx != 0) {
          if (!sprite->behind() || tidx == 0) {
            color = this->colorize(sprconf.palette[sprite->pal()], idx);
          }
        }
      }
      m_pixels.at(scan_y * SCREEN_W + scan_x) = color;
    } // x
  } // render_to(...)

  uint32_t GPU::colorize(const uint8_t pal, const uint8_t idx)
  {
    const uint8_t color = (pal >> (idx*2)) & 0x3;
    // no conversion
    if (m_pixelmode == PM_PALETTE) return color;
    // convert palette to colors
    switch (color) {
    case 0:
        return 0xFFFFFFFF; // white
    case 1:
        return 0xFFA0A0A0; // light-gray
    case 2:
        return 0xFF777777; // gray
    case 3:
        return 0xFF000000; // black
    }
    return 0xFFFF00FF; // magenta = invalid
  }

  uint16_t GPU::bg_tiles() {
    const uint8_t lcdc = memory().read8(IO::REG_LCDC);
    return (lcdc & 0x08) ? 0x9C00 : 0x9800;
  }
  uint16_t GPU::tile_data() {
    const uint8_t lcdc = memory().read8(IO::REG_LCDC);
    return (lcdc & 0x10) ? 0x8000 : 0x8800;
  }

  TileData GPU::create_tiledata()
  {
    const uint8_t lcdc = memory().read8(IO::REG_LCDC);
    const bool is_signed = (lcdc & 0x10) == 0;
    const auto* vram = memory().video_ram_ptr();
    //printf("Background tiles: 0x%04x  Tile data: 0x%04x\n",
    //        bg_tiles(), tile_data());
    const auto* ttile_base = &vram[bg_tiles() - 0x8000];
    const auto* tdata_base = &vram[tile_data() - 0x8000];
    return TileData{ttile_base, tdata_base, is_signed};
  }
  sprite_config_t GPU::sprite_config()
  {
    const uint8_t lcdc = memory().read8(IO::REG_LCDC);
    sprite_config_t config;
    config.patterns = memory().video_ram_ptr();
    config.palette[0] = memory().read8(IO::REG_OBP0);
    config.palette[1] = memory().read8(IO::REG_OBP1);
    config.scan_x = 0;
    config.scan_y = 0;
    config.mode8x16 = lcdc & 0x4;
    return config;
  }

  std::vector<const Sprite*> GPU::find_sprites(const sprite_config_t& config)
  {
    const auto* oam = memory().oam_ram_ptr();
    const Sprite* sprite = (Sprite*) oam;
    const Sprite* sprite_end = sprite + 40;
    std::vector<const Sprite*> results;

    while (sprite < sprite_end) {
      if (sprite->hidden() == false)
      if (sprite->is_within_scanline(config)) results.push_back(sprite);
      sprite++;
    }
    return results;
  }

  std::vector<uint32_t> GPU::dump_background()
  {
    std::vector<uint32_t> data(256 * 256);
    const uint8_t pal = memory().read8(IO::REG_BGP);
    // create tiledata object from LCDC register
    auto td = this->create_tiledata();

    for (int y = 0; y < 256; y++)
    for (int x = 0; x < 256; x++)
    {
      // get the tile id
      const int t = td.tile_id(x >> 3, y >> 3);
      // copy the 16-byte tile into buffer
      const int idx = td.pattern(t, x & 7, y & 7);
      data.at(y * 256 + x) = this->colorize(pal, idx);
    }
    return data;
  }
  std::vector<uint32_t> GPU::dump_tiles()
  {
    std::vector<uint32_t> data(16*24 * 8*8);
    const uint8_t pal = memory().read8(IO::REG_BGP);
    // create tiledata object from LCDC register
    auto td = this->create_tiledata();
    // tiles start at the beginning of video RAM
    td.set_tilebase(memory().video_ram_ptr());

    for (int y = 0; y < 24*8; y++)
    for (int x = 0; x < 16*8; x++)
    {
      int tile = (y / 8) * 16 + (x / 8);
      // copy the 16-byte tile into buffer
      const int idx = td.pattern(tile, x & 7, y & 7);
      data.at(y * 128 + x) = this->colorize(pal, idx);
    }
    return data;
  }

  void GPU::set_video_bank(uint8_t bank)
  {
    this->m_video_offset = bank * 0x2000;
  }
}
