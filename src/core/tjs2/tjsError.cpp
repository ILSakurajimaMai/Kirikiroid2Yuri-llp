//---------------------------------------------------------------------------
/*
	TJS2 Script Engine
	Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

	See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// TJS2's C++ exception class and exception message
//---------------------------------------------------------------------------
#include "tjsCommHead.h"


#include "tjsScriptBlock.h"
#include "tjsError.h"
#include "tjs.h"

#define TJS_MAX_TRACE_TEXT_LEN 1500

#if defined(__ANDROID__)
#include <android/log.h>
#include <unwind.h>
#include <dlfcn.h>
#include <cxxabi.h>
#include <exception>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#endif

namespace TJS
{

//---------------------------------------------------------------------------
// llp7 diagnostics : last eTJSError + backtrace, reported by std::terminate
//---------------------------------------------------------------------------
#if defined(__ANDROID__)
namespace {
const int kDiagMaxFrames = 40;
struct tDiagTrace { void *pcs[kDiagMaxFrames]; int count; };

_Unwind_Reason_Code DiagUnwindCallback(struct _Unwind_Context *ctx, void *arg)
{
	tDiagTrace *t = static_cast<tDiagTrace *>(arg);
	uintptr_t pc = _Unwind_GetIP(ctx);
	if(pc)
	{
		if(t->count >= kDiagMaxFrames) return _URC_END_OF_STACK;
		t->pcs[t->count++] = reinterpret_cast<void *>(pc);
	}
	return _URC_NO_REASON;
}

void DiagCapture(tDiagTrace &t)
{
	t.count = 0;
	_Unwind_Backtrace(DiagUnwindCallback, &t);
}

void DiagDump(const char *title, const tDiagTrace &t)
{
	for(int i = 0; i < t.count; i++)
	{
		Dl_info info;
		const char *lib = "?";
		const char *sym = "?";
		char *dem = nullptr;
		uintptr_t pc = reinterpret_cast<uintptr_t>(t.pcs[i]);
		uintptr_t off = 0, rel = 0;
		if(dladdr(t.pcs[i], &info))
		{
			if(info.dli_fname)
			{
				const char *slash = strrchr(info.dli_fname, '/');
				lib = slash ? slash + 1 : info.dli_fname;
			}
			rel = pc - reinterpret_cast<uintptr_t>(info.dli_fbase);
			if(info.dli_sname)
			{
				int st = 0;
				dem = abi::__cxa_demangle(info.dli_sname, nullptr, nullptr, &st);
				sym = dem ? dem : info.dli_sname;
				off = pc - reinterpret_cast<uintptr_t>(info.dli_saddr);
			}
		}
		__android_log_print(ANDROID_LOG_ERROR, "krkr2-diag",
			"%s #%02d %s+0x%zx %s+0x%zx", title, i, lib, (size_t)rel, sym, (size_t)off);
		free(dem);
	}
}

thread_local char g_DiagLastMsg[1024];
thread_local tDiagTrace g_DiagLastTrace;
thread_local int g_DiagLogged = 0;

void DiagTerminate()
{
	__android_log_print(ANDROID_LOG_FATAL, "krkr2-diag", "std::terminate called");
	if(std::exception_ptr p = std::current_exception())
	{
		try { std::rethrow_exception(p); }
		catch(const eTJS &e)
		{
			__android_log_print(ANDROID_LOG_FATAL, "krkr2-diag",
				"escaped eTJS message: %s", e.GetMessage().AsStdString().c_str());
		}
		catch(const std::exception &e)
		{
			__android_log_print(ANDROID_LOG_FATAL, "krkr2-diag",
				"escaped std::exception: %s", e.what());
		}
		catch(...)
		{
			__android_log_print(ANDROID_LOG_FATAL, "krkr2-diag", "escaped unknown exception");
		}
	}
	TJSDiagDumpLastError("terminate");
	tDiagTrace now;
	DiagCapture(now);
	DiagDump("terminate-site", now);
	abort();
}

struct tDiagInstaller
{
	tDiagInstaller() { std::set_terminate(DiagTerminate); }
} g_DiagInstaller;
} // namespace

void TJSDiagOnErrorCreated(const ttstr & msg)
{
	std::string s = msg.AsStdString();
	strncpy(g_DiagLastMsg, s.c_str(), sizeof(g_DiagLastMsg) - 1);
	g_DiagLastMsg[sizeof(g_DiagLastMsg) - 1] = 0;
	DiagCapture(g_DiagLastTrace);
	if(g_DiagLogged < 4000)
	{
		g_DiagLogged++;
		__android_log_print(ANDROID_LOG_WARN, "krkr2-diag", "eTJSError created: %s", g_DiagLastMsg);
	}
}

void TJSDiagDumpLastError(const char *reason)
{
	__android_log_print(ANDROID_LOG_ERROR, "krkr2-diag",
		"[%s] last eTJSError: %s", reason, g_DiagLastMsg);
	DiagDump("throw-site", g_DiagLastTrace);
}
#else
void TJSDiagOnErrorCreated(const ttstr &) {}
void TJSDiagDumpLastError(const char *) {}
#endif

//---------------------------------------------------------------------------
// TJSGetExceptionObject : retrieves TJS 'Exception' object
//---------------------------------------------------------------------------
void TJSGetExceptionObject(tTJS *tjs, tTJSVariant *res, tTJSVariant &msg,
	tTJSVariant *trace/* trace is optional */)
{
	if(!res) return; // not prcess

	// retrieve class "Exception" from global
	iTJSDispatch2 *global = tjs->GetGlobalNoAddRef();
	tTJSVariant val;
	static tTJSString Exception_name(TJS_W("Exception"));
	tjs_error hr = global->PropGet(0, Exception_name.c_str(),
		Exception_name.GetHint(), &val, global);
	if(TJS_FAILED(hr)) TJS_eTJSError(TJSExceptionNotFound);
	// create an Exception object
	iTJSDispatch2 *excpobj;
	tTJSVariantClosure clo = val.AsObjectClosureNoAddRef();
	tTJSVariant *pmsg = &msg;
	hr = clo.CreateNew(0, NULL, NULL, &excpobj, 1, &pmsg, clo.ObjThis);
	if(TJS_FAILED(hr)) TJS_eTJSError(TJSExceptionNotFound);
	if(trace)
	{
		static tTJSString trace_name(TJS_W("trace"));
		excpobj->PropSet(TJS_MEMBERENSURE, trace_name.c_str(), trace_name.GetHint(),
			trace, excpobj);
	}
	*res = tTJSVariant(excpobj, excpobj);
	excpobj->Release();
}
//---------------------------------------------------------------------------



