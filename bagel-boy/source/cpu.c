#include "cpu.h"

#include "bus.h"
#include "cartridge.h"
#include "instructions.h"
#include "instruction_sets.h"
#include "interrupt_controller.h"
#include "io_registers.h"
#include "utilities.h"

#include <memory.h>
#include <stdio.h>
#include <stdlib.h>

#include <intrin.h>

#define SET_BREAKPOINT(condition) if(condition) { __debugbreak(); }

const uint8_t boot_rom[256] =
{
	0x31, 0xFE, 0xFF, 0xAF, 0x21, 0xFF, 0x9F, 0x32, 0xCB, 0x7C, 0x20, 0xFB, 0x21, 0x26, 0xFF, 0x0E,
	0x11, 0x3E, 0x80, 0x32, 0xE2, 0x0C, 0x3E, 0xF3, 0xE2, 0x32, 0x3E, 0x77, 0x77, 0x3E, 0xFC, 0xE0,
	0x47, 0x11, 0x04, 0x01, 0x21, 0x10, 0x80, 0x1A, 0xCD, 0x95, 0x00, 0xCD, 0x96, 0x00, 0x13, 0x7B,
	0xFE, 0x34, 0x20, 0xF3, 0x11, 0xD8, 0x00, 0x06, 0x08, 0x1A, 0x13, 0x22, 0x23, 0x05, 0x20, 0xF9,
	0x3E, 0x19, 0xEA, 0x10, 0x99, 0x21, 0x2F, 0x99, 0x0E, 0x0C, 0x3D, 0x28, 0x08, 0x32, 0x0D, 0x20,
	0xF9, 0x2E, 0x0F, 0x18, 0xF3, 0x67, 0x3E, 0x64, 0x57, 0xE0, 0x42, 0x3E, 0x91, 0xE0, 0x40, 0x04,
	0x1E, 0x02, 0x0E, 0x0C, 0xF0, 0x44, 0xFE, 0x90, 0x20, 0xFA, 0x0D, 0x20, 0xF7, 0x1D, 0x20, 0xF2,
	0x0E, 0x13, 0x24, 0x7C, 0x1E, 0x83, 0xFE, 0x62, 0x28, 0x06, 0x1E, 0xC1, 0xFE, 0x64, 0x20, 0x06,
	0x7B, 0xE2, 0x0C, 0x3E, 0x87, 0xE2, 0xF0, 0x42, 0x90, 0xE0, 0x42, 0x15, 0x20, 0xD2, 0x05, 0x20,
	0x4F, 0x16, 0x20, 0x18, 0xCB, 0x4F, 0x06, 0x04, 0xC5, 0xCB, 0x11, 0x17, 0xC1, 0xCB, 0x11, 0x17,
	0x05, 0x20, 0xF5, 0x22, 0x23, 0x22, 0x23, 0xC9, 0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B,
	0x03, 0x73, 0x00, 0x83, 0x00, 0x0C, 0x00, 0x0D, 0x00, 0x08, 0x11, 0x1F, 0x88, 0x89, 0x00, 0x0E,
	0xDC, 0xCC, 0x6E, 0xE6, 0xDD, 0xDD, 0xD9, 0x99, 0xBB, 0xBB, 0x67, 0x63, 0x6E, 0x0E, 0xEC, 0xCC,
	0xDD, 0xDC, 0x99, 0x9F, 0xBB, 0xB9, 0x33, 0x3E, 0x3C, 0x42, 0xB9, 0xA5, 0xB9, 0xA5, 0x42, 0x3C,
	0x21, 0x04, 0x01, 0x11, 0xA8, 0x00, 0x1A, 0x13, 0xBE, 0x20, 0xFE, 0x23, 0x7D, 0xFE, 0x34, 0x20,
	0xF5, 0x06, 0x19, 0x78, 0x86, 0x23, 0x05, 0x20, 0xFB, 0x86, 0x20, 0xFE, 0x3E, 0x01, 0xE0, 0x50
};

void cpu_initialize(struct cpu* cpu, struct interrupt_controller* interrupt_controller, struct bus* bus)
{
    memset(cpu, 0, sizeof(struct cpu));

    cpu->interrupt_controller = interrupt_controller;
    cpu->bus = bus;
}

void cpu_destroy(struct cpu* cpu)
{

}

void cpu_cycle(struct cpu* cpu, const uint64_t* clock_cycles)
{
	const struct instruction* instruction = &instruction_set[bus_read(cpu->bus, cpu->pc)];

	static uint8_t current_instruction_clock_cycles = 0;

	if (++current_instruction_clock_cycles >= instruction->duration)
	{
		uint16_t operand = 0;

		switch (instruction->length)
		{
			case 1:
				break;
			case 2:
				operand = bus_read(cpu->bus, cpu->pc + 1);
				break;
			case 3:
				operand = MAKE_SHORT(bus_read(cpu->bus, cpu->pc + 2), bus_read(cpu->bus, cpu->pc + 1));
				break;
		}

		instruction->instruction(cpu, operand);

        if (instruction->syntax != "NOP")
        {
            printf(instruction->syntax, operand);
            printf("\n");
        }

		if (instruction->update_pc)
		{
			cpu->pc += instruction->length;
		}

		current_instruction_clock_cycles = 0;
	}
}

void cpu_handle_interrupts(struct cpu* cpu)
{
	if (cpu->ime)
	{
		uint8_t io_register_if = cpu->interrupt_controller->interrupt_flags;
		uint8_t io_register_ie = cpu->interrupt_controller->interrupt_enable;

		if (!io_register_if || !io_register_ie)
		{
			return;
		}

		bus_write(cpu->bus, cpu->sp - 1, HIGH_BYTE(cpu->pc));
		bus_write(cpu->bus, cpu->sp - 2, LOW_BYTE(cpu->pc));

		cpu->sp -= 2;

		cpu->ime = 0x00;

		if (cpu->halted)
		{
			cpu->halted = false;
		}

		if ((io_register_if & io_register_ie) & INTERRUPT_FLAG_V_BLANK)
		{
			cpu->pc = INTERRUPT_V_BLANK_ADDRESS;
		}
		else if ((io_register_if & io_register_ie) & INTERRUPT_FLAG_LCDC_STAT)
		{
			cpu->pc = INTERRUPT_LCDC_STAT_ADDRESS;
		}
		else if ((io_register_if & io_register_ie) & INTERRUPT_FLAG_TIMER)
		{
			cpu->pc = INTERRUPT_TIMER_ADDRESS;
		}
		else if ((io_register_if & io_register_ie) & INTERRUPT_FLAG_SERIAL)
		{
			cpu->pc = INTERRUPT_SERIAL_ADDRESS;
		}
		else if ((io_register_if & io_register_ie) & INTERRUPT_FLAG_JOYPAD)
		{
			cpu->pc = INTERRUPT_JOYPAD_ADDRESS;
		}

        cpu->interrupt_controller->interrupt_flags = 0x00;
	}
}