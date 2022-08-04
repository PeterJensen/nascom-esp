# Nascom-2 on ESP32

A hardware emulator for the Nascom-2 computer running on an ESP-32 with:
* VGA for display output
* PS/2 keyboard for input
* SD card reader for tape input/output

## History
The Nascom-2 was an early (1979) Z80 based micro.  Typically homebuilt from a kit: https://en.wikipedia.org/wiki/Nascom_(computer_kit)

The one I built 40+ years ago still runs!

## Specs
The specs of a typical machine:
* 4MHz Z80 (an 8080 compatible chip from Zilog)
* 8K static RAM on motherboard
* 64K dynamic RAM on extension board
* Cassette tape used for storage
* NAS-SYS monitor in ROM (2K) 
* Microsoft Basic in ROM (8K)

## Software
Lots of excellent software was written for this machine; including Assemblers/Disassemblers, Pascal editors/compilers, text formatters, games, etc.

The BLS Pascal compiler/editor deserves a special mention.  In my opinion, this is one of the most impressive pieces of software written. It was implemented by Anders Hejsberg.  It is the origin of the Turbo Pascal and Delphi lineage of Pascal (-like) compilers.  It generates executable Z80 code and allows code to be written with a full screen editor, which was a major leap in programmer productivity, and a welcome reprive from the line-by-line editing for Basic programs. The compiler and editor was a **12K of code**. You'll be hard pressed to write a hello-world program and compile it into less code with today's 'modern' development environments.

An excellent resource/archive for software is Tommy Thorn's [nascomhomepage](www.nascomhomepage.com).

## Diagram
![Diagram](images/diagram.jpg)

## Box
openSCAD was used to design a box, which can be printed on a 3D printer. I'm using the Creality Ender-3 printer, which is a very good budget printer (~$300).

I use openSCAD because doing the designs is much like writing code, so I don't have to step out of my comfort zone.

![Box](images/box-1.jpg)
