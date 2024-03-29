/******************************************************************************
 * Icinga 2                                                                   *
 * Copyright (C) 2012-2014 Icinga Development Team (http://www.icinga.org)    *
 *                                                                            *
 * This program is free software; you can redistribute it and/or              *
 * modify it under the terms of the GNU General Public License                *
 * as published by the Free Software Foundation; either version 2             *
 * of the License, or (at your option) any later version.                     *
 *                                                                            *
 * This program is distributed in the hope that it will be useful,            *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of             *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the              *
 * GNU General Public License for more details.                               *
 *                                                                            *
 * You should have received a copy of the GNU General Public License          *
 * along with this program; if not, write to the Free Software Foundation     *
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.             *
 ******************************************************************************/

#include "base/utility.h"
#include "base/convert.h"
#include "base/application.h"
#include "base/logger_fwd.h"
#include "base/exception.h"
#include <mmatch.h>
#include <boost/lexical_cast.hpp>
#include <boost/function.hpp>
#include <boost/foreach.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/trim.hpp>

#ifdef __FreeBSD__
#	include <pthread_np.h>
#endif /* __FreeBSD__ */

#ifndef _MSC_VER
#	include <cxxabi.h>
#endif /* _MSC_VER */

using namespace icinga;

boost::thread_specific_ptr<String> Utility::m_ThreadName;
boost::thread_specific_ptr<unsigned int> Utility::m_RandSeed;

/**
 * Demangles a symbol name.
 *
 * @param sym The symbol name.
 * @returns A human-readable version of the symbol name.
 */
String Utility::DemangleSymbolName(const String& sym)
{
	String result = sym;

#ifndef _MSC_VER
	int status;
	char *realname = abi::__cxa_demangle(sym.CStr(), 0, 0, &status);

	if (realname != NULL) {
		result = String(realname);
		free(realname);
	}
#else /* _MSC_VER */
	CHAR output[256];

	if (UnDecorateSymbolName(sym.CStr(), output, sizeof(output), UNDNAME_COMPLETE) > 0)
		result = output;
#endif /* _MSC_VER */

	return result;
}

/**
 * Returns a human-readable type name of a type_info object.
 *
 * @param ti A type_info object.
 * @returns The type name of the object.
 */
String Utility::GetTypeName(const std::type_info& ti)
{
	return DemangleSymbolName(ti.name());
}

/**
 * Looks up source file name and line number information for the specified
 * ELF executable and RVA.
 *
 * @param exe The ELF file.
 * @param rva The RVA.
 * @returns Source file and line number.
 */
String Utility::Addr2Line(const String& exe, uintptr_t rva)
{
#ifndef _WIN32
	std::ostringstream msgbuf;
	msgbuf << "addr2line -s -e " << Application::GetExePath(exe) << " " << std::hex << rva << " 2>/dev/null";

	String args = msgbuf.str();

	FILE *fp = popen(args.CStr(), "r");

	if (!fp)
		return "RVA: " + Convert::ToString(rva);

	char buffer[512] = {};
	fgets(buffer, sizeof(buffer), fp);
	fclose(fp);

	String line = buffer;
	boost::algorithm::trim_right(line);

	if (line.GetLength() == 0)
		return "RVA: " + Convert::ToString(rva);

	return line;
#else /* _WIN32 */
	return String();
#endif /* _WIN32 */
}

String Utility::GetSymbolName(const void *addr)
{
#ifdef HAVE_DLADDR
	Dl_info dli;

	if (dladdr(addr, &dli) > 0)
		return dli.dli_sname;
#endif /* HAVE_DLADDR */

#ifdef _WIN32
	char buffer[sizeof(SYMBOL_INFO)+MAX_SYM_NAME * sizeof(TCHAR)];
	PSYMBOL_INFO pSymbol = (PSYMBOL_INFO)buffer;
	pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
	pSymbol->MaxNameLen = MAX_SYM_NAME;

	DWORD64 dwAddress = (DWORD64)addr;
	DWORD64 dwDisplacement;

	IMAGEHLP_LINE64 line;
	line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

	if (SymFromAddr(GetCurrentProcess(), dwAddress, &dwDisplacement, pSymbol)) {
		char output[256];
		if (UnDecorateSymbolName(pSymbol->Name, output, sizeof(output), UNDNAME_COMPLETE))
			return String(output) + "+" + Convert::ToString(dwDisplacement);
		else
			return String(pSymbol->Name) + "+" + Convert::ToString(dwDisplacement);
	}
#endif /* _WIN32 */

	return "(unknown function)";
}

