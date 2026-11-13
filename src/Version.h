#ifndef _VERSION_H_
#define _VERSION_H_

// Set by CMake from the version of the project() command; the fallback is for
// builds that do not go through CMake.
#ifndef IMAGINA_VERSION
#define IMAGINA_VERSION "0.0.0-dev"
#endif

#define IMAGINA_STRINGIFY_(x) #x
#define IMAGINA_STRINGIFY(x) IMAGINA_STRINGIFY_(x)

#endif
