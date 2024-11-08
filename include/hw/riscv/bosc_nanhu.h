/*
 * BOSC Xiangshan Nanhu V3a machine interface
 *
 * Copyright (c) 2024 Beijing Institute of Open Source Chip (BOSC)
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

#ifndef HW_BOSC_NH_H
#define HW_BOSC_NH_H

#include "hw/boards.h"
#include "hw/riscv/riscv_hart.h"

#define TYPE_RISCV_NANHU_SOC "riscv.bosc.nanhu.soc"
#define RISCV_NANHU_SOC(obj) \
    OBJECT_CHECK(BOSCNanhuSocState, (obj), TYPE_RISCV_NANHU_SOC)

typedef struct BOSCNanhuSocState {
    /*< private >*/
    DeviceState parent_obj;

    /*< public >*/
    RISCVHartArrayState cpus;
    DeviceState *plic;
    MemoryRegion mask_rom;
} BOSCNanhuSocState;

typedef struct NanhuState {
    /*< private >*/
    MachineState parent_obj;
    
    /*< public >*/
    BOSCNanhuSocState soc;
    int fdt_size;
} NanhuState;

#define TYPE_RISCV_NANHU_MACHINE MACHINE_TYPE_NAME("bosc-nanhu")
#define RISCV_NANHU_MACHINE(obj) \
    OBJECT_CHECK(NanhuState, (obj), TYPE_RISCV_NANHU_MACHINE)

enum {
    NANHU_DEV_MROM,
    NANHU_DEV_UART0,
    NANHU_DEV_CLINT,
    NANHU_DEV_PLIC,
    NANHU_DEV_DRAM,
};

enum {
    UART0_IRQ = 40,
};

#define BOSC_NANHU_PLIC_HART_CONFIG "MS"
#define BOSC_NANHU_PLIC_NUM_SOURCES 96
#define BOSC_NANHU_PLIC_NUM_PRIORITIES 7
#define BOSC_NANHU_PLIC_PRIORITY_BASE 0x00
#define BOSC_NANHU_PENDING_BASE 0x1000
#define BOSC_NANHU_ENABLE_BASE 0x2000
#define BOSC_NANHU_ENABLE_STRIDE 0x80
#define BOSC_NANHU_CONTEXT_BASE 0x200000
#define BOSC_NANHU_CONTEXT_STRIDE 0x1000

#endif