String Utility::GetSymbolSource(const void *addr)
{
#ifdef HAVE_DLADDR
	Dl_info dli;

	if (dladdr(addr, &dli) > 0) {
		uintptr_t rva = reinterpret_cast<uintptr_t>(addr) - reinterpret_cast<uintptr_t>(dli.dli_fbase);
		return Addr2Line(dli.dli_fname, rva);
	}
#endif /* HAVE_DLADDR */

#ifdef _WIN32
	char buffer[sizeof(SYMBOL_INFO)+MAX_SYM_NAME * sizeof(TCHAR)];
	PSYMBOL_INFO pSymbol = (PSYMBOL_INFO)buffer;
	pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
	pSymbol->MaxNameLen = MAX_SYM_NAME;

	DWORD64 dwAddress = (DWORD64)addr;
	DWORD dwDisplacement;

	IMAGEHLP_LINE64 line;
	line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

	if (SymGetLineFromAddr64(GetCurrentProcess(), dwAddress, &dwDisplacement, &line))
		return String(line.FileName) + ":" + Convert::ToString(line.LineNumber);
#endif /* _WIN32 */

	return "(unknown file/line)";
}

/**
 * Performs wildcard pattern matching.
 *
 * @param pattern The wildcard pattern.
 * @param text The String that should be checked.
 * @returns true if the wildcard pattern matches, false otherwise.
 */
bool Utility::Match(const String& pattern, const String& text)
{
	return (match(pattern.CStr(), text.CStr()) == 0);
}

/**
 * Returns the directory component of a path. See dirname(3) for details.
 *
 * @param path The full path.
 * @returns The directory.
 */
String Utility::DirName(const String& path)
{
	char *dir;

#ifdef _WIN32
	String dupPath = path;

	/* PathRemoveFileSpec doesn't properly handle forward slashes. */
	BOOST_FOREACH(char& ch, dupPath) {
		if (ch == '/')
			ch = '\\';
	}

	dir = strdup(dupPath.CStr());
#else /* _WIN32 */
	dir = strdup(path.CStr());
#endif /* _WIN32 */

	if (dir == NULL)
		BOOST_THROW_EXCEPTION(std::bad_alloc());

	String result;

#ifndef _WIN32
	result = dirname(dir);
#else /* _WIN32 */
	if (!PathRemoveFileSpec(dir)) {
		free(dir);

		BOOST_THROW_EXCEPTION(win32_error()
		    << boost::errinfo_api_function("PathRemoveFileSpec")
		    << errinfo_win32_error(GetLastError()));
	}

	result = dir;

	if (result.IsEmpty())
		result = ".";
#endif /* _WIN32 */

	free(dir);

	return result;
}

/**
 * Returns the file component of a path. See basename(3) for details.
 *
 * @param path The full path.
 * @returns The filename.
 */
String Utility::BaseName(const String& path)
{
	char *dir = strdup(path.CStr());
	String result;

	if (dir == NULL)
		BOOST_THROW_EXCEPTION(std::bad_alloc());

#ifndef _WIN32
	result = basename(dir);
#else /* _WIN32 */
	result = PathFindFileName(dir);
#endif /* _WIN32 */

	free(dir);

	return result;
}

/**
 * Null deleter. Used as a parameter for the shared_ptr constructor.
 *
 * @param - The object that should be deleted.
 */
void Utility::NullDeleter(void *)
{
	/* Nothing to do here. */
}

/**
 * Returns the current UNIX timestamp including fractions of seconds.
 *
 * @returns The current time.
 */