//---------------------------------------------------------------------------
// eTJSScriptException
//---------------------------------------------------------------------------
eTJSScriptError::tScriptBlockHolder::tScriptBlockHolder(tTJSScriptBlock *block)
{
	Block = block;
	Block->AddRef();
}
//---------------------------------------------------------------------------
eTJSScriptError::tScriptBlockHolder::~tScriptBlockHolder()
{
	Block->Release();
}
//---------------------------------------------------------------------------
eTJSScriptError::tScriptBlockHolder::tScriptBlockHolder(
		const tScriptBlockHolder &Holder)
{
	Block = Holder.Block;
	Block->AddRef();
}
//---------------------------------------------------------------------------
eTJSScriptError::eTJSScriptError(const ttstr &  Msg,
	tTJSScriptBlock *block, tjs_int pos) :
		eTJSError(Msg), Block(block), Position(pos)
{
}
//---------------------------------------------------------------------------
tjs_int eTJSScriptError::GetSourceLine() const
{
	return Block.Block->SrcPosToLine(Position) +1;
}
//---------------------------------------------------------------------------
const tjs_char * eTJSScriptError::GetBlockName() const
{
	const tjs_char * name = Block.Block->GetName() ;
	return name ? name : TJS_W("");
}
//---------------------------------------------------------------------------
bool eTJSScriptError::AddTrace(tTJSScriptBlock *block, tjs_int srcpos)
{
	tjs_int len = Trace.GetLen();
	if(len >= TJS_MAX_TRACE_TEXT_LEN) return false;

	if(len != 0) Trace += TJS_W("\n<-- ");
	Trace += block->GetLineDescriptionString(srcpos);

	return true;
}
//---------------------------------------------------------------------------
bool eTJSScriptError::AddTrace(tTJSInterCodeContext *context, tjs_int codepos)
{
	tjs_int len = Trace.GetLen();
	if(len >= TJS_MAX_TRACE_TEXT_LEN) return false;

	if(len != 0) Trace += TJS_W(" <-- ");
	Trace += context->GetPositionDescriptionString(codepos);

	return true;
}
//---------------------------------------------------------------------------
bool eTJSScriptError::AddTrace(const ttstr & data)
{
	tjs_int len = Trace.GetLen();
	if(len >= TJS_MAX_TRACE_TEXT_LEN) return false;
	if(len != 0) Trace += TJS_W(" <-- ");
	Trace += data;
	return true;
}
//---------------------------------------------------------------------------




