
struct pci_info {
  ushort vendor_id;
  ushort device_id;

  uchar header_type;
  uchar class_code;
  uchar subclass;
  uchar unused1;

  uchar prog_if;
  uchar rev_id;
  uchar int_pin;
  uchar int_line;

  uint32 base[6];
  uint32 size[6];

  uchar kind[6];
  uchar unused2[2];
};

void pci_init(void);
struct pci_info *pci_get_nth(uint n);
struct pci_info *pci_find(uint vendor, uint device);
