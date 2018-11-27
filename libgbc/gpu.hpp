#pragma once
#include "common.hpp"
#include "tiledata.hpp"
#include "sprite.hpp"
#include <cstdint>
#include <vector>

namespace gbc
{
  enum pixelmode_t {
    PM_RGBA = 0,     // regular 32-bit RGBA
    PM_PALETTE = 1,  // no conversion
  };
  class GPU
  {
  public:
    static const int SCREEN_W = 160;
    static const int SCREEN_H = 144;

    GPU(Machine&) noexcept;
    void reset() noexcept;
    void simulate();
    void set_pixelmode(pixelmode_t);
    // the vector is resized to exactly fit the screen
    const auto& pixels() const noexcept { return m_pixels; }

    void render_and_vblank();
    bool is_vblank() const noexcept;
    bool is_hblank() const noexcept;

    void    set_mode(uint8_t mode);
    uint8_t get_mode() const noexcept;

    uint16_t video_offset() const noexcept { return m_video_offset; }
    bool     video_writable() noexcept;
    void     set_video_bank(uint8_t bank);
    void     lcd_power_changed(bool state);

    bool lcd_enabled() const noexcept;
    bool window_enabled() const noexcept;
    std::pair<int, int> window_size();
    bool window_visible();
    int  window_x();
    int  window_y();

    // CGB palette registers
    enum pal_t { PAL_BG, PAL_SPR };
    uint8_t& getpal(pal_t pal, uint8_t index);

    Machine& machine() noexcept { return m_memory.machine(); }
    Memory&  memory() noexcept { return m_memory; }
    IO&      io() noexcept { return m_io; }
    std::vector<uint32_t> dump_background();
    std::vector<uint32_t> dump_window();
    std::vector<uint32_t> dump_tiles();

  private:
    uint64_t scanline_cycles();
    uint64_t oam_cycles();
    uint64_t vram_cycles();
    void render_scanline(int y);
    void do_ly_comparison();
    TileData create_tiledata(uint16_t tiles, uint16_t patt);
    sprite_config_t sprite_config();
    std::vector<const Sprite*> find_sprites(const sprite_config_t&);
    uint32_t colorize(uint8_t pal, uint8_t);
    // addresses
    uint16_t bg_tiles() const noexcept;
    uint16_t window_tiles() const noexcept;
    uint16_t tile_data() const noexcept;
    // palettes
    uint8_t tile_palette() const noexcept;
    uint8_t sprite_palette() const noexcept;

    std::vector<uint32_t> m_pixels;
    Memory& m_memory;
    IO& m_io;
    uint8_t&    m_reg_lcdc;
    uint8_t&    m_reg_stat;
    uint8_t&    m_reg_ly;
    pixelmode_t m_pixelmode = PM_RGBA;
    int m_current_scanline = 0;
    uint16_t m_video_offset = 0x0;

    std::array<uint8_t, 64> m_bg_palette;
    std::array<uint8_t, 8> m_spr_palette;
  };

  inline void GPU::set_pixelmode(pixelmode_t pm) {
    this->m_pixelmode = pm;
  }
}
