// Author: Peter Jensen

$fn = 50;
x = 0; y = 1; z = 2;

boxOuterDims        = [60.0, 60.0, 32.0];
boxWall             = 2;
boxInnerDims        = [boxOuterDims[x] - 2*boxWall, boxOuterDims[y] - 2*boxWall, boxOuterDims[z] - 2*boxWall];

esp32Dims           = [51.6, 28.2, 5.7];
esp32Offsets        = [boxOuterDims[x]/2 - esp32Dims[x]/2, 0, -boxInnerDims[z]/2 + esp32Dims[z]/2];
vgaPlateDims        = [31.8, 1.0, 13.4];
vgaConnectorDims    = [19.0, 16.5, 10.8];
vgaConnectorOffsets = [0, 6.2 - vgaConnectorDims[y]/2, 0];
vgaOffsets          = [-10.0, boxOuterDims[y]/2 - vgaPlateDims[y]/2, 0];
usbDims             = [13.0, 18.0, 5.6];
usbOffsets          = [18.0, boxOuterDims[y]/2 - usbDims[y]/2, vgaOffsets[z]];
sdDims              = [42.3, 3.0, 23.5];
sdPins              = [8.9, 6.5, 15.0];
sdBraceDims         = [sdDims[x] + 1, sdDims[y] + 3, 3];
sdBraceOffsets      = [-boxInnerDims[x]/2 + sdBraceDims[x]/2, -boxInnerDims[y]/2 + sdBraceDims[y]/2, -boxInnerDims[z]/2 + sdBraceDims[z]/2];
sdPinsOffsets       = [sdDims[x]/2 + sdPins[x]/2 - 5.2, -sdDims[y]/2 + sdPins[y]/2 - 1.0, 0];
sdOffsets           = [-boxOuterDims[x]/2 + sdDims[x]/2, -boxInnerDims[y]/2 + sdDims[y]/2 + 1, -boxInnerDims[z]/2 + sdDims[z]/2];
esp32HolesD         = 3;
esp32HolesOffset    = [2 - esp32Dims[x]/2, 2 - esp32Dims[y]/2, -esp32Dims[z]/2];
vgaHolesD           = 3.3;
vgaHolesOffsets     = [12.5, 0, 0];

// Reset switch/mount
switch2BaseDims     = [11.9, 11.9, 3.1];
switch2StemD        = 6.6;
switch2StemH        = 3.1;
switch2TopD         = 12.9;
switch2TopH         = 5.7;
switch2Z            = switch2BaseDims[z] + switch2StemH + switch2TopH - 1;
switchCylT          = 0.5;
  
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

resetMountOffsets = [-boxOuterDims[x]/4, 0, boxOuterDims[z]/2 - switch2Z];

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

// Slots for top
slotZ        = 2;
slot1Dims    = [10, 2, 2];
slot1Offsets = [0, boxInnerDims[y]/2 + slot1Dims[y]/2, boxInnerDims[z]/2 - slot1Dims[z]/2 - slotZ];

slots2Dims    = [10, 1, 2];
slots2Offsets = [0, -boxInnerDims[y]/2 - slots2Dims[y]/2, boxInnerDims[z]/2 - slots2Dims[z]/2 - slotZ];
slots2Dist    = 30;

module slot1() {
  translate(slot1Offsets)
    cube(slot1Dims, center = true);
}

module slots2() {
  translate(slots2Offsets) {
    translate([-slots2Dist/2, 0, 0]) cube(slots2Dims, center = true);
    translate([slots2Dist/2, 0, 0]) cube(slots2Dims, center = true);
  }
}

module slots() {
  slot1();
  slots2();
}

boxRimDims = [boxInnerDims[x], boxInnerDims[y], 2];
boxRimInnerDims = boxRimDims - [2, 2, 0];

tapY1 = slot1Dims[z] + slotZ;
tapX2 = 2;
tapY2 = tapY1;
tapX3 = tapX2 + 1;
tapY3 = tapY2 - 1;
tapX4 = tapX2;
tapY4 = tapY3 - 1;
tapX5 = tapX2;
tapPoints = [[0, 0], [0, tapY1], [tapX2, tapY2], [tapX3, tapY3], [tapX4, tapY4], [tapX5, 0]];
tapW  = slot1Dims[x] - 1;

module tap() {
  rotate([-90, 0, 0])
    translate([0, 0, -tapW/2])
      linear_extrude(height = tapW)
        polygon(tapPoints);
}

