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
#include "hw/riscv/boot.h"
#include "hw/riscv/bosc_nanhu.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/intc/riscv_aclint.h"
#include "hw/intc/sifive_plic.h"
#include "hw/char/serial.h"

static const MemMapEntry nanhu_memmap[] = {
    [NANHU_DEV_MROM]  =        {     0x1000,        0xf000 },
    [NANHU_DEV_UART0] =        { 0x310B0000,       0x10000 },
    [NANHU_DEV_CLINT] =        { 0x38000000,       0x10000 },
    [NANHU_DEV_PLIC]  =        { 0x3c000000,      0x400000 },
    [NANHU_DEV_DRAM]  =        { 0x80000000,           0x0 },
};

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

    /* load device tree */
    if (machine->dtb)
    {
        machine->fdt = load_device_tree(machine->dtb, &s->fdt_size);
        if (!machine->fdt) 
        {
            error_report("load_device_tree() failed");
            exit(EXIT_FAILURE);
        }
    } 
    else 
    {
        error_report("must provide a device tree using -dtb");
        exit(EXIT_FAILURE);
    }
    
    /* load the firmware */
    if (machine->firmware) 
    {
        firmware_name = riscv_default_firmware_name(&s->soc.cpus);
        firmware_end_addr = riscv_find_and_load_firmware(machine, firmware_name, start_addr, NULL);
    }
    
    /* load the kernel */
    if (machine->kernel_filename)
    {
        kernel_start_addr = riscv_calc_kernel_start_addr(&s->soc.cpus, firmware_end_addr);
        kernel_entry = riscv_load_kernel(machine, &s->soc.cpus,
                                         kernel_start_addr, true, NULL);
    } 
    else 
    {
       /*
        * If dynamic firmware is used, it doesn't know where is the next mode
        * if kernel argument is not set.
        */
        kernel_entry = 0;
    }

    /* load fdt */
    fdt_load_addr = riscv_compute_fdt_addr(memmap[NANHU_DEV_DRAM].base,
                                           memmap[NANHU_DEV_DRAM].size,
                                           machine);
    riscv_load_fdt(fdt_load_addr, machine->fdt);

    /* load the reset vector */
    riscv_setup_rom_reset_vec(machine, &s->soc.cpus, 
                              start_addr,
                              memmap[NANHU_DEV_MROM].base, memmap[NANHU_DEV_MROM].size,
                              kernel_entry,
                              fdt_load_addr); 
}

static void bosc_nanhu_soc_realize(DeviceState *dev_soc, Error **errp)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    BOSCNanhuSocState *s = RISCV_NANHU_SOC(dev_soc);
    MemoryRegion *sys_mem = get_system_memory();
    const MemMapEntry *memmap = nanhu_memmap;

    /* CPU */
    object_property_set_str(OBJECT(&s->cpus), "cpu-type", ms->cpu_type, &error_abort);
    object_property_set_int(OBJECT(&s->cpus), "num-harts", ms->smp.cpus, &error_abort);
    object_property_set_int(OBJECT(&s->cpus), "resetvec", 0x1000, &error_abort);
    sysbus_realize(SYS_BUS_DEVICE(&s->cpus), &error_fatal);

    /* Mask ROM */
    memory_region_init_rom(&s->mask_rom, OBJECT(dev_soc), "riscv.bosc.nanhu.rom", memmap[NANHU_DEV_MROM].size, &error_fatal);
    memory_region_add_subregion(sys_mem, memmap[NANHU_DEV_MROM].base, &s->mask_rom);

    /* PLIC */
    s->plic = sifive_plic_create(memmap[NANHU_DEV_PLIC].base,
        (char *)BOSC_NANHU_PLIC_HART_CONFIG,
        ms->smp.cpus, 0,
        BOSC_NANHU_PLIC_NUM_SOURCES,
        BOSC_NANHU_PLIC_NUM_PRIORITIES,
        BOSC_NANHU_PLIC_PRIORITY_BASE,
        BOSC_NANHU_PENDING_BASE,
        BOSC_NANHU_ENABLE_BASE,
        BOSC_NANHU_ENABLE_STRIDE,
        BOSC_NANHU_CONTEXT_BASE,
        BOSC_NANHU_CONTEXT_STRIDE,
        memmap[NANHU_DEV_PLIC].size);

    /* CLINT */
    riscv_aclint_swi_create(memmap[NANHU_DEV_CLINT].base,
        0, ms->smp.cpus, 
        false);
    riscv_aclint_mtimer_create(memmap[NANHU_DEV_CLINT].base + RISCV_ACLINT_SWI_SIZE,
        RISCV_ACLINT_DEFAULT_MTIMER_SIZE,
        0, ms->smp.cpus,
        RISCV_ACLINT_DEFAULT_MTIMECMP, 
        RISCV_ACLINT_DEFAULT_MTIME,
        RISCV_ACLINT_DEFAULT_TIMEBASE_FREQ, 
        false);

    /* UART */
    serial_mm_init(sys_mem, 
        memmap[NANHU_DEV_UART0].base, 2, 
        qdev_get_gpio_in(DEVICE(s->plic), UART0_IRQ), 399193,
        serial_hd(0), DEVICE_LITTLE_ENDIAN);
}

static void bosc_nanhu_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    
    /* machine properties */
    mc->desc = "RISC-V Board compatible with BOSC Xiangshan Nanhu SoC";
    mc->init = nanhu_machine_init;
    mc->max_cpus = 1;   // only support 1 hart now
    mc->default_cpu_type = TYPE_RISCV_CPU_BOSC_NANHU;
    mc->default_ram_id = "riscv.bosc.nanhu.ram";
    mc->default_ram_size = 1 * GiB;
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

static void bosc_nanhu_soc_init(Object *obj)
{
    BOSCNanhuSocState *s = RISCV_NANHU_SOC(obj);
    object_initialize_child(obj, "cpus", &s->cpus, TYPE_RISCV_HART_ARRAY);
}

static void bosc_nanhu_soc_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    dc->realize = bosc_nanhu_soc_realize;
    /* Reason: Uses serial_hds in realize function, thus can't be used twice */
    dc->user_creatable = false;
}

static const TypeInfo bosc_nanhu_soc_typeinfo = {
    .name       = TYPE_RISCV_NANHU_SOC,
    .parent     = TYPE_DEVICE,
    .class_init = bosc_nanhu_soc_class_init,
    .instance_init = bosc_nanhu_soc_init,
    .instance_size = sizeof(BOSCNanhuSocState),
};

static void bosc_nanhu_soc_register_types(void)
{
    type_register_static(&bosc_nanhu_soc_typeinfo);
}

type_init(bosc_nanhu_soc_register_types)
