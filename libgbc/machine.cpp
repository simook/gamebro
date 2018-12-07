#include "machine.hpp"

namespace gbc
{
  Machine::Machine(std::vector<uint8_t> rom, bool init)
      : cpu(*this), memory(*this, std::move(rom)),
        io(*this), gpu(*this), apu(*this)
  {
    // set CGB mode when ROM supports it
    const uint8_t cgb = memory.read8(0x143);
    this->m_cgb_mode = (cgb & 0x80) && ENABLE_GBC;
    // reset CPU now that we know the machine type
    if (init) this->cpu.reset();
  }

  void Machine::reset()
  {
    cpu.reset();
    memory.reset();
    io.reset();
    gpu.reset();
  }
  void Machine::stop() noexcept
  {
    this->m_running = false;
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
