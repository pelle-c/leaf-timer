# leaf-timer

## Stack trace decode
```
$ pwd
/home/pc/.arduino15/packages/esp32/tools/esp-x32/2405/bin
$ ./xtensa-esp32-elf-addr2line -aspCfire /home/pc/.cache/arduino/sketches/89CD04695C8F2761AA26B72F230E20AD/Software.ino.elf 0x400825dd:0x3ffb1fe0 0x4008e5c5:0x3ffb2000 0x40094839:0x3ffb2020 0x4015b007:0x3ffb20a0 0x4015b03c:0x3ffb20c0 0x4015b982:0x3ffb20e0 0x4015b19d:0x3ffb2100 0x4015b1cd:0x3ffb2120 0x400d3b9a:0x3ffb2140 0x400d443f:0x3ffb2160 0x400d465b:0x3ffb21d0 0x400d47a5:0x3ffb2230 0x400e5c70:0x3ffb2270 0x4008f336:0x3ffb2290
0x400825dd: panic_abort at panic.c:463
0x4008e5c5: esp_system_abort at esp_system_chip.c:92
0x40094839: abort at abort.c:38
0x4015b007: __cxxabiv1::__terminate(void (*)()) at eh_terminate.cc:48
0x4015b03c: std::terminate() at eh_terminate.cc:58 (discriminator 1)
0x4015b982: __cxa_allocate_exception at eh_alloc.cc:412
0x4015b19d: operator new(unsigned int) at new_op.cc:54
0x4015b1cd: operator new[](unsigned int) at new_opv.cc:32
0x400d3b9a: string2int(String) at Software.ino:491 (discriminator 1)
0x400d443f: get_hour(String) at Software.ino:442 (discriminator 2)
0x400d465b: timer_is_active() at Software.ino:464 (discriminator 2)
0x400d47a5: loop() at Software.ino:299
0x400e5c70: loopTask(void*) at main.cpp:74
0x4008f336: vPortTaskWrapper at port.c:139
```
