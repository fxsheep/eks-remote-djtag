# eks-remote-djtag
OpenOCD remote bitbang stub for SC8810 DSP MMIO JTAG

## Building
HACK: Build as static to run on Android
```
arm-linux-gnueabihf-gcc remote_bitbang_sc8810_djtag.c -static -o remote_bitbang_sc8810_djtag
```
Note: Your GCC version might be too new for downstream Android kernel. Use GCC 4.x

## Usage
Install busybox on Android, then push `remote_bitbang_sc8810_djtag` to somewhere executable(not /sdcard), then run it:
```
adb push remote_bitbang_sc8810_djtag /data/local/tmp
adb shell
> su
> cd /data/local/tmp
> chmod 755 remote_bitbang_sc8810_djtag
> busybox nc -l -p 7777 -e ./remote_bitbang_sc8810_djtag
```

Run openocd on host:
```
openocd -c "adapter driver remote_bitbang; remote_bitbang_host <device ip address>; remote_bitbang_port 7777" -c "jtag newtap dsp tap -irlen 32 -expected-id 0x016224a5"
```

Connect to openocd:
```
telnet 127.0.0.1 4444
```

As there's (apparently) no CEVA-X1622 support in current OpenOCD, only raw JTAG operations can be performed: 
 
Read core version(?):
```
> irscan dsp.tap 0x72000000
> drscan dsp.tap 32 0      
16220401
> 
```

Read PC value:
```
> irscan dsp.tap 0x34000000
> drscan dsp.tap 32 0      
c00a87d8
> 
```
Note that it seems only the uppermost 8bits of IR is used.

