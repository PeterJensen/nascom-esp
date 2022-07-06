// Author: Peter Jensen

x = 0; y = 1; z = 2;

esp32Dims           = [51.6, 28.2, 4.7];
vgaPlateDims        = [30.8, 1.0, 12.4];
vgaConnectorDims    = [16.2, 16.5, 10.7];
vgaConnectorOffsets = [0, 6.2 - vgaConnectorDims[y]/2, 0];
vgaOffsets          = [-20.0, esp32Dims[y]/2+vgaPlateDims[y], 11.0];
usbDims             = [13.0, 18.0, 5.6];
usbOffsets          = [10.0, esp32Dims[y]/2 - usbDims[y]/2 + 3, vgaOffsets[z]];
sdDims              = [42.3, 23.5, 2.9];
sdOffsets           = [esp32Dims[x]/2 - sdDims[x]/2 - 20.0, 0, sdDims[z]/2 + esp32Dims[z]/2];

module esp32() {
  cube(esp32Dims, center=true);
}

module vga() {
  translate(vgaOffsets) {
    cube(vgaPlateDims, center=true);
    translate(vgaConnectorOffsets)
      cube(vgaConnectorDims, center=true);
  }
}

module usb() {
  translate(usbOffsets)
    cube(usbDims, center=true);
}

module sd() {
  translate(sdOffsets)
    cube(sdDims, center=true);
}

module components() {
  esp32();
  vga();
  usb();
  sd();
}

module main() {
  components();
}

main();