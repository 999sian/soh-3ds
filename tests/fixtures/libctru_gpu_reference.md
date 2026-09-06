# libctru GPU command reference

libctru_gpu_reference.c is an unchanged copy of libctru/source/gpu/gpu.c from devkitPro/libctru revision 36fe1ada5b7ebe53ba4decda36d764a55f8fefb6. The original authors are the libctru contributors. The accompanying libctru_LICENSE reproduces the license notice from that revision's README.

Source: https://github.com/devkitPro/libctru/blob/36fe1ada5b7ebe53ba4decda36d764a55f8fefb6/libctru/source/gpu/gpu.c

The test extracts command functions into a temporary translation unit and adds a general-writer entry counter for structural testing. It replaces the hardware panic service with longjmp for host boundary checks. Its benchmark uses a separate translation unit with the counter removed. No source from this fixture is linked into the game.
