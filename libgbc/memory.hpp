#pragma once
#include <array>
#include <cstdint>
#include <stdexcept>

namespace gbc
{
  class Machine;

  class Memory
  {
  public:
    using range_t = std::pair<uint16_t, uint16_t>;
    static constexpr range_t ProgramArea {0x0000, 0x7FFF};
    static constexpr range_t Display_Chr {0x8000, 0x97FF};
    static constexpr range_t Display_BG1 {0x9800, 0x9BFF};
    static constexpr range_t Display_BG2 {0x9C00, 0x9FFF};

    static constexpr range_t WorkRAM     {0xC000, 0xDFFF};
    static constexpr range_t OAM_RAM     {0xFE00, 0xFEFF};
    static constexpr range_t ZRAM        {0xFF80, 0xFFFF};

    Memory(Machine&);

    uint8_t read8(uint16_t address);
    void    write8(uint16_t address, uint8_t value);

    uint16_t read16(uint16_t address);
    void     write16(uint16_t address, uint16_t value);

    static constexpr uint16_t range_size(range_t range) { return range.second - range.first; }

    // for installing BIOS and program
    auto& program_area() noexcept { return m_program_area; }

  private:
    inline bool is_within(uint16_t addr, const range_t& range) const
    {
      return addr >= range.first && addr <= range.second;
    }

    Machine& m_machine;
    std::array<uint8_t, 32768> m_program_area;
    std::array<uint8_t, 8192>  m_work_ram;
    std::array<uint8_t, 256>   m_oam_ram;
    std::array<uint8_t, 128>   m_zram; // high-speed RAM
  };
}
