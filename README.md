# Introduction to WEB_SENSORS APPLICATION
This repo contains the SW that is the WEB_SENSORS device-side application, intended to run on a NINA-W10 module, which contains an ESP32 chip.  The HW repo for the associated WHRE application board can be found [here](https://github.com/u-blox/whre-prototype).

This repo can be used in:

1.  as a standalone C environment directly under ESP32/ESP_IDF (without the WHRE device-side application),


# Standalone C Usage Directly Under ESP32/ESP_IDF
The components of this repo can be built, run (on an ESP32 platform such as `EVK-NINA-W10` or a WHRE application board) and unit tested as C code directly under ESP32.  Only a rudimentary application is provided since the application support is intended to be provided by the u-blox/Connect Blue Javascript world; if you want the full WHRE device-side application, please look at that entry in this `README.md` file instead.

## Building And Downloading
To build this project you need to follow the instructions here to set up the Espressif build environment; I used the Windows version:

https://docs.espressif.com/projects/esp-idf/en/latest/get-started/index.html#setup-toolchain

You MUST use this version of the Espressif MSYS32-based environment: https://dl.espressif.com/dl/esp32_win32_msys2_environment_and_toolchain-20180110.zip (NOT 2019xxx) and you must `git clone` release 3.1.1 of the ESP32 environment with `git clone -b release/v3.1 --recursive https://github.com/espressif/esp-idf.git`.  If you wish to use the Eclipse project files that come with this repo unchanged, you must set your username to `rob` (paths are absolute). You must have an `IDF_PATH` environment variable in your MSYS32 environment which points to the location of your `/c/msys32/home/your_user_name_here/esp/esp-idf` directory (best to use a Linux-style path, though the Windows form will  usually work also).  This may be made persistent by creating a file called `export_idf_path.sh` in your `C:/msys32/etc/profile.d` directory containing the single line `export IDF_PATH="/c/msys32/home/your_user_name_here/esp/esp-idf"`.

Building with the Espressif environment is a journey back to the last century.  It uses Cygwin and a command-line menu system.  For an IDE, we use Eclipse and that involves setting up a very specific project environment which includes hard coded paths.  Some advice:

- only install the Espressif tools in the default directory, `c:/msys32`; doing anything else will involve a lot of typing later,
- the path for this project on your hard drive must be `c:/msys32/home/rob/esp/whre-device-application` (I'm Rob, Hi!), otherwise more typing/configuring will ensue.

This is the route to an easy life, you have been warned.

So, clone this project into the `c:/msys32/home/your_user_name_here/esp` directory.

You should then launch the Espressif `msys` command prompt, `cd` into the build directory `~/esp/whre-device-application` and run `make menuconfig` to tell it two things:

- which `COM` port your `NINA-W10` is attached to (`Serial flasher config`),
- that you want 64-bit support (select `Component config`->`ESP32-specific`->`Enable 'nano' formatting options for printf/scanf family` and press the space bar to make sure it is NOT selected (i.e. no `*` is shown next to it).

You can then build and download the code from the command prompt by entering `make flash`.  If you're running on an `EVK-NINA-W10` then, when the flash programmer starts addressing your `COM` port, hold down the `BOOT` button on the `EVK-NINA-W10` and press the `RESET` button on the `EVK-NINA-W10` (while still holding `BOOT` down).  Once programming has commenced you can release both buttons.

To build under Eclipse ("Eclipse for C/C++ Development", AKA CDT) just use `File -> Import...` and, in the dialog that pops up, choose `C/C++ -> Existing Code as Makefile Project`, set the root directory to `c:/msys32/home/your_user_name_here/esp/whre-device-application` and import that project.  You will then need to set up the Eclipse environment, which is specific to that Eclipse project, as described here:

https://docs.espressif.com/projects/esp-idf/en/latest/get-started/eclipse-setup.html#project-properties

When building under Eclipse it is not always clear if the build has failed: if your build ends with the line "To flash all build output, run 'make flash' or:" followed by a Python command line then you're good to go, otherwise you need to scroll up and look for red lines in the console window.  It does, however, give you nice context highlighting in the editor window and search capability, so it is worth doing if you intend to spend some time doing development.

## Running
Once the `EVK-NINA-W10` board has been programmed, run `make monitor` at the Espressif `msys` command prompt to run the Espressif debug environment (IDF Monitor) and then press the `RESET` button on the `EVK-NINA-W10` board.

## Testing
Follow the instructions [here](https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/unit-tests.html) to set up the Espressif tools for unit testing.  The component name for this project is "whre-application" so the relevant unit tests can be built, at the `msys` prompt, by `cd`ing to `~/esp/whre-device-application` and then entering the following command:

`make -j4 -C ${IDF_PATH}/tools/unit-test-app EXTRA_COMPONENT_DIRS=~/esp/whre-device-application TEST_COMPONENTS=whre-application`

Note that you must use absolute paths (the `msys` shell expands `~` to provide an absolute path) or `make` will [silently] not work.  Also, the build output is not kept in your project directory, it's over in the `esp-idf\tools\unit-test-app` directory, so if you find yourself stuck with some old config settings you might want to go and delete the `build` directory and `sdkconfig` file over there and re-build.

When the build is complete it will tell you how to load the binary onto the target.  Note: `make flash` is the simple form but below that you will see the longer Python command-line; if you use the Python version it doesn't spend 15 seconds thinking before acting *and* it works, whereas I found that `make flash` downloaded a previous, non-test, binary that I'd built.

If you're running on `EVK-NINA-W10` you'll need to do the RESET/BOOT key dance to complete the download.  Start IDF Monitor with `make monitor` (you could also have added `monitor` to the `make flash ...` construction) and press the RESET button.  After some start-up diagnostic prints you should see:

`Press ENTER to see the list of tests.`

Press ENTER and you will see:

`Here's the test menu, pick your combo:`

...followed by a list of tests for this project and its components, something like:

`(1)     "initialisation" [battery-charger-bq24295]`

If no tests show up, it is probably because you got a path wrong in the `make` line above and `make` has silently not worked.  Follow the instructions [here](https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/unit-tests.html#running-unit-tests) on how to run tests.

## Debugging Over The Serial Port
With IDF Monitor you can view the system state and any `printf()` output.  It is also possible to configure the target code to jump into a "`gdb`-like" stub when a processor exception is hit; this allows back-trace, examination of CPU registers and static variables, nothing else (no stack variables, no possibility to single-step or continue in any way).

## JTAG Debugging With Segger J-Link
It is possible to run break-point/single-step type debugging over JTAG using a Segger J-Link box if you have access to the JTAG pins of the `NINA-W10` module. HOWEVER, Espressif require that this is done through THEIR port of OpenOCD and OpenOCD only works with a J-Link box on Windows if you replace the Segger J-Link driver with WinUSB and, once you've done that, none of the normal Segger software stuff will work until you switch back to the J-Link driver once more.  See [here](https://wiki.segger.com/OpenOCD) for more information; you have been warned.

The generic instructions can be found here:

https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/jtag-debugging/index.html

For the Segger J-Link case, on Windows, these become:

- Unzip the latest win32 release of OpenOCD for ESP32 from [here](https://github.com/espressif/openocd-esp32/releases) into `c:\msys32\home\your_user_name_here\esp`.
- Download [Zadig](https://zadig.akeo.ie/).  This will allow you to use WinUSB instead of the J-Link driver.  However note that it is then possible to screw up other USB devices; the first entry in the Zadiq [FAQ](https://github.com/pbatard/libwdi/wiki/FAQ#Zadig) tells you how to fix that.  And of course you'll have to put the jlink driver back to use the Segger J-Link with any other device; don't forget.
- Run Zadig and get it to list all of your USB devices.  From the pull-down, find the jlink driver and replace it with WinUSB.  Exit Zadiq.
- Open a Windows command prompt and `cd` to `C:\msys32\home\your_user_name_here\esp\openocd-esp32`.  Run the following command:

  `bin\openocd -s share\openocd\scripts -f ..\whre-device-application\jlink-debugger.cfg`

  You should see something like:
  
  ```
  Open On-Chip Debugger 0.10.0-dev (2018-11-05-04:10)
  Licensed under GNU GPL v2
  For bug reports, read
          http://openocd.org/doc/doxygen/bugs.html
  adapter speed: 10000 kHz
  esp32 interrupt mask on
  Info : No device selected, using first device.
  Info : J-Link V10 compiled Sep  5 2018 10:54:19
  Info : Hardware version: 10.10
  Info : VTarget = 3.335 V
  Info : clock speed 10000 kHz
  Info : JTAG tap: esp32.cpu0 tap/device found: 0x120034e5 (mfg: 0x272 (Tensilica), part: 0x2003, ver: 0x1)
  Info : JTAG tap: esp32.cpu1 tap/device found: 0x120034e5 (mfg: 0x272 (Tensilica), part: 0x2003, ver: 0x1)
  Info : Target halted. PRO_CPU: PC=0x400D20A8 (active)    APP_CPU: PC=0x400E0F7E
  Info : Detected debug stubs @ 3ffb2aac on core0 of target 'esp32'
  ```
  
  If you get a "LIBUSB" error of some form then you've not successfully switched to the WinUSB driver.  If "VTarget" is not around 3.3 V then the target is not powered.  If you get a JTAG error, or both  JTAG TAPs are not found, then there is an electrical connection issue in the JTAG interface. If you get warnings about DIR instruction overruns, try reducing the adapter speed in the configuration file. 
  
  Leave this running and, to check that GDB works from the command-line, open your `msys` prompt, `cd` to `~/esp/whre-device-application` and enter the command:
  
  `xtensa-esp32-elf-gdb -x gdbinit build/whre-device-application.elf`
  
  In the OpenOCD window you should see something like:
  
  ```
  Info : accepting 'gdb' connection on tcp/3333
  Info : Target halted. PRO_CPU: PC=0x4009171A (active)    APP_CPU: PC=0x400E0F7E
  Info : Flash mapping 0: 0x10020 -> 0x3f400020, 21 KB
  Info : Flash mapping 1: 0x20018 -> 0x400d0018, 67 KB
  Info : Target halted. PRO_CPU: PC=0x4009171A (active)    APP_CPU: PC=0x400E0F7E
  Info : Auto-detected flash size 2048 KB
  Info : Using flash size 2048 KB
  Info : Target halted. PRO_CPU: PC=0x4009171A (active)    APP_CPU: PC=0x400E0F7E
  Info : Flash mapping 0: 0x10020 -> 0x3f400020, 21 KB
  Info : Flash mapping 1: 0x20018 -> 0x400d0018, 67 KB
  Info : Using flash size 68 KB
  Info : Target halted. PRO_CPU: PC=0x4009171A (active)    APP_CPU: PC=0x400E0F7E
  Info : Flash mapping 0: 0x10020 -> 0x3f400020, 21 KB
  Info : Flash mapping 1: 0x20018 -> 0x400d0018, 67 KB
  Info : Using flash size 24 KB
  Info : JTAG tap: esp32.cpu0 tap/device found: 0x120034e5 (mfg: 0x272 (Tensilica), part: 0x2003, ver: 0x1)
  Info : JTAG tap: esp32.cpu1 tap/device found: 0x120034e5 (mfg: 0x272 (Tensilica), part: 0x2003, ver: 0x1)
  Info : esp32: Debug controller 0 was reset (pwrstat=0x5F, after clear 0x0F).
  Info : esp32: Core 0 was reset (pwrstat=0x5F, after clear 0x0F).
  Info : Target halted. PRO_CPU: PC=0x5000004B (active)    APP_CPU: PC=0x00000000
  Info : esp32: Core 0 was reset (pwrstat=0x1F, after clear 0x0F).
  Info : esp32: Debug controller 1 was reset (pwrstat=0x5F, after clear 0x0F).
  Info : esp32: Core 1 was reset (pwrstat=0x5F, after clear 0x0F).
  Info : Target halted. PRO_CPU: PC=0x40000400 (active)    APP_CPU: PC=0x40000400
  Info : Target halted. PRO_CPU: PC=0x400D20A8 (active)    APP_CPU: PC=0x400E0F7E
  Info : Detected debug stubs @ 3ffb2aac on core0 of target 'esp32'
  ```
  
  ...and in the `gdb` window you should see something like:
  
  ```
  GNU gdb (crosstool-NG crosstool-ng-1.22.0-80-g6c4433a5) 7.10
  Copyright (C) 2015 Free Software Foundation, Inc.
  License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>
  This is free software: you are free to change and redistribute it.
  There is NO WARRANTY, to the extent permitted by law.  Type "show copying"
  and "show warranty" for details.
  This GDB was configured as "--host=i686-host_pc-mingw32 --target=xtensa-esp32-elf".
  Type "show configuration" for configuration details.
  For bug reporting instructions, please see:
  <http://www.gnu.org/software/gdb/bugs/>.
  Find the GDB manual and other documentation resources online at:
  <http://www.gnu.org/software/gdb/documentation/>.
  For help, type "help".
  Type "apropos word" to search for commands related to "word"...
  Reading symbols from build/whre-device-application.elf...done.
  app_main () at C:/msys32/home/rob/esp/whre-device-application/main/main.c:17
  17      {
  JTAG tap: esp32.cpu0 tap/device found: 0x120034e5 (mfg: 0x272 (Tensilica), part: 0x2003, ver: 0x1)
  JTAG tap: esp32.cpu1 tap/device found: 0x120034e5 (mfg: 0x272 (Tensilica), part: 0x2003, ver: 0x1)
  esp32: Debug controller 0 was reset (pwrstat=0x5F, after clear 0x0F).
  esp32: Core 0 was reset (pwrstat=0x5F, after clear 0x0F).
  Target halted. PRO_CPU: PC=0x5000004B (active)    APP_CPU: PC=0x00000000
  esp32: Core 0 was reset (pwrstat=0x1F, after clear 0x0F).
  esp32: Debug controller 1 was reset (pwrstat=0x5F, after clear 0x0F).
  esp32: Core 1 was reset (pwrstat=0x5F, after clear 0x0F).
  Target halted. PRO_CPU: PC=0x40000400 (active)    APP_CPU: PC=0x40000400
  Hardware assisted breakpoint 1 at 0x400d20a8: file C:/msys32/home/rob/esp/whre-device-application/main/main.c, line 17.
  Target halted. PRO_CPU: PC=0x400D20A8 (active)    APP_CPU: PC=0x400E0F7E
  [New Thread 1073438800]
  [New Thread 1073436900]
  [New Thread 1073441456]
  [New Thread 1073429276]
  [New Thread 1073412744]
  [New Thread 1073413512]
  [New Thread 1073430408]
  [Switching to Thread 1073434868]
  
  Temporary breakpoint 1, app_main ()
      at C:/msys32/home/rob/esp/whre-device-application/main/main.c:17
  17      {
  ```
  
  You are now in `gdb` at the temporary breakpoint that has been inserted at `main()`; knock yourself out.

# Use Under u-blox/Connect Blue Javascript Environment
Support for the WHRE device-side software at an application level is provided by the u-blox/Connect Blue Javascript environment.  Note that unit testing of components currently does NOT work in this environment; to build/run unit tests please set up for the standalone C world, make sure that the `IDF_PATH` environment variable is pointing to that installation of `esp-idf`, e.g. `c:/msys32/home/your_user_name_here/esp/esp-idf` and NOT the one for the u-blox/Connect Blue world, and follow the instructions above.

## Building And Downloading The Binary Image
First, clone the `nina-esp` project (`git@git-sho-mlm.u-blox.net:sw-emb-sho/products/nina-esp.git`) from the u-blox Connect Blue Gitlab repo:

`git clone git@git-sho-mlm.u-blox.net:sw-emb-sho/products/nina-esp.git`

`cd` to the root directory of `nina-esp` and switch to the `whre_integration` branch by entering:

`git checkout whre_integration`

Now follow the instructions in the `readme.md` file in the same folder to set up the environment and build/download the `nina-esp` binary image, noting that if you are building from the command-line, you **must** add `WHRE_ENABLED=1` to the end of the command line to bring in the WHRE components.

Note: if the build fails with an error coming out of a `make` file (e.g. something like `project.mk: *** multiple target patterns.  Stop.`) check that your `IDF_PATH` is pointing at the installation of the `esp-idf` tools within the u-blox/Connect Blue Javascript repo (i.e. `` `pwd`/nina-esp/component/cb_rtsl_esp32/esp-idf``) and **not** the `esp-idf` tools used for the standalone C environment above.

To check that the build/download has worked, attach an `EVK-NINA-W10` board or a WHRE application board to your PC and open a terminal on the COM port which is the AT interface to `NINA-W1` (if you're using `EVK-NINA-W1` this will be the lowest-numbered of the two COM ports that appear) at 115200 bits/s. If you type `AT` in that terminal window and press \<enter\> you should get back the reponse `OK`.  You may also open a terminal on the logging COM port (if you're using `EVK-NINA-W1` this is the higher numbered of the two COM ports) at 1000000 bits/s, press the RESET button on the board (or power it off and on again) and you should see a stream of logging text, at the end of which you should see something like:

```
5809:cb_mjs.c:00324: Start script init.js
5811:cb_mjs.c:00142: MJS Print: failed to read file "init.js"
5812:cb_mjs.c:00328: mJS error 7
```

## Editing/Downloading Javascript
The simplest way of doing this is to install [MS Visual Studio Code](https://code.visualstudio.com/download).  With this installed, got to `View`->`Extensions` and search for "u-blox Script IDE"; install this extension.  Now do `File`->`Open folder` and open the folder `component\cb_rtsl_utils\scripting`.  The default script file `init.js` is in that folder.  With that script file opened press `CTRL-SHIFT-P`, select the command `u-blox: Select serial port` and chose the serial port to which `NINA-W1`'s AT UART interface is connected.  Then press `CTRL-SHIFT-P` again and select the command `u-blox: Send files to device`; this should download the script file to the target (using the AT command `AT+UDWNFILE`).

Reset the target and your newly downloaded `init.js` script should be loaded and executed.  To check what's going on take a look at the output from the logging COM port and you should see debug prints from your code's execution.

### If Your Script Causes The Target To Crash
If your script causes the target to crash then you're a bit stuffed as you won't be able to download a new script 'cos the target has crashed.  To get out of this situation, connect a terminal to the AT-command COM port and put the following into your copy buffer:

```
AT+UFACTORY
AT+CPWROFF
```

Reset the target and, as soon as you see the `+STARTUP` string come out, paste your copy buffer into the terminal window.  This should erase any stored scripts and reset `NINA-W1`.# web_sensors
# web_sensors
