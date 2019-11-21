# Introduction
This folder contains both the XML and the generated Lua versions of the custom LWM2M objects used by the code in this repo.  The Lua files are loaded onto SARA-R412M to effect the definition of LWM2M objects.  The XML format is as standardised by OMA for LWM2M objects.

# Generation
The Lua files are generated from the XML using the Lua script `lwm2m_object_generator.lua` (which is part of the SARA-R412M firmware release).  To run this script you must have Lua installed on your PC.  You then simply run:

```
lwm2m_object_generator.lua your_definition_file.xml
```

If all goes well, you will end up with a `.lua` file written to disk bearing the name `object_` followed by the `<Name />` field from the XML definition converted to lower case with underscores.  For instance, an XML object containing the name field `<Name>Freds Thing</Name>` would be named `object_freds_thing.lua`.

# Usage
The `.lua` files must be loaded into SARA-R412M.  This can be done using Qualcomm tools if you have them (the files must end up in the `/config` directory of the alternate file system) or it can be done over the AT interface.  Both approaches can use the direct USB interface to SARA-R412M, so that you can download the files from a PC.  If you are working on a WHRE System Prototype board you must download the binary from https://github.com/u-blox/whre-switch-sara-r4-modem-on to the board in order to keep the SARA-R412M modem powered while you do this.

The files are downloaded to SARA-R412M over the AT interface with the standard `AT+UDWNFILE` AT command but employing the special tag `XLWM2M`.  For instance, to download the Lua file `object_freds_thing.lua` you would first find the size of the file in bytes, let's say it is 12367 bytes, and issue the AT command:

```
AT+UDWNFILE="object_freds_thing.lua",12367,"XLWM2M"
```

You should then receive a prompt `>` back from the module for you to download the file, which must be the exact number of bytes indicated.  This can all be done, for instance, using Teraterm, making sure that the "binary" box is ticked in the Teraterm download window.  Re-boot the module with `AT+CFUN=15` and issue `AT+ULWM2MLIST?` (if this returns `ERROR` wait and try again as it takes LWM2M about 9 seconds to start up after the AT interface has become responsive) and the OMA IDs of your objects should appear in the list.

If the file already exists on SARA-R412M, you must delete it first with:

```
AT+UDELFILE="object_freds_thing.lua","XLWM2M"
```
