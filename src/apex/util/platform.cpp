/* Copyright 2024 Automated Algo (www.automatedalgo.com)

This file is part of Automated Algo's "Apex" project.

Apex is free software: you can redistribute it and/or modify it under the terms
of the GNU Lesser General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

Apex is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License along
with Apex. If not, see <https://www.gnu.org/licenses/>.
*/

#include <apex/util/platform.hpp>

#include <stdexcept>
#include <thread>

#ifndef _WIN32
#include <sys/syscall.h> /* For SYS_xxx definitions */
#include <sys/utsname.h>
#include <unistd.h>
#include <pwd.h>
#include <cstring>
#include <cassert>
#else
#include <Windows.h>
#ifdef WIN32_LEAN_AND_MEAN
#include <Winsock2.h> /* For gethostname */
#endif
#endif


namespace apex
{

long thread_id()
{
#ifndef _WIN32
  /* On Linux the thread-id returned via syscall is more useful than that C++
   * get_id(), since it will correspond to the values reported by top and other
   * tools. */
  return ::syscall(SYS_gettid);
#else
  return GetCurrentThreadId();
#endif
}


TimeVal time_now()
{
#ifndef _WIN32
  timeval epoch;
  gettimeofday(&epoch, nullptr);
  return {epoch.tv_sec, epoch.tv_usec};
#else
  SYSTEMTIME system_time;
  GetSystemTime(&system_time); // obtain milliseconds
  TimeVal::type_type now;
  time(&now); // seconds elapsed since midnight January 1, 1970
  TimeVal tv_systime{now, system_time.wMilliseconds * 1000};
  return tv_systime;
#endif
}


std::string hostname()
{
#ifndef _WIN32
  struct utsname name;
  if (uname(&name) != 0)
    throw std::runtime_error("uname failed");
  return name.nodename;
#else
  char buf[256];
  gethostname(buf, sizeof(buf));
  temp[sizeof(buf)-1] = '\0';
  return temp;
#endif
}


std::string username()
{
  char buf[64] = {'\0'};

#if(_POSIX_C_SOURCE)
  struct passwd * passwd_ptr = ::getpwuid(::getuid());
  if (passwd_ptr && passwd_ptr->pw_name) {
    strncpy(buf, passwd_ptr->pw_name, sizeof(buf));
    goto end;
  }
#endif

#if (_POSIX_C_SOURCE >= 199506L)
  if (::getlogin_r(buf, sizeof(buf)) == 0)
    goto end;
#endif

#if (_XOPEN_SOURCE && !(_POSIX_C_SOURCE >= 200112L) || _GNU_SOURCE)
  assert(L_cuserid < sizeof(buf));
  ::cuserid(buf);
  goto end;
#endif

end:
  buf[sizeof(buf) - 1] = '\0';
  return buf;
}


} // namespace apex