module taps() {
  xOffset1 = boxRimDims[y]/2 - tapX2/2 - 0.5;
  xOffset2 = -boxRimDims[y]/2 + tapX2/2;
  yOffset  = boxRimDims[x]/2 - tapX2;
  zOffset  = boxInnerDims[z]/2;
//  tap();
  translate([0, yOffset, zOffset]) rotate([0, 0, 90]) tap();
  translate([slots2Dist/2, -yOffset, zOffset]) rotate([0, 0, -90]) tap();
  translate([-slots2Dist/2, -yOffset, zOffset]) rotate([0, 0, -90]) tap();
}


module esp32Holes(d = esp32HolesD) {
  translate(esp32HolesOffset)
    cylinder(d=d, h=esp32Dims[z]);
  translate([esp32HolesOffset[x], -esp32HolesOffset[y], esp32HolesOffset[z]])
    cylinder(d=d, h=esp32Dims[z]);
}

module esp32() {
  translate(esp32Offsets)
    difference() {
      cube(esp32Dims, center=true);
      esp32Holes();
    }
}

module vgaHoles(d = vgaHolesD) {
  module oneHole() {
    h = vgaPlateDims[y] + 5;
    translate([0, h/2, 0])
      rotate([90, 0, 0])
        cylinder(d = d, h = h);
  }
//  translate(vgaOffsets) {
    translate(vgaHolesOffsets)
      oneHole();
    translate([-vgaHolesOffsets[x], vgaHolesOffsets[y], vgaHolesOffsets[z]])
      oneHole();
//  }
}

module vga() {
  translate(vgaOffsets) {
    difference() {
      cube(vgaPlateDims, center=true);
      vgaHoles();
    }
    translate(vgaConnectorOffsets)
      cube(vgaConnectorDims, center=true);
  }
}

module usb() {
  translate(usbOffsets)
    cube(usbDims, center=true);
}

module sd() {
  translate(sdOffsets) {
    cube(sdDims, center=true);
    translate(sdPinsOffsets)
      cube(sdPins, center=true);
  }
}

module sdBrace() {
  difference() {
    translate(sdBraceOffsets)
      cube(sdBraceDims, center=true);
    sd();
  }
}

module components() {
    esp32();
    vga();
    usb();
    sd();
    translate(vgaOffsets)
      vgaHoles();
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

module boxText(str, size = 8) {
  rotate([90, 0, 0])
    linear_extrude(height = 5)
      text(str, font="Ariel:style=Bold", size, halign="center", valign="center", spacing=0.95);
}

module nascomText() {
  translate([0, -boxOuterDims[y]/2 + 0.5, 5])
    boxText("NASCOM-2", 7.5);
}

module madeByText() {
  translate([0, -boxOuterDims[y]/2 + 0.5, -5])
    boxText("Jury-rigged by: Peter Jensen", 3);
}

module serialNoText() {
  translate([0, -boxOuterDims[y]/2 + 0.5, -11])
    boxText("Serial No.: 3", 3);
}
  
module boxBottom() {
  color([0.3, 0.5, 0.8, 1.0]) {
    difference() {    
      roundedBox(boxOuterDims, 4);
      cube(boxInnerDims, center=true);
      nascomText();
      madeByText();
      serialNoText();
      components();
      slots();
      translate([0, 0, boxOuterDims[z] - boxWall])
        cube(boxOuterDims, center=true);
    }
    sdBrace();
    translate(esp32Offsets)
      esp32Holes(esp32HolesD - 0.5);
  }
}

module boxTopRim() {
  translate([0, 0, boxInnerDims[z]/2 - boxRimDims[z]/2])
    difference() {
      cube(boxRimDims, center = true);
      cube(boxRimInnerDims, center = true);
    }
}

ledD = 4.9;
ledOffsets = [boxOuterDims[x]/4, 0, boxInnerDims[z]/2 + boxWall/2];
module led() {
  translate(ledOffsets)
    cylinder(d=ledD, h=boxWall, center=true);
}

module boxTop() {
  difference() {
    roundedBox(boxOuterDims, 4);
    translate([0, 0, -boxWall])
      cube(boxOuterDims, center=true);
    resetMount(true);
    led();
  }
  boxTopRim();
  resetMount();
  taps();
}

module main() {
  boxBottom();
  //components();
  //boxTop();
  //resetMount();
}

main();