This is a trivial minimal library intended to act as a proxy for a
dynamically loaded optional libintl. This dynamically loaded library
would be intl.dll (the core DLL of GNU gettext-runtime) on Windows,
libintl.dylib on MacOS, and libintl.so on many embedded (?)
platforms. It is relevant on Windows, MacOS and some embedded
platforms. On Linux you have gettext functionality already in the C
library.

If you compile it with -DSTUB_ONLY the ability to dynamically load
libintl is not compiled in, and this library always acts as just a
dummy.

The STUB_ONLY patch was provided by Geoffrey Wossum, thanks.

proxy-libintl was originally intended to be used when building
software that wants to use i18n features of (GNU) gettext, but one
wants to be able to decide only at package/installer construction time
whether to actually support i18n or not. In the negative case, one
wants to avoid having to ship the gettext DLL (intl.dll) at all. With
the -DSTUB_ONLY possibility one can also use it when one has no
intention to provide even the possibility of localisation in the
binaries one builds against this.

When building Windows DLLs with gcc, if you don't use a .def file and
don't use __declspec(dllexport) attributes either to declare the list
of exported functions, GNU ld will export all global symbols. Usually
this is what you want, as it corresponds closely to what happens on
ELF-based platforms like Linux.

However, when you build such a DLL, let's call it libfoo, against the
static proxy-libintl, this then means that the libintl entry points
will also get exported from the libfoo DLL. This is definitely not
what you want. It might then lead to other DLLs higher up in the
dependency stack to import the libintl functions from the libfoo DLL.

To avoid this, use the --exluce-libs ld flag, i.e. pass
-Wl,--exclude-libs=libintl.a in your LDFLAGS when building
libfoo. Unfortunately there is no __declspec(nodllexport)...

Both a "plain" 32-bit library and a x86_64 one is provided.

Tor Lillqvist <tml@iki.fi>, July 2008
