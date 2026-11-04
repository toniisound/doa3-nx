#ifndef RECOMP_DOA3_BUILD_H
#define RECOMP_DOA3_BUILD_H

/* A guest address that differs between DOA3 USA (1.0) and Japan 3.1: the
   value for the lifted XBE. Japan values come from the Version Tracking map;
   see docs/doa3-jp-test.md. */
#ifdef RECOMP_DOA3_JP_BINDINGS
#define DOA3(usa, japan) (japan)
#else
#define DOA3(usa, japan) (usa)
#endif

#endif
