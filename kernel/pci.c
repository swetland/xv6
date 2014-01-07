/* pci.c 
 *
 * Copyright (c) 2013 Brian Swetland
 * 
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "types.h"
#include "defs.h"
#include "x86.h"

#include "pci.h"

#define ADDR 0xCF8
#define DATA 0xCFC

#define PCI_DEBUG 1

static uint32 pciread(uint bus, uint dev, uint fn, uint reg) {
  uint n = (1 << 31) |
    ((bus & 0xff) << 16) | ((dev & 0x1F) << 11) |
    ((fn & 0x7) << 8) | (reg & 0xFC);
  outl(ADDR, n);
  return inl(DATA);
}

static void pciwrite(uint bus, uint dev, uint fn, uint reg, uint val) {
  uint n = (1 << 31) |
    ((bus & 0xff) << 16) | ((dev & 0x1F) << 11) |
    ((fn & 0x7) << 8) | (reg & 0xFC);
  outl(ADDR, n);
  outl(DATA, val);
}

static char *pci_bar_type[4] = { "32bit", "16bit", "64bit", "" };

static void pciprobe(uint32 bus, uint32 dev, uint32 fun, struct pci_info *pci) {
  uint32 n, i, s;

  memset(pci, 0, sizeof(*pci));

  n = pciread(bus, dev, fun, 0x00);
  pci->device_id = n >> 16;
  pci->vendor_id = n;

  n = pciread(bus, dev, fun, 0x08);
  pci->class_code = n >> 24;
  pci->subclass = n >> 16;
  pci->prog_if = n >> 8;
  pci->rev_id = n;

  n = pciread(bus, dev, fun, 0x0c);
  pci->header_type = n >> 24;

#if PCI_DEBUG
  cprintf("%d:%d.%d V=%x D=%x C=%x/%x/%x",
    bus, dev, fun, pci->vendor_id, pci->device_id,
    pci->class_code, pci->subclass, pci->prog_if);
#endif

  switch (pci->header_type & 0x7F) {
  case 0x00: // Device
    s = pciread(bus, dev, fun, 0x04);
    pciwrite(bus, dev, fun, 0x04, 0); // disconnect from bus 
    for (i = 0; i < 6; i++) {
      pci->base[i] = pciread(bus, dev, fun, 0x10 + 4*i);
      pciwrite(bus, dev, fun, 0x10 + 4*i, 0xffffffff);
      n = pciread(bus, dev, fun, 0x10 + 4*i);
      pciwrite(bus, dev, fun, 0x10 + 4*i, pci->base[i]);
      pci->kind[i] = pci->base[i] & 0xF;
      if (pci->base[i] & 1) {
        pci->base[i] &= 0xFFFC;
        pci->size[i] = (~(n & 0xFFFC) & 0xFFFF) + 1;
      } else {
        pci->base[i] &= 0xFFFFFFF0;
        pci->size[i] = (~(n & 0xFFFFFFF0) & 0xFFFFFFFF) + 1;
      }
      if ((pci->kind[i] & 6) == 2) {
        // 64bit BAR uses the next slot too
        i++;
        pci->base[i] = pciread(bus, dev, fun, 0x10 + 4*i);
        pci->size[i] = 0;
        pci->kind[i] = 0;
      }
    }
    pciwrite(bus, dev, fun, 0x04, s); // restore to bus;
    n = pciread(bus, dev, fun, 0x3c);
    pci->int_pin = n >> 8;
    pci->int_line = n;
#if PCI_DEBUG
    cprintf(" I=%d/%d [", pci->int_line, pci->int_pin);
    for (i = 0; i < 6; i++) {
      if (pci->base[i] == 0)
        continue;
      cprintf(" %d:%x-%x", i, pci->base[i], pci->base[i] + pci->size[i] - 1);
      if ((pci->kind[i] & 6) == 2)
        i++;
    }
    cprintf(" ]\n");
#endif
    break;
  case 0x01: // Bridge
#if PCI_DEBUG
    cprintf(" Bridge");
#endif
    break;
  default:
#if PCI_DEBUG
    cprintf("T=%x\n", pci->header_type);
#endif
    break;
  }
}

#define PCIMAX 128
static struct pci_info pci_infos[PCIMAX];
static uint pci_count = 0;

void pci_init(void) {
  uint32 bus, dev, fun;
  struct pci_info *pci = pci_infos;
  for (bus = 0; bus < 256; bus++) {
    for (dev = 0; dev < 32; dev++) {
      if (pciread(bus, dev, 0, 0) != 0xFFFFFFFF) {
        pciprobe(bus, dev, 0, pci++);
        if ((pci - pci_infos) > PCIMAX)
          goto done;
      }
      if (pciread(bus, dev, fun, 12) & 0x00800000) {
        for (fun = 1; fun < 8; fun++)
          if(pciread(bus, dev, fun, 0) != 0xFFFFFFFF) {
            pciprobe(bus, dev, fun, pci++);
            if ((pci - pci_infos) > PCIMAX)
              goto done;
          }
      }
    }
  }
done:
  pci_count = pci - pci_infos;
}

struct pci_info *pci_get_nth(uint n) {
  if (n >= pci_count)
    return 0;
  return pci_infos + n;
}

struct pci_info *pci_find(uint vendor, uint device) {
  uint n;
  for (n = 0; n < pci_count; n++) {
    if (pci_infos[n].vendor_id != vendor)
      continue;
    if (pci_infos[n].device_id != device)
      continue;
    return pci_infos + n;
  }
  return 0;
}

