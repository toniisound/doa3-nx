/* SPDX-License-Identifier: GPL-3.0-or-later
   Force-included into every C file of the Switch build. The NRO is one
   statically linked image, but devkitA64 compiles with -fPIC, so every global
   (the guest registers in recomp_runtime, guest RAM...) was reached through a
   GOT load. Hidden visibility lets the compiler address them directly. */
#pragma GCC visibility push(hidden)
