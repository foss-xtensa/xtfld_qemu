#ifndef _HW_XTENSA_XTSC_H
#define _HW_XTENSA_XTSC_H

#define TYPE_XTENSA_XTSC     "xtensa_xtsc"
#define XTENSA_XTSC_DEV_ID   "xtensa_xtsc"
#define XTENSA_XTSC_ADDR_PROP "addr"
#define XTENSA_XTSC_SIZE_PROP "size"
#define XTENSA_XTSC_NAME_PROP "name"
#define XTENSA_XTSC_COMM_ADDR_PROP "comm_addr"
#define XTENSA_XTSC_RESERVED_SIZE_PROP "reserved_size"
#define XTENSA_XTSC_DSP_IRQ_ADDR_PROP "dsp_irq_addr"
#define XTENSA_XTSC_DSP_IRQ_PROP "dsp_irq"

struct Object;

Object *xtensa_xtsc_device(void);
Object **xtensa_xtsc_devices(void);
hwaddr xtensa_xtsc_get_addr(Object *obj, int i);
uint64_t xtensa_xtsc_get_size(Object *obj, int i);

#endif /* _HW_XTENSA_XTSC_H */
