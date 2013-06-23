//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

/** Add this to get the @ref ripple_basics module.

    @file ripple_basics.cpp
    @ingroup ripple_basics
*/

#include "ripple_basics.h"

#include <fstream>
#include <iostream>

// VFALCO TODO Rewrite Sustain to use beast::Process
//
// These are for Sustain Linux variants
#ifdef __linux__
#include <sys/types.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#endif
#ifdef __FreeBSD__
#include <sys/types.h>
#include <sys/wait.h>
#endif

#include <boost/algorithm/string.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/asio.hpp> // VFALCO NOTE just for parseIpPort (!)
#include <boost/regex.hpp>

// VFALCO TODO Replace OpenSSL randomness with a dependency-free implementation
//         Perhaps Schneier's Fortuna or a variant. Abstract the collection of
//         entropy and provide OS-specific implementation. We can re-use the
//         BearShare source code for this.
//
//         Add Random number generation to the new VFLib
//
#include <openssl/rand.h> // Because of ripple_RandomNumbers.cpp

#ifdef BEAST_WIN32
#include <windows.h>  // for ripple_RandomNumbers.cpp
#include <wincrypt.h> // for ripple_RandomNumbers.cpp
// Winsock #defines 'max' and does other stupid things so put it last
#include <Winsock2.h> // for ripple_ByteOrder.cpp
#endif

#if RIPPLE_USE_NAMESPACE
namespace ripple
{
#endif

#include "containers/ripple_RangeSet.cpp"
#include "containers/ripple_TaggedCache.cpp"

#include "utility/ripple_Log.cpp"

#include "utility/ripple_ByteOrder.cpp"
#include "utility/ripple_CountedObject.cpp"
#include "utility/ripple_DiffieHellmanUtil.cpp"
#include "utility/ripple_IniFile.cpp"
#include "utility/ripple_StringUtilities.cpp"
#include "utility/ripple_Sustain.cpp"
#include "utility/ripple_ThreadName.cpp"
#include "utility/ripple_Time.cpp"
#include "utility/ripple_UptimeTimer.cpp"

#include "utility/ripple_RandomNumbers.cpp" // has Win32/Posix dependencies

#include "types/ripple_UInt256.cpp"

#if RIPPLE_USE_NAMESPACE
}
#endif