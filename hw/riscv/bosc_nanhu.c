/*
 * QEMU RISC-V Board Compatible with BOSC Xiangshan Nanhu V3a platform
 *
 * Copyright (c) 2025 Beijing Institute of Open Source Chip (BOSC)
 *
 * Provides a board compatible with the Nanhu V3a board: 2 harts, 8GB RAM,
 * 
 * Device:
 * 0) 16550a UART
 * 1) CLINT
 * 2) Sifive PLIC
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
#include "hw/char/serial-mm.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qom/object.h"
#include "hw/irq.h"

#define TYPE_BOSC_NANHU_IRQGEN "bosc-nanhu-irqgen"
OBJECT_DECLARE_SIMPLE_TYPE(BOSCNanhuIrqGenState, BOSC_NANHU_IRQGEN)

typedef struct BOSCNanhuIrqGenState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;
    uint32_t pulse_count;
    uint32_t last_value;
} BOSCNanhuIrqGenState;

enum {
    IRQGEN_REG_TRIGGER = 0x0,
    IRQGEN_REG_COUNT = 0x4,
    IRQGEN_REG_LAST = 0x8,
};

static uint64_t bosc_nanhu_irqgen_read(void *opaque, hwaddr offset,
                                       unsigned size)
{
    BOSCNanhuIrqGenState *s = opaque;

    switch (offset) {
    case IRQGEN_REG_TRIGGER:
        return 0;
    case IRQGEN_REG_COUNT:
        return s->pulse_count;
    case IRQGEN_REG_LAST:
        return s->last_value;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid read offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        return 0;
    }
}

static void bosc_nanhu_irqgen_write(void *opaque, hwaddr offset,
                                    uint64_t value, unsigned size)
{
    BOSCNanhuIrqGenState *s = opaque;

    switch (offset) {
    case IRQGEN_REG_TRIGGER:
        s->last_value = value;
        if (value & 0x1) {
            s->pulse_count++;
            qemu_set_irq(s->irq, 1);
            qemu_set_irq(s->irq, 0);
        }
        break;
    case IRQGEN_REG_COUNT:
        if (value == 0)
            s->pulse_count = 0;
        break;
    case IRQGEN_REG_LAST:
        s->last_value = value;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid write offset 0x%" HWADDR_PRIx
                      " value=0x%" PRIx64 "\n",
                      __func__, offset, value);
    }
}

static const MemoryRegionOps bosc_nanhu_irqgen_ops = {
    .read = bosc_nanhu_irqgen_read,
    .write = bosc_nanhu_irqgen_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void bosc_nanhu_irqgen_init(Object *obj)
{
    BOSCNanhuIrqGenState *s = BOSC_NANHU_IRQGEN(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &bosc_nanhu_irqgen_ops, s,
                          TYPE_BOSC_NANHU_IRQGEN, 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static const TypeInfo bosc_nanhu_irqgen_info = {
    .name          = TYPE_BOSC_NANHU_IRQGEN,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BOSCNanhuIrqGenState),
    .instance_init = bosc_nanhu_irqgen_init,
};

static void bosc_nanhu_irqgen_register_types(void)
{
    type_register_static(&bosc_nanhu_irqgen_info);
}

type_init(bosc_nanhu_irqgen_register_types)

static const MemMapEntry nanhu_memmap[] = {
    [NANHU_DEV_ROM] = {0x0, 0x40000},
    [NANHU_DEV_UART0] = {0x310B0000, 0x10000},
    [NANHU_DEV_UART1] = {0x60000, 0x10000},
    [NANHU_DEV_IRQGEN_NS] = {0x30000000, 0x1000},
    [NANHU_DEV_IRQGEN_SEC] = {0x30001000, 0x1000},
    [NANHU_DEV_CLINT] = {0x38000000, 0x10000},
    [NANHU_DEV_PLIC] = {0x3C000000, 0x4000000},
    [NANHU_DEV_DRAM] = {0x80000000, 0x0},
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
                   qdev_get_gpio_in(s->plic, UART0_IRQ), 399193,
                   serial_hd(0), DEVICE_LITTLE_ENDIAN);

    /* UART1 */
    serial_mm_init(sys_mem,
                   memmap[NANHU_DEV_UART1].base, 2,
                   qdev_get_gpio_in(s->plic, UART1_IRQ), 399193,
                   serial_hd(1), DEVICE_LITTLE_ENDIAN);

    /* Dedicated non-secure IRQ generator */
    sysbus_create_simple(TYPE_BOSC_NANHU_IRQGEN,
                         memmap[NANHU_DEV_IRQGEN_NS].base,
                         qdev_get_gpio_in(s->plic, IRQGEN_NS_IRQ));

    /* Dedicated secure IRQ generator */
    sysbus_create_simple(TYPE_BOSC_NANHU_IRQGEN,
                         memmap[NANHU_DEV_IRQGEN_SEC].base,
                         qdev_get_gpio_in(s->plic, IRQGEN_SEC_IRQ));

    /* ROM */
    memory_region_init_rom(&s->rom, OBJECT(dev_soc), "riscv.bosc.nanhu.rom", memmap[NANHU_DEV_ROM].size, &error_fatal);
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
    .name = TYPE_RISCV_NANHU_SOC,
    .parent = TYPE_DEVICE,
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
    if (machine->firmware)
    {
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
    mc->max_cpus = 1; // conpate with Nanhu-v3a board
    mc->default_cpu_type = TYPE_RISCV_CPU_BOSC_NANHU;
    mc->default_ram_id = "riscv.bosc.nanhu.ram";
    mc->default_ram_size = 8 * GiB; // conpate with Nanhu-v3a board
}

static const TypeInfo bosc_nanhu_machine_typeinfo = {
    .name = TYPE_RISCV_NANHU_MACHINE,
    .parent = TYPE_MACHINE,
    .class_init = bosc_nanhu_machine_class_init,
    .instance_size = sizeof(NanhuState),
};

static void bosc_nanhu_machine_register_types(void)
{
    type_register_static(&bosc_nanhu_machine_typeinfo);
}

type_init(bosc_nanhu_machine_register_types)
