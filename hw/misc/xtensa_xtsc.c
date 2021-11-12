#include "qemu/osdep.h"
#include "qom/object_interfaces.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/misc/xtensa_xtsc.h"
#include "hw/xtsc.h"
#include "exec/address-spaces.h"
#include "qemu/error-report.h"

#define XTENSA_XTSC(obj) OBJECT_CHECK(xtensa_xtsc, (obj), TYPE_XTENSA_XTSC)

typedef struct shared_memory {
    int ref;
    void *ram_ptr;
    MemoryRegion memory;
    hwaddr addr;
    uint64_t size;
    uint64_t reserved_size;
    char *name;
} SharedMemory;

/**
  A xtensa pstore is currently an area of reservered memory that survives reboots. The memory
  is persisted to disk if the filename parameter has been given. We persist the memory region
  when the device is unrealized.
*/
typedef struct xtensa_xtsc {
    DeviceState parent;
    SharedMemory *shmem;
    hwaddr addr;
    uint64_t size;
    char *name;
    hwaddr comm_addr; /* communication area address */
    uint64_t reserved_size;
    hwaddr dsp_irq_addr;
    uint32_t dsp_irq;
} xtensa_xtsc;

static Property xtensa_xtsc_properties[] = {
    DEFINE_PROP_UINT64(XTENSA_XTSC_ADDR_PROP, xtensa_xtsc, addr, 0),
    DEFINE_PROP_SIZE(XTENSA_XTSC_SIZE_PROP, xtensa_xtsc, size, 0),
    DEFINE_PROP_STRING(XTENSA_XTSC_NAME_PROP, xtensa_xtsc, name),
    DEFINE_PROP_UINT64(XTENSA_XTSC_COMM_ADDR_PROP, xtensa_xtsc, comm_addr, 0),
    DEFINE_PROP_SIZE(XTENSA_XTSC_RESERVED_SIZE_PROP, xtensa_xtsc, reserved_size, 0),
    DEFINE_PROP_UINT64(XTENSA_XTSC_DSP_IRQ_ADDR_PROP, xtensa_xtsc, dsp_irq_addr, 0),
    DEFINE_PROP_UINT32(XTENSA_XTSC_DSP_IRQ_PROP, xtensa_xtsc, dsp_irq, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static SharedMemory mem[64];
static Object *obj[64];

Object *xtensa_xtsc_device(void)
{
    return obj[0];
}

Object **xtensa_xtsc_devices(void)
{
    return obj;
}

hwaddr xtensa_xtsc_get_addr(Object *obj, int i)
{ 
    xtensa_xtsc *s = XTENSA_XTSC(obj);

    switch (i) {
    case 0:
        return s->addr;
    case 1:
        return s->comm_addr;
    case 2:
        return s->dsp_irq_addr;
    default:
        g_assert_not_reached();
    }
}

uint64_t xtensa_xtsc_get_size(Object *obj, int i)
{
    xtensa_xtsc *s = XTENSA_XTSC(obj);

    switch (i) {
    case 0:
        return s->size;
    case 1:
        return s->reserved_size;
    case 2:
        return s->dsp_irq;
    default:
        g_assert_not_reached();
    }
}

static SharedMemory *find_shared_memory(const char *name,
                                        hwaddr addr,
                                        uint64_t size,
                                        uint64_t reserved_size)
{
    size_t i;

    if (addr + size < addr) {
        fprintf(stderr,
                "%s: %s, %" HWADDR_PRIx "/%" PRIx64 ": wrap around\n",
                __func__, name, addr, size);
        abort();
    }
    if (size < reserved_size) {
        fprintf(stderr,
                "%s: %s, %" HWADDR_PRIx "/%" PRIx64 ": reserved size exceeds memory size\n",
                __func__, name, addr, size);
        abort();
    }
    for (i = 0; i < ARRAY_SIZE(mem); ++i) {
        if (mem[i].ref) {
            if (strcmp(mem[i].name, name) == 0 &&
                mem[i].addr == addr &&
                mem[i].size == size &&
                mem[i].reserved_size == reserved_size) {
                return mem + i;
            }
            if (addr < mem[i].addr + mem[i].size &&
                addr + size > mem[i].addr) {
                fprintf(stderr,
                        "%s: %s, %" HWADDR_PRIx "/%" PRIx64 ": overlaps other area without exact match\n",
                        __func__, name, addr, size);
                abort();
            }
        }
    }
    return NULL;
}

static SharedMemory *find_or_open_shared_memory(const char *name,
                                                hwaddr addr,
                                                uint64_t size,
                                                uint64_t reserved_size)
{
    size_t i;
    SharedMemory *pmem;

    if (!name)
        name = "SharedRAM_L";
    pmem = find_shared_memory(name, addr, size, reserved_size);

    if (pmem) {
        ++pmem->ref;
        return pmem;
    }

    for (i = 0; i < ARRAY_SIZE(mem); ++i)
        if (mem[i].ref == 0)
            break;
    pmem = mem + i;

    pmem->ref = 1;
    pmem->name = strdup(name);
    pmem->addr = addr;
    pmem->size = size;
    pmem->reserved_size = reserved_size;

    pmem->ram_ptr = xtsc_open_shared_memory(pmem->name, size);
    // Reserve a slot of memory that we will use for XTSC shared memory.
    memory_region_init_ram_ptr(&mem->memory, NULL, XTENSA_XTSC_DEV_ID, size,
                               pmem->ram_ptr);

    // Ok, now we just need to move it to the right physical address.
    memory_region_add_subregion(get_system_memory(), addr, &mem->memory);

    return pmem;
}

static void release_shared_memory(SharedMemory *pmem)
{
    if (pmem->ref == 1) {
        memory_region_del_subregion(get_system_memory(), &pmem->memory);
        free(pmem->name);
    }
    --pmem->ref;
}

static void xtensa_xtsc_realize(DeviceState *dev, Error **errp)
{
    xtensa_xtsc *s = XTENSA_XTSC(dev);
    unsigned i;

    s->shmem = find_or_open_shared_memory(s->name, s->addr,
                                          s->size, s->reserved_size);

    for (i = 0; i < ARRAY_SIZE(obj); ++i) {
        if (obj[i] == NULL) {
            obj[i] = OBJECT(dev);
            break;
        }
    }
}

static void xtensa_xtsc_unrealize(DeviceState *dev)
{
    xtensa_xtsc *s = XTENSA_XTSC(dev);
    unsigned i, j;
    
    release_shared_memory(s->shmem);
    for (i = j = 0; i < ARRAY_SIZE(obj); ++i)
        if (obj[i] != OBJECT(dev))
            obj[j++] = obj[i];
    while (j < ARRAY_SIZE(obj))
        obj[j++] = NULL;
}

static void xtensa_xtsc_class_init(ObjectClass *klass, void *data) 
{
  DeviceClass *dc = DEVICE_CLASS(klass);

  device_class_set_props(dc, xtensa_xtsc_properties);
  dc->realize = xtensa_xtsc_realize;
  dc->unrealize = xtensa_xtsc_unrealize;
  dc->desc = "xtensa xtsc connector";
  set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo xtensa_xtsc_info = {
    .name = TYPE_XTENSA_XTSC,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(xtensa_xtsc),
    .class_init = xtensa_xtsc_class_init,
};

static void xtensa_xtsc_register(void) 
{
  type_register_static(&xtensa_xtsc_info);
}

type_init(xtensa_xtsc_register);