//---------------------------------------------------------------------------
// throw helper functions
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
static void TJSReportExceptionSource(const ttstr &msg, tTJSScriptBlock *block,
	tjs_int srcpos)
{
	if(TJSEnableDebugMode)
	{
		tTJS *tjs = block->GetTJS();
		tjs->OutputExceptionToConsole((msg + TJS_W(" at ") +
			block->GetLineDescriptionString(srcpos)).c_str());
	}
}
//---------------------------------------------------------------------------
static void TJSReportExceptionSource(const ttstr &msg,
	tTJSInterCodeContext *context, tjs_int codepos)
{
	if(TJSEnableDebugMode)
	{
		tTJS *tjs = context->GetBlock()->GetTJS();
		tjs->OutputExceptionToConsole((msg + TJS_W(" at ") +
			context->GetPositionDescriptionString(codepos)).c_str());
	}
}
//---------------------------------------------------------------------------
void TJS_eTJS() { throw eTJS(); }
//---------------------------------------------------------------------------
void TJS_eTJSError(const ttstr & msg) { throw eTJSError(msg); }
void TJS_eTJSError(const tjs_char* msg) { throw eTJSError(msg); }
//---------------------------------------------------------------------------
void TJS_eTJSVariantError(const ttstr & msg) { throw eTJSVariantError(msg); }
void TJS_eTJSVariantError(const tjs_char * msg) { throw eTJSVariantError(msg); }
//---------------------------------------------------------------------------
void TJS_eTJSScriptError(const ttstr &msg, tTJSScriptBlock *block, tjs_int srcpos)
{
	TJSReportExceptionSource(msg, block, srcpos);
	throw eTJSScriptError(msg, block, srcpos);
}
//---------------------------------------------------------------------------
void TJS_eTJSScriptError(const tjs_char *msg, tTJSScriptBlock *block, tjs_int srcpos)
{
	TJSReportExceptionSource(msg, block, srcpos);
	throw eTJSScriptError(msg, block, srcpos);
}
//---------------------------------------------------------------------------
void TJS_eTJSScriptError(const ttstr &msg, tTJSInterCodeContext *context, tjs_int codepos)
{
	TJSReportExceptionSource(msg, context, codepos);
	throw eTJSScriptError(msg, context->GetBlock(), context->CodePosToSrcPos(codepos));
}
//---------------------------------------------------------------------------
void TJS_eTJSScriptError(const tjs_char *msg, tTJSInterCodeContext *context, tjs_int codepos)
{
	TJSReportExceptionSource(msg, context, codepos);
	throw eTJSScriptError(msg, context->GetBlock(), context->CodePosToSrcPos(codepos));
}
//---------------------------------------------------------------------------
void TJS_eTJSScriptException(const ttstr &msg, tTJSScriptBlock *block,
	tjs_int srcpos, tTJSVariant &val)
{
	TJSReportExceptionSource(msg, block, srcpos);
	throw eTJSScriptException(msg, block, srcpos, val);
}
//---------------------------------------------------------------------------
void TJS_eTJSScriptException(const tjs_char *msg, tTJSScriptBlock *block,
	tjs_int srcpos, tTJSVariant &val)
{
	TJSReportExceptionSource(msg, block, srcpos);
	throw eTJSScriptException(msg, block, srcpos, val);
}
//---------------------------------------------------------------------------
void TJS_eTJSScriptException(const ttstr &msg, tTJSInterCodeContext *context,
	tjs_int codepos, tTJSVariant &val)
{
	TJSReportExceptionSource(msg, context, codepos);
	throw eTJSScriptException(msg, context->GetBlock(), context->CodePosToSrcPos(codepos), val);
}
//---------------------------------------------------------------------------
void TJS_eTJSScriptException(const tjs_char *msg, tTJSInterCodeContext *context,
	tjs_int codepos, tTJSVariant &val)
{
	TJSReportExceptionSource(msg, context, codepos);
	throw eTJSScriptException(msg, context->GetBlock(), context->CodePosToSrcPos(codepos), val);
}
//---------------------------------------------------------------------------
void TJS_eTJSCompileError(const ttstr & msg, tTJSScriptBlock *block, tjs_int srcpos)
{
	TJSReportExceptionSource(msg, block, srcpos);
	throw eTJSCompileError(msg, block, srcpos);
}
//---------------------------------------------------------------------------
void TJS_eTJSCompileError(const tjs_char *msg, tTJSScriptBlock *block, tjs_int srcpos)
{
	TJSReportExceptionSource(msg, block, srcpos);
	throw eTJSCompileError(msg, block, srcpos);
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
void TJSThrowFrom_tjs_error(tjs_error hr, const tjs_char *name)
{
	// raise an exception descripted as tjs_error
	// name = variable name ( otherwide it can be NULL )

	switch(hr)
	{
	case TJS_E_MEMBERNOTFOUND:
	  {
		if(name)
		{
			ttstr str(TJSMemberNotFound);
			str.Replace(TJS_W("%1"), name);
			TJS_eTJSError(str);
		}
		else
		{
			TJS_eTJSError(TJSMemberNotFoundNoNameGiven);
		}
	  }
	case TJS_E_NOTIMPL:
		TJS_eTJSError(TJSNotImplemented);
	case TJS_E_INVALIDPARAM:
		TJS_eTJSError(TJSInvalidParam);
	case TJS_E_BADPARAMCOUNT:
		TJS_eTJSError(TJSBadParamCount);
	case TJS_E_INVALIDTYPE:
		TJS_eTJSError(TJSInvalidType);
	case TJS_E_ACCESSDENYED:
		TJS_eTJSError(TJSAccessDenyed);
	case TJS_E_INVALIDOBJECT:
		TJS_eTJSError(TJSInvalidObject);
	case TJS_E_NATIVECLASSCRASH:
		TJS_eTJSError(TJSNativeClassCrash);
	default:
		if(TJS_FAILED(hr))
		{
			tjs_char buf[256];
			TJS_snprintf(buf, 256, TJSUnknownFailure, hr);
			TJS_eTJSError(buf);
		}
	}
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// error messages  ( can be localized )
//---------------------------------------------------------------------------
ttstr TJSNonamedException = TJS_W("No-named exception");
//---------------------------------------------------------------------------
} // namespace TJS


#define TJS_DECL_MESSAGE_BODY
#undef tjsErrorH
#undef __TJS_ERROR_INC_H__
#include "tjsError.h"
//---------------------------------------------------------------------------

