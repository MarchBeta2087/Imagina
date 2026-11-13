#ifndef _GMP_COMPAT_H_
#define _GMP_COMPAT_H_

// MPIR is a fork of GMP and drops in for everything this project uses (the .im
// file format stores the raw numbers itself, see File.cpp), so GMP is used when
// MPIR is not installed.  Distributions ship GMP, MPIR has to be built by hand.
#if __has_include(<mpir.h>)
#include <mpir.h>
#else
#include <gmp.h>
#endif

// MPIR defines __MPIR_VERSION in mpir.h; keying on that (and not on the header
// name, which a compatibility shim can also provide) decides whether the C++
// stream operator below is needed.
#if defined(__MPIR_VERSION) && __has_include(<mpirxx.h>)
#include <mpirxx.h>
#else
#include <gmpxx.h>

#include <cstring>
#include <limits>
#include <ostream>

// GMP's stream operator for the expression templates forwards to the C struct,
// but only MPIR declares the overload for it.  Format the value the way the
// stream asks for (fixed or general) instead.
inline std::ostream &operator<<(std::ostream &Stream, mpf_srcptr Value) {
	int Precision = int(Stream.precision());
	bool Fixed = (Stream.flags() & std::ios_base::floatfield) == std::ios_base::fixed;
	char *Text = nullptr;
	gmp_asprintf(&Text, Fixed ? "%.*Ff" : "%.*Fg", Precision, Value);
	Stream << Text;
	void (*Free)(void *, size_t) = nullptr;
	mp_get_memory_functions(nullptr, nullptr, &Free);
	if (Free) Free(Text, std::strlen(Text) + 1);
	return Stream;
}
#endif

#endif