double Utility::GetTime(void)
{
#ifdef _WIN32
	FILETIME cft;
	GetSystemTimeAsFileTime(&cft);

	ULARGE_INTEGER ucft;
	ucft.HighPart = cft.dwHighDateTime;
	ucft.LowPart = cft.dwLowDateTime;

	SYSTEMTIME est = { 1970, 1, 4, 1, 0, 0, 0, 0};
	FILETIME eft;
	SystemTimeToFileTime(&est, &eft);

	ULARGE_INTEGER ueft;
	ueft.HighPart = eft.dwHighDateTime;
	ueft.LowPart = eft.dwLowDateTime;

	return ((ucft.QuadPart - ueft.QuadPart) / 10000) / 1000.0;
#else /* _WIN32 */
	struct timeval tv;

	if (gettimeofday(&tv, NULL) < 0) {
		BOOST_THROW_EXCEPTION(posix_error()
		    << boost::errinfo_api_function("gettimeofday")
		    << boost::errinfo_errno(errno));
	}

	return tv.tv_sec + tv.tv_usec / 1000000.0;
#endif /* _WIN32 */
}

/**
 * Returns the ID of the current process.
 *
 * @returns The PID.
 */
pid_t Utility::GetPid(void)
{
#ifndef _WIN32
	return getpid();
#else /* _WIN32 */
	return GetCurrentProcessId();
#endif /* _WIN32 */
}

/**
 * Sleeps for the specified amount of time.
 *
 * @param timeout The timeout in seconds.
 */
void Utility::Sleep(double timeout)
{
#ifndef _WIN32
	usleep(timeout * 1000 * 1000);
#else /* _WIN32 */
	::Sleep(timeout * 1000);
#endif /* _WIN32 */
}

/**
 * Loads the specified library.
 *
 * @param library The name of the library.
 */
#ifdef _WIN32
HMODULE
#else /* _WIN32 */
void *
#endif /* _WIN32 */
Utility::LoadExtensionLibrary(const String& library)
{
	String path;
#if defined(_WIN32)
	path = library + ".dll";
#elif defined(__APPLE__)
	path = "lib" + library + ".dylib";
#else /* __APPLE__ */
	path = "lib" + library + ".so";
#endif /* _WIN32 */

	Log(LogInformation, "base", "Loading library '" + path + "'");

#ifdef _WIN32
	HMODULE hModule = LoadLibrary(path.CStr());

	if (hModule == NULL) {
		BOOST_THROW_EXCEPTION(win32_error()
		    << boost::errinfo_api_function("LoadLibrary")
		    << errinfo_win32_error(GetLastError())
		    << boost::errinfo_file_name(path));
	}
#else /* _WIN32 */
	void *hModule = dlopen(path.CStr(), RTLD_NOW);

	if (hModule == NULL) {
		BOOST_THROW_EXCEPTION(std::runtime_error("Could not load library '" + path + "': " + dlerror()));
	}
#endif /* _WIN32 */

	return hModule;
}

/**
 * Generates a new unique ID.
 *
 * @returns The new unique ID.
 */
String Utility::NewUniqueID(void)
{
	static boost::mutex mutex;
	static int next_id = 0;

	/* I'd much rather use UUIDs but RHEL is way too cool to have
	 * a semi-recent version of boost. Yay. */

	String id;

	char buf[128];
	if (gethostname(buf, sizeof(buf)) == 0)
		id = String(buf) + "-";

	id += Convert::ToString((long)Utility::GetTime()) + "-";

	{
		boost::mutex::scoped_lock lock(mutex);
		id += Convert::ToString(next_id);
		next_id++;
	}

	return id;
}

/**
 * Calls the specified callback for each file matching the path specification.
 *
 * @param pathSpec The path specification.
 * @param callback The callback which is invoked for each matching file.
 * @param type The file type (a combination of GlobFile and GlobDirectory)
 */
