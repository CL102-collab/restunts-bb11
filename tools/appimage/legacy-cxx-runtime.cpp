/* Supply the C++11 array-length exception missing from the pinned static runtime.
 * This object is added only to the generated AppImage SDK's libstdc++ archive. */
#include <new>
#include "../../src/restunts/c/legacy.h"

std::bad_array_new_length::~bad_array_new_length() throw()
{
}

const legacy_char *std::bad_array_new_length::what() const throw()
{
	return "std::bad_array_new_length";
}

extern "C" [[noreturn]] void __cxa_throw_bad_array_new_length()
{
	throw std::bad_array_new_length();
}
