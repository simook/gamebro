#include "machine.hpp"
#include "bios.hpp"

namespace gbc
{
  Machine::Machine(std::vector<uint8_t> rom)
      : cpu(*this), memory(*this, rom), io(*this), gpu(*this)
  {
    printf("Game cartridge type: 0x%02x", memory.read8(0x147));
  }

  void Machine::reset()
  {
    cpu.reset();
    memory.reset();
    io.reset();
    gpu.reset();
  }

  uint64_t Machine::now() noexcept
  {
    return cpu.gettime();
  }

  void Machine::set_handler(interrupt i, interrupt_handler handler)
  {
    switch (i) {
      case VBLANK:
          io.vblank.callback = handler;
          return;
      case TIMER:
          io.timerint.callback = handler;
          return;
      case DEBUG:
          io.debugint.callback = handler;
          return;
    }
  }

  void Machine::set_inputs(uint8_t mask)
  {
    io.trigger_keys(mask);
  }

  void Machine::break_now()
  {
    cpu.break_now();
  }
  bool Machine::is_breaking() const noexcept
  {
    return cpu.is_breaking();
  }

  void Machine::undefined()
  {
    if (this->stop_when_undefined) {
      printf("*** An undefined operation happened\n");
      cpu.break_now();
    }
  }
}
