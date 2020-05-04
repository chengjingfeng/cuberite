
// Logging free functions defined in Logger.cpp
#pragma once

// python style format specified logging

extern void vFLOG(const char * a_Format, fmt::format_args a_ArgList);
template <typename... Args>
void FLOG(const char * a_Format, const Args & ... a_Args)
{
	vFLOG(a_Format, fmt::make_format_args(a_Args...));
}

extern void vFLOGINFO(const char * a_Format, fmt::format_args a_ArgList);
template <typename... Args>
void FLOGINFO(const char * a_Format, const Args & ... a_Args)
{
	vFLOGINFO(a_Format, fmt::make_format_args(a_Args...));
}

extern void vFLOGWARNING(const char * a_Format, fmt::format_args a_ArgList);
template <typename... Args>
void FLOGWARNING(const char * a_Format, const Args & ... a_Args)
{
	vFLOGWARNING(a_Format, fmt::make_format_args(a_Args...));
}

extern void vFLOGERROR(const char * a_Format, fmt::format_args a_ArgList);
template <typename... Args>
void FLOGERROR(const char * a_Format, const Args & ... a_Args)
{
	vFLOGERROR(a_Format, fmt::make_format_args(a_Args...));
}

// printf style format specified logging (DEPRECATED)

extern void vLOG(const char * a_Format, fmt::printf_args a_ArgList);
template <typename... Args>
void LOG(const char * a_Format, const Args & ... a_Args)
{
	vLOG(a_Format, fmt::make_printf_args(a_Args...));
}

extern void vLOGINFO(const char * a_Format, fmt::printf_args a_ArgList);
template <typename... Args>
void LOGINFO(const char * a_Format, const Args & ... a_Args)
{
	vLOGINFO(a_Format, fmt::make_printf_args(a_Args...));
}

extern void vLOGWARNING(const char * a_Format, fmt::printf_args a_ArgList);
template <typename... Args>
void LOGWARNING(const char * a_Format, const Args & ... a_Args)
{
	vLOGWARNING(a_Format, fmt::make_printf_args(a_Args...));
}

extern void vLOGERROR(const char * a_Format, fmt::printf_args a_ArgList);
template <typename... Args>
void LOGERROR(const char * a_Format, const Args & ... a_Args)
{
	vLOGERROR(a_Format, fmt::make_printf_args(a_Args...));
}


// Macro variants

// In debug builds, translate LOGD to LOG, otherwise leave it out altogether:
#ifdef _DEBUG
	#define LOGD LOG
#else
	#define LOGD(...)
#endif  // _DEBUG

#define LOGWARN LOGWARNING

#ifdef _DEBUG
	#define FLOGD FLOG
#else
	#define FLOGD(...)
#endif  // _DEBUG

#define FLOGWARN FLOGWARNING
