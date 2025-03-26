/*
 * QEMU RISC-V Board Compatible with BOSC Xiangshan Nanhu V3a platform
 *
 * Copyright (c) 2024 Beijing Institute of Open Source Chip (BOSC)
 *
 * Provides a board compatible with the Nanhu V3a:
 *
 * 0) 16550a UART
 * 1) CLINT
 * 2) Sifive PLIC
 *
 * Note: Nanhu V3a only supports 1 hart now
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qemu/units.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "sysemu/device_tree.h"
#include "sysemu/sysemu.h"
#include "target/riscv/cpu.h"
#include "hw/boards.h"
#include "hw/sysbus.h"
#include "hw/misc/unimp.h"
#include "hw/riscv/boot.h"
#include "hw/riscv/bosc_nanhu.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/intc/riscv_aclint.h"
#include "hw/intc/sifive_plic.h"
#include "hw/char/serial.h"

static const MemMapEntry nanhu_memmap[] = {
    [NANHU_DEV_ROM]  =         {        0x0,       0x40000 },
    [NANHU_DEV_UART0] =        {    0x50000,       0x10000 },
    [NANHU_DEV_UART1] =        {    0x60000,       0x10000 },
    [NANHU_DEV_CLINT] =        { 0x38000000,       0x10000 },
    [NANHU_DEV_PLIC]  =        { 0x3C000000,     0x4000000 },
    [NANHU_DEV_DRAM]  =        { 0x80000000,           0x0 },
};

static void bosc_nanhu_soc_realize(DeviceState *dev_soc, Error **errp)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    BOSCNanhuSocState *s = RISCV_NANHU_SOC(dev_soc);
    MemoryRegion *sys_mem = get_system_memory();
    const MemMapEntry *memmap = nanhu_memmap;
    uint32_t num_harts = ms->smp.cpus;

    /* CPU */
    qdev_prop_set_uint32(DEVICE(&s->cpus), "num-harts", num_harts);
    qdev_prop_set_uint32(DEVICE(&s->cpus), "hartid-base", 0);
    qdev_prop_set_string(DEVICE(&s->cpus), "cpu-type",
                         TYPE_RISCV_CPU_BOSC_NANHU);
    sysbus_realize(SYS_BUS_DEVICE(&s->cpus), &error_fatal);

    /* PLIC */
    s->plic = sifive_plic_create(memmap[NANHU_DEV_PLIC].base, (char *)BOSC_NANHU_PLIC_HART_CONFIG,
        num_harts, 
        0, BOSC_NANHU_PLIC_NUM_SOURCES,
        BOSC_NANHU_PLIC_NUM_PRIORITIES, BOSC_NANHU_PLIC_PRIORITY_BASE,
        BOSC_NANHU_PENDING_BASE, BOSC_NANHU_ENABLE_BASE,
        BOSC_NANHU_ENABLE_STRIDE, BOSC_NANHU_CONTEXT_BASE,
        BOSC_NANHU_CONTEXT_STRIDE, memmap[NANHU_DEV_PLIC].size);

    /* CLINT */
    riscv_aclint_swi_create(memmap[NANHU_DEV_CLINT].base, 0, 
        num_harts, false);
    riscv_aclint_mtimer_create(memmap[NANHU_DEV_CLINT].base + RISCV_ACLINT_SWI_SIZE, RISCV_ACLINT_DEFAULT_MTIMER_SIZE,
        0, num_harts,
        RISCV_ACLINT_DEFAULT_MTIMECMP, RISCV_ACLINT_DEFAULT_MTIME, RISCV_ACLINT_DEFAULT_TIMEBASE_FREQ, 
        false);

    /* UART0 */
    serial_mm_init(sys_mem, 
        memmap[NANHU_DEV_UART0].base, 2, 
        qdev_get_gpio_in(DEVICE(s->plic), UART0_IRQ), 399193,
        serial_hd(0), DEVICE_LITTLE_ENDIAN);

    /* UART1 */
    create_unimplemented_device("riscv.bosc.nanhu.uart1",
        memmap[NANHU_DEV_UART1].base, memmap[NANHU_DEV_UART1].size);
    
    /* ROM */
    memory_region_init_rom(&s->rom, OBJECT(dev_soc), "riscv.bosc.nanhu.rom", memmap[NANHU_DEV_MROM].size, &error_fatal);
    memory_region_add_subregion(sys_mem, memmap[NANHU_DEV_ROM].base, &s->rom);
}

static void bosc_nanhu_soc_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    dc->realize = bosc_nanhu_soc_realize;
    dc->user_creatable = false;
}

static void bosc_nanhu_soc_instance_init(Object *obj)
{
    BOSCNanhuSocState *s = RISCV_NANHU_SOC(obj);

    object_initialize_child(obj, "cpus", &s->cpus, TYPE_RISCV_HART_ARRAY);
}

static const TypeInfo bosc_nanhu_soc_typeinfo = {
    .name       = TYPE_RISCV_NANHU_SOC,
    .parent     = TYPE_DEVICE,
    .instance_size = sizeof(BOSCNanhuSocState),
    .instance_init = bosc_nanhu_soc_instance_init,
    .class_init = bosc_nanhu_soc_class_init,
};

static void bosc_nanhu_soc_register_types(void)
{
    type_register_static(&bosc_nanhu_soc_typeinfo);
}

type_init(bosc_nanhu_soc_register_types)


static void nanhu_machine_init(MachineState *machine)
{
    NanhuState *s = RISCV_NANHU_MACHINE(machine);
    const MemMapEntry *memmap = nanhu_memmap;
    MemoryRegion *sys_mem = get_system_memory();
    target_ulong start_addr = memmap[NANHU_DEV_DRAM].base;
    const char *firmware_name;
    target_ulong firmware_end_addr, kernel_start_addr;
    uint64_t kernel_entry, fdt_load_addr;
    
    /* Initialize SoC */
    object_initialize_child(OBJECT(machine), "soc", &s->soc, TYPE_RISCV_NANHU_SOC);
    qdev_realize(DEVICE(&s->soc), NULL, &error_fatal);

    /* register RAM */
    memory_region_add_subregion(sys_mem, memmap[NANHU_DEV_DRAM].base, machine->ram);

    /* ROM reset vector */
    riscv_setup_rom_reset_vec(machine, &s->soc.cpus,
                              start_addr,
                              memmap[NANHU_DEV_ROM].base,
                              memmap[NANHU_DEV_ROM].size, 0, 0);
    if (machine->firmware) {
        riscv_load_firmware(machine->firmware, &start_addr, NULL);
    }

    /* Note: dtb has been integrated into firmware(OpenSBI) when compiling */
}

static void bosc_nanhu_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    
    /* machine properties */
    mc->desc = "RISC-V Board compatible with BOSC Xiangshan Nanhu SoC";
    mc->init = nanhu_machine_init;
    mc->max_cpus = 2;   // conpate with Nanhu-v3a board
    mc->default_cpu_type = TYPE_RISCV_CPU_BOSC_NANHU;
    mc->default_ram_id = "riscv.bosc.nanhu.ram";
    mc->default_ram_size = 8 * GiB; // conpate with Nanhu-v3a board
}

static const TypeInfo bosc_nanhu_machine_typeinfo = {
    .name       = TYPE_RISCV_NANHU_MACHINE,
    .parent     = TYPE_MACHINE,
    .class_init = bosc_nanhu_machine_class_init,
    .instance_size = sizeof(NanhuState),
};

static void bosc_nanhu_machine_register_types(void)
{
    type_register_static(&bosc_nanhu_machine_typeinfo);
}

type_init(bosc_nanhu_machine_register_types)
