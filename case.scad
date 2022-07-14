// Author: Peter Jensen

x = 0; y = 1; z = 2;

esp32Dims           = [51.6, 28.2, 4.7];
vgaPlateDims        = [30.8, 1.0, 12.4];
vgaConnectorDims    = [16.2, 16.5, 10.7];
vgaConnectorOffsets = [0, 6.2 - vgaConnectorDims[y]/2, 0];
vgaOffsets          = [-18.0, esp32Dims[y]/2+vgaPlateDims[y]+1.5, 11.0];
usbDims             = [13.0, 18.0, 5.6];
usbOffsets          = [10.0, esp32Dims[y]/2 - usbDims[y]/2 + 3, vgaOffsets[z]];
sdDims              = [42.3, 23.5, 2.9];
sdOffsets           = [esp32Dims[x]/2 - sdDims[x]/2 - 20.0, 0, sdDims[z]/2 + esp32Dims[z]/2];
componentsDims      = [esp32Dims[x] + (sdDims[x] - esp32Dims[x])/2 - sdOffsets[x],
                       esp32Dims[y],
                       esp32Dims[z]/2 + vgaOffsets[z] + vgaPlateDims[z]/2];
boxOuterDims        = [componentsDims[x], componentsDims[y] + 6, componentsDims[z] + 10];
boxWall             = 2;
boxInnerDims        = [boxOuterDims[x] - 2*boxWall, boxOuterDims[y] - 2*boxWall, boxOuterDims[z] - 2*boxWall];

componentsOffsets   = [componentsDims[x]/2 - esp32Dims[x]/2, 0, esp32Dims[z]/2 - boxInnerDims[z]/2];

switch2BaseDims = [11.9, 11.9, 3.1];
switch2StemD = 6.6;
switch2StemH = 3.1;
switch2TopD  = 12.9;
switch2TopH  = 5.7;
switch2Z     = switch2BaseDims[z] + switch2StemH + switch2TopH - 1;
switchCylT = 0.5;
  
module switch2() {
  translate([0, 0, switch2BaseDims[z]/2]) {
    cube(switch2BaseDims, center = true);
    translate([0, 0, switch2BaseDims[z]/2])
      cylinder(d = switch2StemD, h = switch2StemH, $fn = 40);
    translate([0, 0, switch2BaseDims[z]/2 + switch2StemH])
      cylinder(d = switch2TopD, h = switch2TopH, $fn = 40);
  }
}

switch2CylT      = 1.2;
switch2CylMargin = 1.0;
switch2CylZ      = switch2Z;
switch2CylD      = switch2TopD + switch2CylMargin;

resetMountOffsets = [-boxOuterDims[x]/2 + 12, -boxOuterDims[y]/2 + 12, boxOuterDims[z]/2 - switch2Z];

module switch2Cyl() {
  translate([0, 0, 0]) {
    difference() {
      cylinder(d = switch2CylD + 2*switch2CylT, h = switch2Z, $fn = 40);
      translate([0, 0, switch2BaseDims[z]])
        cylinder(d = switch2CylD, h = switch2Z, $fn = 40);
      translate([0, 0, switch2BaseDims[z]/2])
        cube(switch2BaseDims, center = true);
    }
  }
}

module resetMount(cut = false, show = false) {
  translate(resetMountOffsets) {
    if (show)
      switch2();
    switch2Cyl();
    if (cut)
      cylinder(d = switch2CylD + 2*switchCylT, h = switch2CylZ);
  }
}

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
  translate(componentsOffsets) {
    esp32();
    vga();
    usb();
    sd();
  }
}


module roundedBox(dims, radius) {
  module cut() {
    difference() {
      cube([2*radius, 2*radius, dims[z]], center=true);
      translate([radius, radius, -dims[z]/2])
        cylinder(r=radius, h=dims[z], $fn=50);
    }
  }
  difference() {
    cube(dims, center=true);
    translate([-dims[x]/2, -dims[y]/2, 0])
      cut();
    translate([-dims[x]/2, dims[y]/2, 0])
      rotate([0, 0, -90]) cut();
    translate([dims[x]/2, -dims[y]/2, 0])
      rotate([0, 0, 90]) cut();
    translate([dims[x]/2, dims[y]/2, 0])
      rotate([0, 0, 180]) cut();
  }
}

module boxText(str) {
  translate([0, -boxOuterDims[y]/2 + 0.5, 0])
    rotate([90, 0, 0])
      linear_extrude(height = 5)
        text(str, font="Ariel:style=Bold", size=8, halign="center", valign="center", spacing=0.95);
}

module box() {
  color([0.3, 0.5, 0.8, 1.0])
    difference() {    
      roundedBox(boxOuterDims, 4);
      cube(boxInnerDims, center=true);
      boxText("NASCOM-2");
      components();
      translate([0, 0, boxOuterDims[z] - boxWall])
        cube(boxOuterDims, center=true);
    }
}

module main() {
  box();
  //components();
  resetMount();
}

main();