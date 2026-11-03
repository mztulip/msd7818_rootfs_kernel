#!/bin/sh
# Buildroot post-build script of the WIWA HD95 mini rootfs: drop what a
# read-only flash system with no network does not need.
# $1 = target directory
set -e
T="$1"
rm -f "$T"/lib/libc.a					# static musl, programs are already linked
rm -f "$T"/etc/init.d/S40network "$T"/etc/init.d/S50crond	# no network in kernel_mini
rm -f "$T"/etc/init.d/S01seedrng			# wants to write /var/lib on a read-only root
