# libteak

## 1. Introduction

This is a library to help develop applications for the DSP that comes with the
DSi and 3DS consoles. It is provided as part of
[BlocksDS](https://blocksds.skylyrac.net), but it can also be used as a
standalone library.

- [Documentation](https://blocksds.skylyrac.net/libteak/index.html)
- [Examples](https://codeberg.org/blocksds/sdk/src/branch/master/examples/dsp)

Please, report issues [here](https://codeberg.org/blocksds/sdk/issues).

## 2. Setup

The Teak toolchain is currently only distributed by
[Wonderful Toolchain](https://wonderful.asie.pl). You will need to install the
Teak toolchain of Wonderful Toolchain. Follow the instructions in this link:
https://wonderful.asie.pl/docs/getting-started/

Note that the Teak toolchain is currently only available on Linux, not Windows.

Now, install the Teak toolchain of Wonderful Toolchain:

```bash
wf-pacman -Syu
wf-pacman -S toolchain-llvm-teak-llvm
```

Finally, build the library:

```bash
make
```

## 3. Editing assembly files

The assembler of the Teak toolchain doesn't support expanding C-style defines
and macros. Using magic numbers in the code is bad practice, so this repository
has a simple python script (in the folder `tools`) that is used to preprocess
assembly files and expand simple expressions.

All files stored in the `asm` folder are preprocessed and saved to the `source`
folder. Both files are present in the repository because that way python 3 isn't
a dependency required to build the library. However, it is required if you want
to modify the assembly files of the library.