bool Utility::Glob(const String& pathSpec, const boost::function<void (const String&)>& callback, int type)
{
#ifdef _WIN32
	HANDLE handle;
	WIN32_FIND_DATA wfd;

	handle = FindFirstFile(pathSpec.CStr(), &wfd);

	if (handle == INVALID_HANDLE_VALUE) {
		DWORD errorCode = GetLastError();

		if (errorCode == ERROR_FILE_NOT_FOUND)
			return false;

		BOOST_THROW_EXCEPTION(win32_error()
		    << boost::errinfo_api_function("FindFirstFile")
			<< errinfo_win32_error(errorCode)
		    << boost::errinfo_file_name(pathSpec));
	}

	do {
		if (strcmp(wfd.cFileName, ".") == 0 || strcmp(wfd.cFileName, "..") == 0)
			continue;

		if ((wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(type & GlobDirectory))
			continue;

		if (!(wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(type & GlobFile))
			continue;

		callback(DirName(pathSpec) + "/" + wfd.cFileName);
	} while (FindNextFile(handle, &wfd));

	if (!FindClose(handle)) {
		BOOST_THROW_EXCEPTION(win32_error()
		    << boost::errinfo_api_function("FindClose")
		    << errinfo_win32_error(GetLastError()));
	}

	return true;
#else /* _WIN32 */
	glob_t gr;

	int rc = glob(pathSpec.CStr(), GLOB_ERR | GLOB_NOSORT, NULL, &gr);

	if (rc < 0) {
		if (rc == GLOB_NOMATCH)
			return false;

		BOOST_THROW_EXCEPTION(posix_error()
		    << boost::errinfo_api_function("glob")
		    << boost::errinfo_errno(errno)
		    << boost::errinfo_file_name(pathSpec));
	}

	if (gr.gl_pathc == 0) {
		globfree(&gr);
		return false;
	}

	size_t left;
	char **gp;
	for (gp = gr.gl_pathv, left = gr.gl_pathc; left > 0; gp++, left--) {
		struct stat statbuf;

		if (stat(*gp, &statbuf) < 0)
			BOOST_THROW_EXCEPTION(posix_error()
			    << boost::errinfo_api_function("stat")
			    << boost::errinfo_errno(errno));

		if (!S_ISDIR(statbuf.st_mode) && !S_ISREG(statbuf.st_mode))
			continue;

		if (S_ISDIR(statbuf.st_mode) && !(type & GlobDirectory))
			continue;

		if (!S_ISDIR(statbuf.st_mode) && !(type & GlobFile))
			continue;

		callback(*gp);
	}

	globfree(&gr);

	return true;
#endif /* _WIN32 */
}

/**
 * Calls the specified callback for each file in the specified directory
 * or any of its child directories if the file name matches the specified
 * pattern.
 *
 * @param path The path.
 * @param pattern The pattern.
 * @param callback The callback which is invoked for each matching file.
 * @param type The file type (a combination of GlobFile and GlobDirectory)
 */
bool Utility::GlobRecursive(const String& path, const String& pattern, const boost::function<void (const String&)>& callback, int type)
{
#ifdef _WIN32
	HANDLE handle;
	WIN32_FIND_DATA wfd;

	String pathSpec = path + "/*";

	handle = FindFirstFile(pathSpec.CStr(), &wfd);

	if (handle == INVALID_HANDLE_VALUE) {
		DWORD errorCode = GetLastError();

		if (errorCode == ERROR_FILE_NOT_FOUND)
			return false;

		BOOST_THROW_EXCEPTION(win32_error()
		    << boost::errinfo_api_function("FindFirstFile")
			<< errinfo_win32_error(errorCode)
		    << boost::errinfo_file_name(pathSpec));
	}

	do {
		if (strcmp(wfd.cFileName, ".") == 0 || strcmp(wfd.cFileName, "..") == 0)
			continue;

		String cpath = path + "/" + wfd.cFileName;

		if (wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			GlobRecursive(cpath, pattern, callback, type);

		if ((wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(type & GlobDirectory))
			continue;

		if (!(wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(type & GlobFile))
			continue;

		if (!Utility::Match(pattern, wfd.cFileName))
			continue;

		callback(cpath);
	} while (FindNextFile(handle, &wfd));

	if (!FindClose(handle)) {
		BOOST_THROW_EXCEPTION(win32_error()
		    << boost::errinfo_api_function("FindClose")
		    << errinfo_win32_error(GetLastError()));
	}
#else /* _WIN32 */
	DIR *dirp;

	dirp = opendir(path.CStr());

	if (dirp == NULL)
		BOOST_THROW_EXCEPTION(posix_error()
		    << boost::errinfo_api_function("opendir")
		    << boost::errinfo_errno(errno)
		    << boost::errinfo_file_name(path));

	while (dirp) {
		dirent ent, *pent;

		if (readdir_r(dirp, &ent, &pent) < 0) {
			closedir(dirp);

			BOOST_THROW_EXCEPTION(posix_error()
			    << boost::errinfo_api_function("readdir_r")
			    << boost::errinfo_errno(errno)
			    << boost::errinfo_file_name(path));
		}

		if (!pent)
			break;

		if (strcmp(ent.d_name, ".") == 0 || strcmp(ent.d_name, "..") == 0)
			continue;

		String cpath = path + "/" + ent.d_name;

		struct stat statbuf;

		if (lstat(cpath.CStr(), &statbuf) < 0) {
			closedir(dirp);

			BOOST_THROW_EXCEPTION(posix_error()
			    << boost::errinfo_api_function("lstat")
			    << boost::errinfo_errno(errno)
			    << boost::errinfo_file_name(cpath));
		}

		if (S_ISDIR(statbuf.st_mode))
			GlobRecursive(cpath, pattern, callback, type);

		if (stat(cpath.CStr(), &statbuf) < 0) {
			closedir(dirp);

			BOOST_THROW_EXCEPTION(posix_error()
			    << boost::errinfo_api_function("stat")
			    << boost::errinfo_errno(errno)
			    << boost::errinfo_file_name(cpath));
		}

		if (!S_ISDIR(statbuf.st_mode) && !S_ISREG(statbuf.st_mode))
			continue;

		if (S_ISDIR(statbuf.st_mode) && !(type & GlobDirectory))
			continue;

		if (!S_ISDIR(statbuf.st_mode) && !(type & GlobFile))
			continue;

		if (!Utility::Match(pattern, ent.d_name))
			continue;

		callback(cpath);
	}

	closedir(dirp);
#endif /* _WIN32 */

	return true;
}

#ifndef _WIN32
void Utility::SetNonBlocking(int fd)
{
	int flags = fcntl(fd, F_GETFL, 0);

	if (flags < 0) {
		BOOST_THROW_EXCEPTION(posix_error()
		    << boost::errinfo_api_function("fcntl")
		    << boost::errinfo_errno(errno));
	}

	if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
		BOOST_THROW_EXCEPTION(posix_error()
		    << boost::errinfo_api_function("fcntl")
		    << boost::errinfo_errno(errno));
	}
}

void Utility::SetCloExec(int fd)
{
	int flags = fcntl(fd, F_GETFD, 0);

	if (flags < 0) {
		BOOST_THROW_EXCEPTION(posix_error()
		    << boost::errinfo_api_function("fcntl")
		    << boost::errinfo_errno(errno));
	}

	if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0) {
		BOOST_THROW_EXCEPTION(posix_error()
		    << boost::errinfo_api_function("fcntl")
		    << boost::errinfo_errno(errno));
	}
}
#endif /* _WIN32 */

void Utility::SetNonBlockingSocket(SOCKET s)
{
#ifndef _WIN32
	SetNonBlocking(s);
#else /* _WIN32 */
	unsigned long lTrue = 1;
	ioctlsocket(s, FIONBIO, &lTrue);
#endif /* _WIN32 */
}

void Utility::QueueAsyncCallback(const boost::function<void (void)>& callback)
{
	Application::GetTP().Post(callback);
}

String Utility::NaturalJoin(const std::vector<String>& tokens)
{
	String result;

	for (int i = 0; i < tokens.size(); i++) {
		result += tokens[i];

		if (tokens.size() > i + 1) {
			if (i < tokens.size() - 2)
				result += ", ";
			else if (i == tokens.size() - 2)
				result += " and ";
		}
	}

	return result;
}

String Utility::FormatDuration(int duration)
{
	std::vector<String> tokens;
	String result;

	if (duration >= 86400) {
		int days = duration / 86400;
		tokens.push_back(Convert::ToString(days) + (days != 1 ? " days" : " day"));
		duration %= 86400;
	}

	if (duration >= 3600) {
		int hours = duration / 3600;
		tokens.push_back(Convert::ToString(hours) + (hours != 1 ? " hours" : " hour"));
		duration %= 3600;
	}

	if (duration >= 60) {
		int minutes = duration / 60;
		tokens.push_back(Convert::ToString(minutes) + (minutes != 1 ? " minutes" : " minute"));
		duration %= 60;
	}

	if (duration >= 1) {
		int seconds = duration;
		tokens.push_back(Convert::ToString(seconds) + (seconds != 1 ? " seconds" : " second"));
	}

	if (tokens.size() == 0) {
		int milliseconds = floor(duration * 1000);
		if (milliseconds >= 1)
			tokens.push_back(Convert::ToString(milliseconds) + (milliseconds != 1 ? " milliseconds" : " millisecond"));
		else
			tokens.push_back("less than 1 millisecond");
	}

	return NaturalJoin(tokens);
}

String Utility::FormatDateTime(const char *format, double ts)
{
	char timestamp[128];
	time_t tempts = (time_t)ts; /* We don't handle sub-second timestamps here just yet. */
	tm tmthen;

#ifdef _MSC_VER
	tm *temp = localtime(&tempts);

	if (temp == NULL) {
		BOOST_THROW_EXCEPTION(posix_error()
		    << boost::errinfo_api_function("localtime")
		    << boost::errinfo_errno(errno));
	}

	tmthen = *temp;
#else /* _MSC_VER */
	if (localtime_r(&tempts, &tmthen) == NULL) {
		BOOST_THROW_EXCEPTION(posix_error()
		    << boost::errinfo_api_function("localtime_r")
		    << boost::errinfo_errno(errno));
	}
#endif /* _MSC_VER */

	strftime(timestamp, sizeof(timestamp), format, &tmthen);

	return timestamp;
}

String Utility::EscapeShellCmd(const String& s)
{
	String result;
	size_t prev_quote = String::NPos;
	int index = -1;

	BOOST_FOREACH(char ch, s) {
		bool escape = false;

		index++;

#ifdef _WIN32
		if (ch == '%' || ch == '"' || ch == '\'')
			escape = true;
#else /* _WIN32 */
		if (ch == '"' || ch == '\'') {
			/* Find a matching closing quotation character. */
			if (prev_quote == String::NPos && (prev_quote = s.FindFirstOf(ch, index + 1)) != String::NPos)
				; /* Empty statement. */
			else if (prev_quote != String::NPos && s[prev_quote] == ch)
				prev_quote = String::NPos;
			else
				escape = true;
		}
#endif /* _WIN32 */

		if (ch == '#' || ch == '&' || ch == ';' || ch == '`' || ch == '|' ||
		    ch == '*' || ch == '?' || ch == '~' || ch == '<' || ch == '>' ||
		    ch == '^' || ch == '(' || ch == ')' || ch == '[' || ch == ']' ||
		    ch == '{' || ch == '}' || ch == '$' || ch == '\\' || ch == '\x0A' ||
		    ch == '\xFF')
			escape = true;

		if (escape)
#ifdef _WIN32
			result += '^';
#else /* _WIN32 */
			result += '\\';
#endif /* _WIN32 */

		result += ch;
	}

	return result;
}

String Utility::EscapeShellArg(const String& s)
{
	String result;

#ifdef _WIN32
	result = "\"";
#else /* _WIN32 */
	result = "'";
#endif /* _WIN32 */

	BOOST_FOREACH(char ch, s) {
#ifdef _WIN32
		if (ch == '"' || ch == '%') {
			result += ' ';
		}
#else /* _WIN32 */
		if (ch == '\'')
			result += "'\\'";
#endif
		result += ch;
	}

#ifdef _WIN32
	result += '"';
#else /* _WIN32 */
	result += '\'';
#endif /* _WIN32 */

	return result;
}

#ifdef _WIN32
static void WindowsSetThreadName(const char *name)
{
	THREADNAME_INFO info;
	info.dwType = 0x1000;
	info.szName = name;
	info.dwThreadID = -1;
	info.dwFlags = 0;

	__try {
		RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(ULONG_PTR), (ULONG_PTR *)&info);
	} __except(EXCEPTION_EXECUTE_HANDLER) {
		/* Nothing to do here. */
	}
}
#endif /* _WIN32 */

void Utility::SetThreadName(const String& name, bool os)
{
	m_ThreadName.reset(new String(name));

	if (!os)
		return;

#ifdef _WIN32
	WindowsSetThreadName(name.CStr());
#endif /* _WIN32 */

#ifdef HAVE_PTHREAD_SET_NAME_NP
	pthread_set_name_np(pthread_self(), name.CStr());
#endif /* HAVE_PTHREAD_SET_NAME_NP */

#ifdef HAVE_PTHREAD_SETNAME_NP
#	ifdef __APPLE__
	pthread_setname_np(name.CStr());
#	else /* __APPLE__ */
	String tname = name.SubStr(0, 15);
	pthread_setname_np(pthread_self(), tname.CStr());
#	endif /* __APPLE__ */
#endif /* HAVE_PTHREAD_SETNAME_NP */
}

String Utility::GetThreadName(void)
{
	String *name = m_ThreadName.get();

	if (!name) {
		std::ostringstream idbuf;
		idbuf << boost::this_thread::get_id();
		return idbuf.str();
	}

	return *name;
}

unsigned long Utility::SDBM(const String& str, size_t len)
{
	unsigned long hash = 0;
	size_t current = 0;

	BOOST_FOREACH(char c, str) {
		if (current >= len)
			break;

		hash = c + (hash << 6) + (hash << 16) - hash;

		current++;
	}

	return hash;
}

int Utility::CompareVersion(const String& v1, const String& v2)
{
	std::vector<String> tokensv1, tokensv2;
	boost::algorithm::split(tokensv1, v1, boost::is_any_of("."));
	boost::algorithm::split(tokensv2, v2, boost::is_any_of("."));

	for (int i = 0; i < tokensv2.size() - tokensv1.size(); i++)
		tokensv1.push_back("0");

	for (int i = 0; i < tokensv1.size() - tokensv2.size(); i++)
		tokensv2.push_back("0");

	for (size_t i = 0; i < tokensv1.size(); i++) {
		if (Convert::ToLong(tokensv2[i]) > Convert::ToLong(tokensv1[i]))
			return 1;
		else if (Convert::ToLong(tokensv2[i]) < Convert::ToLong(tokensv1[i]))
			return -1;
	}

	return 0;
}

String Utility::GetHostName(void)
{
	char name[255];

	if (gethostname(name, sizeof(name)) < 0)
		strcpy(name, "<unknown host>");

	return String(name);
}

int Utility::Random(void)
{
#ifdef _WIN32
	return rand();
#else /* _WIN32 */
	unsigned int *seed = m_RandSeed.get();

	if (!seed) {
		seed = new unsigned int(Utility::GetTime());
		m_RandSeed.reset(seed);
	}

	return rand_r(seed);
#endif /* _WIN32 */
}

tm Utility::LocalTime(time_t ts)
{
#ifdef _MSC_VER
	tm *result = localtime(&ts);

	if (result == NULL) {
		BOOST_THROW_EXCEPTION(posix_error()
		    << boost::errinfo_api_function("localtime")
		    << boost::errinfo_errno(errno));
	}

	return *result;
#else /* _MSC_VER */
	tm result;

	if (localtime_r(&ts, &result) == NULL) {
		BOOST_THROW_EXCEPTION(posix_error()
		    << boost::errinfo_api_function("localtime_r")
		    << boost::errinfo_errno(errno));
	}

	return result;
#endif /* _MSC_VER */
}
