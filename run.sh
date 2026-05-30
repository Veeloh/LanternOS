#!/usr/bin/env bash
set -e
make && qemu-system-i386 -fda build/main_floppy.img
