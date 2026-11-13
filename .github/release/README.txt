latte-c @VERSION@ for Windows (@ARCH@)

Contents
  include\latte\latte.h, latte.hpp   public headers
  bin\latte.dll                      shared library
  lib\latte.lib                      import library for latte.dll
  lib\static\latte_mt.lib            static library, built with /MT
  lib\static\latte_md.lib            static library, built with /MD
  licenses\                          licences of the bundled third-party code

Which library to link
  Use the DLL:            link lib\latte.lib and ship bin\latte.dll next to your
                          executable. latte.dll has no dependency on the Visual C++
                          runtime and needs no defines.
  Static, /MT runtime:    link lib\static\latte_mt.lib
  Static, /MD runtime:    link lib\static\latte_md.lib
  Pick the static library that matches the /MT or /MD setting of your project.

System libraries a static link needs
  winhttp.lib advapi32.lib shell32.lib

32-bit (x86)
  The functions use the cdecl calling convention.

Documentation
  https://github.com/licenselatte/latte-c#readme
