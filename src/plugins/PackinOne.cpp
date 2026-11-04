// PackinOne.cpp - Built-in native implementation of Moonstone's PackinOne.dll
// Provides Scripts.*, Storages.*, Layer.*, and System.* extensions for Lip lipples.

#define NCB_MODULE_NAME TJS_W("packinone.dll")

#include "tp_stub.h"
#include "ncbind/ncbind.hpp"
#include "LayerExBase.h"
#include "StorageImpl.h"
#include "Platform.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <sys/stat.h>
#include <vector>

namespace {

//---------------------------------------------------------------------------
// Helper: Attach a tTJSDispatch function or constant to a global class/object
//---------------------------------------------------------------------------
static void RegisterDispatchMember(const tjs_char *targetName, const tjs_char *memberName, tTJSDispatch *fn) {
	iTJSDispatch2 *global = TVPGetScriptDispatch();
	if (!global) {
		fn->Release();
		return;
	}
	tTJSVariant targetVar;
	if (TJS_SUCCEEDED(global->PropGet(0, targetName, NULL, &targetVar, global)) &&
	    targetVar.Type() == tvtObject && targetVar.AsObjectNoAddRef()) {
		iTJSDispatch2 *target = targetVar.AsObjectNoAddRef();
		tTJSVariant val(fn);
		target->PropSet(TJS_MEMBERENSURE, memberName, NULL, &val, target);
	}
	fn->Release();
	global->Release();
}

static void RegisterIntConstant(const tjs_char *targetName, const tjs_char *memberName, tjs_int64 value) {
	iTJSDispatch2 *global = TVPGetScriptDispatch();
	if (!global) return;
	tTJSVariant targetVar;
	if (TJS_SUCCEEDED(global->PropGet(0, targetName, NULL, &targetVar, global)) &&
	    targetVar.Type() == tvtObject && targetVar.AsObjectNoAddRef()) {
		iTJSDispatch2 *target = targetVar.AsObjectNoAddRef();
		tTJSVariant val(value);
		target->PropSet(TJS_MEMBERENSURE, memberName, NULL, &val, target);
	}
	global->Release();
}

//---------------------------------------------------------------------------
// Member enumeration helper (used by getObjectKeys, getObjectCount, foreach, clone, equalStruct)
//---------------------------------------------------------------------------
struct MemberEntry {
	tTJSVariant key;
	tTJSVariant value;
};

class MemberCollector : public tTJSDispatch {
public:
	std::vector<MemberEntry> items;
	tjs_error TJS_INTF_METHOD FuncCall(
		tjs_uint32, const tjs_char *, tjs_uint32 *, tTJSVariant *result,
		tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *) override {
		if (numparams >= 3) {
			tjs_int flags = (tjs_int)*param[1];
			if (!(flags & TJS_HIDDENMEMBER)) {
				MemberEntry e;
				e.key = *param[0];
				e.value = *param[2];
				items.push_back(e);
			}
		}
		if (result) *result = (tjs_int)1;
		return TJS_S_OK;
	}
};

static void CollectMembers(iTJSDispatch2 *obj, iTJSDispatch2 *ctx, std::vector<MemberEntry> &out, bool ignoreProp = true) {
	if (!obj) return;
	MemberCollector *collector = new MemberCollector();
	tTJSVariantClosure closure(collector);
	obj->EnumMembers(ignoreProp ? TJS_IGNOREPROP : 0, &closure, ctx ? ctx : obj);
	out.swap(collector->items);
	collector->Release();
}

static bool IsArrayObject(iTJSDispatch2 *obj, iTJSDispatch2 *ctx = NULL) {
	if (!obj) return false;
	if (obj->IsInstanceOf(0, NULL, NULL, TJS_W("Array"), ctx ? ctx : obj) == TJS_S_TRUE) {
		return true;
	}
	tTJSVariant v;
	if (TJS_SUCCEEDED(obj->ClassInstanceInfo(TJS_CII_GET, 0, &v))) {
		return ttstr(v) == TJS_W("Array");
	}
	return false;
}

static bool IsDictionaryObject(iTJSDispatch2 *obj, iTJSDispatch2 *ctx = NULL) {
	if (!obj) return false;
	if (obj->IsInstanceOf(0, NULL, NULL, TJS_W("Dictionary"), ctx ? ctx : obj) == TJS_S_TRUE) {
		return true;
	}
	tTJSVariant v;
	if (TJS_SUCCEEDED(obj->ClassInstanceInfo(TJS_CII_GET, 0, &v))) {
		return ttstr(v) == TJS_W("Dictionary");
	}
	return false;
}

static tjs_int GetArrayCount(iTJSDispatch2 *arr) {
	tTJSVariant v;
	static tjs_uint32 countHint = 0;
	if (TJS_SUCCEEDED(arr->PropGet(0, TJS_W("count"), &countHint, &v, arr))) {
		return (tjs_int)v;
	}
	return 0;
}

//---------------------------------------------------------------------------
// Scripts.* implementations
//---------------------------------------------------------------------------
class FnGetObjectKeys : public tTJSDispatch {
	tjs_error TJS_INTF_METHOD FuncCall(
		tjs_uint32, const tjs_char *m, tjs_uint32 *, tTJSVariant *result,
		tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *) override {
		if (m) return TJS_E_MEMBERNOTFOUND;
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;
		iTJSDispatch2 *arr = TJSCreateArrayObject();
		if (param[0]->Type() == tvtObject) {
			tTJSVariantClosure clo = param[0]->AsObjectClosureNoAddRef();
			if (clo.Object) {
				std::vector<MemberEntry> members;
				CollectMembers(clo.Object, clo.ObjThis, members, true);
				std::vector<ttstr> keys;
				keys.reserve(members.size());
				for (size_t i = 0; i < members.size(); ++i) {
					keys.push_back(ttstr(members[i].key));
				}
				std::sort(keys.begin(), keys.end());
				for (size_t i = 0; i < keys.size(); ++i) {
					tTJSVariant kv(keys[i]);
					arr->PropSetByNum(TJS_MEMBERENSURE, (tjs_int)i, &kv, arr);
				}
			}
		}
		if (result) *result = tTJSVariant(arr, arr);
		arr->Release();
		return TJS_S_OK;
	}
};

class FnGetObjectCount : public tTJSDispatch {
	tjs_error TJS_INTF_METHOD FuncCall(
		tjs_uint32, const tjs_char *m, tjs_uint32 *, tTJSVariant *result,
		tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *) override {
		if (m) return TJS_E_MEMBERNOTFOUND;
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;
		tjs_int count = 0;
		if (param[0]->Type() == tvtObject) {
			tTJSVariantClosure clo = param[0]->AsObjectClosureNoAddRef();
			if (clo.Object) {
				if (TJS_FAILED(clo.Object->GetCount(&count, NULL, NULL, clo.ObjThis ? clo.ObjThis : clo.Object))) {
					std::vector<MemberEntry> members;
					CollectMembers(clo.Object, clo.ObjThis, members, true);
					count = (tjs_int)members.size();
				}
			}
		}
		if (result) *result = count;
		return TJS_S_OK;
	}
};

class FnForeach : public tTJSDispatch {
	tjs_error TJS_INTF_METHOD FuncCall(
		tjs_uint32, const tjs_char *m, tjs_uint32 *, tTJSVariant *result,
		tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *objthis) override {
		if (m) return TJS_E_MEMBERNOTFOUND;
		if (numparams < 2) return TJS_E_BADPARAMCOUNT;
		if (result) result->Clear();
		if (param[0]->Type() != tvtObject || param[1]->Type() != tvtObject) return TJS_S_OK;

		tTJSVariantClosure target = param[0]->AsObjectClosureNoAddRef();
		tTJSVariantClosure func = param[1]->AsObjectClosureNoAddRef();
		if (!target.Object || !func.Object) return TJS_S_OK;

		iTJSDispatch2 *funcThis = func.ObjThis ? func.ObjThis : objthis;

		int extraCount = numparams > 2 ? (numparams - 2) : 0;
		std::vector<tTJSVariant> callVals(2 + extraCount);
		std::vector<tTJSVariant *> callPtrs(2 + extraCount);
		for (int i = 0; i < extraCount; ++i) callVals[2 + i] = *param[2 + i];
		for (size_t i = 0; i < callVals.size(); ++i) callPtrs[i] = &callVals[i];

		if (IsArrayObject(target.Object, target.ObjThis)) {
			tjs_int cnt = GetArrayCount(target.Object);
			for (tjs_int i = 0; i < cnt; ++i) {
				callVals[0] = i;
				callVals[1].Clear();
				target.Object->PropGetByNum(TJS_IGNOREPROP, i, &callVals[1], target.ObjThis ? target.ObjThis : target.Object);
				tTJSVariant r;
				func.Object->FuncCall(0, NULL, NULL, &r, (tjs_int)callPtrs.size(), &callPtrs[0], funcThis);
				if (r.Type() != tvtVoid) {
					if (result) *result = r;
					return TJS_S_OK;
				}
			}
		} else {
			std::vector<MemberEntry> members;
			CollectMembers(target.Object, target.ObjThis, members, true);
			for (size_t i = 0; i < members.size(); ++i) {
				callVals[0] = members[i].key;
				callVals[1] = members[i].value;
				tTJSVariant r;
				func.Object->FuncCall(0, NULL, NULL, &r, (tjs_int)callPtrs.size(), &callPtrs[0], funcThis);
				if (r.Type() != tvtVoid) {
					if (result) *result = r;
					return TJS_S_OK;
				}
			}
		}
		return TJS_S_OK;
	}
};

static tTJSVariant CloneVariant(const tTJSVariant &src, std::map<iTJSDispatch2 *, tTJSVariant> &seen, int depth) {
	if (depth > 64 || src.Type() != tvtObject) return src;
	tTJSVariantClosure clo = src.AsObjectClosureNoAddRef();
	if (!clo.Object) return src;
	std::map<iTJSDispatch2 *, tTJSVariant>::iterator it = seen.find(clo.Object);
	if (it != seen.end()) return it->second;

	if (IsArrayObject(clo.Object, clo.ObjThis)) {
		iTJSDispatch2 *arr = TJSCreateArrayObject();
		tTJSVariant out(arr, arr);
		arr->Release();
		seen[clo.Object] = out;
		tjs_int cnt = GetArrayCount(clo.Object);
		for (tjs_int i = 0; i < cnt; ++i) {
			tTJSVariant elem;
			clo.Object->PropGetByNum(TJS_IGNOREPROP, i, &elem, clo.ObjThis ? clo.ObjThis : clo.Object);
			tTJSVariant cloned = CloneVariant(elem, seen, depth + 1);
			arr->PropSetByNum(TJS_MEMBERENSURE | TJS_IGNOREPROP, i, &cloned, arr);
		}
		return out;
	}

	if (IsDictionaryObject(clo.Object, clo.ObjThis)) {
		iTJSDispatch2 *dic = TJSCreateDictionaryObject();
		tTJSVariant out(dic, dic);
		dic->Release();
		seen[clo.Object] = out;
		std::vector<MemberEntry> members;
		CollectMembers(clo.Object, clo.ObjThis, members, true);
		for (size_t i = 0; i < members.size(); ++i) {
			ttstr k(members[i].key);
			tTJSVariant cloned = CloneVariant(members[i].value, seen, depth + 1);
			dic->PropSet(TJS_MEMBERENSURE | TJS_IGNOREPROP, k.c_str(), NULL, &cloned, dic);
		}
		return out;
	}

	tTJSVariant customClone;
	if (TJS_SUCCEEDED(clo.Object->FuncCall(0, TJS_W("clone"), NULL, &customClone, 0, NULL, clo.ObjThis ? clo.ObjThis : clo.Object))) {
		return customClone;
	}
	return src;
}

class FnClone : public tTJSDispatch {
	tjs_error TJS_INTF_METHOD FuncCall(
		tjs_uint32, const tjs_char *m, tjs_uint32 *, tTJSVariant *result,
		tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *) override {
		if (m) return TJS_E_MEMBERNOTFOUND;
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;
		if (result) {
			std::map<iTJSDispatch2 *, tTJSVariant> seen;
			*result = CloneVariant(*param[0], seen, 0);
		}
		return TJS_S_OK;
	}
};

static bool EqualVariant(const tTJSVariant &a, const tTJSVariant &b, bool looseNumeric, int depth) {
	if (depth > 64) return true;
	tTJSVariantType ta = a.Type(), tb = b.Type();
	if (looseNumeric && (ta == tvtInteger || ta == tvtReal) && (tb == tvtInteger || tb == tvtReal)) {
		return (tTVReal)a == (tTVReal)b;
	}
	if (ta != tb) return false;
	if (ta == tvtVoid) return true;
	if (ta != tvtObject) return a.DiscernCompare(b);

	tTJSVariantClosure ca = a.AsObjectClosureNoAddRef();
	tTJSVariantClosure cb = b.AsObjectClosureNoAddRef();
	if (ca.Object == cb.Object && ca.ObjThis == cb.ObjThis) return true;
	if (!ca.Object || !cb.Object) return false;

	bool arrA = IsArrayObject(ca.Object), arrB = IsArrayObject(cb.Object);
	if (arrA != arrB) return false;
	if (arrA) {
		tjs_int cntA = GetArrayCount(ca.Object), cntB = GetArrayCount(cb.Object);
		if (cntA != cntB) return false;
		for (tjs_int i = 0; i < cntA; ++i) {
			tTJSVariant va, vb;
			ca.Object->PropGetByNum(0, i, &va, ca.ObjThis ? ca.ObjThis : ca.Object);
			cb.Object->PropGetByNum(0, i, &vb, cb.ObjThis ? cb.ObjThis : cb.Object);
			if (!EqualVariant(va, vb, looseNumeric, depth + 1)) return false;
		}
		return true;
	}

	std::vector<MemberEntry> ma, mb;
	CollectMembers(ca.Object, ca.ObjThis, ma, true);
	CollectMembers(cb.Object, cb.ObjThis, mb, true);
	if (ma.size() != mb.size()) return false;
	for (size_t i = 0; i < ma.size(); ++i) {
		ttstr key(ma[i].key);
		tTJSVariant vb;
		if (TJS_FAILED(cb.Object->PropGet(TJS_MEMBERMUSTEXIST | TJS_IGNOREPROP, key.c_str(), NULL, &vb, cb.ObjThis ? cb.ObjThis : cb.Object)))
			return false;
		if (!EqualVariant(ma[i].value, vb, looseNumeric, depth + 1)) return false;
	}
	return true;
}

class FnEqualStruct : public tTJSDispatch {
	bool loose;
public:
	explicit FnEqualStruct(bool l) : loose(l) {}
	tjs_error TJS_INTF_METHOD FuncCall(
		tjs_uint32, const tjs_char *m, tjs_uint32 *, tTJSVariant *result,
		tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *) override {
		if (m) return TJS_E_MEMBERNOTFOUND;
		if (numparams < 2) return TJS_E_BADPARAMCOUNT;
		if (result) *result = (tjs_int)(EqualVariant(*param[0], *param[1], loose, 0) ? 1 : 0);
		return TJS_S_OK;
	}
};

class FnGetObjectContext : public tTJSDispatch {
	tjs_error TJS_INTF_METHOD FuncCall(
		tjs_uint32, const tjs_char *m, tjs_uint32 *, tTJSVariant *result,
		tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *) override {
		if (m) return TJS_E_MEMBERNOTFOUND;
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;
		if (result) {
			if (param[0]->Type() == tvtObject) {
				iTJSDispatch2 *ctx = param[0]->AsObjectClosureNoAddRef().ObjThis;
				*result = tTJSVariant(ctx, ctx);
			} else {
				result->Clear();
			}
		}
		return TJS_S_OK;
	}
};

class FnIsNullContext : public tTJSDispatch {
	tjs_error TJS_INTF_METHOD FuncCall(
		tjs_uint32, const tjs_char *m, tjs_uint32 *, tTJSVariant *result,
		tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *) override {
		if (m) return TJS_E_MEMBERNOTFOUND;
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;
		bool isNull = true;
		if (param[0]->Type() == tvtObject) {
			isNull = (param[0]->AsObjectClosureNoAddRef().ObjThis == NULL);
		}
		if (result) *result = (tjs_int)(isNull ? 1 : 0);
		return TJS_S_OK;
	}
};

class FnSetCallMissing : public tTJSDispatch {
	tjs_error TJS_INTF_METHOD FuncCall(
		tjs_uint32, const tjs_char *m, tjs_uint32 *, tTJSVariant *result,
		tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *) override {
		if (m) return TJS_E_MEMBERNOTFOUND;
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;
		if (param[0]->Type() == tvtObject) {
			iTJSDispatch2 *obj = param[0]->AsObjectNoAddRef();
			if (obj) {
				tTJSVariant nameVar(numparams >= 2 && param[1]->Type() != tvtVoid
					? ttstr(*param[1])
					: ttstr(TJS_W("missing")));
				obj->ClassInstanceInfo(TJS_CII_SET_MISSING, 0, &nameVar);
			}
		}
		if (result) result->Clear();
		return TJS_S_OK;
	}
};

class FnGetClassNames : public tTJSDispatch {
	tjs_error TJS_INTF_METHOD FuncCall(
		tjs_uint32, const tjs_char *m, tjs_uint32 *, tTJSVariant *result,
		tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *) override {
		if (m) return TJS_E_MEMBERNOTFOUND;
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;
		iTJSDispatch2 *arr = TJSCreateArrayObject();
		if (param[0]->Type() == tvtObject) {
			iTJSDispatch2 *obj = param[0]->AsObjectNoAddRef();
			if (obj) {
				for (tjs_uint idx = 0; idx < 64; ++idx) {
					tTJSVariant cls;
					if (TJS_FAILED(obj->ClassInstanceInfo(TJS_CII_GET, idx, &cls)) || cls.Type() == tvtVoid)
						break;
					arr->PropSetByNum(TJS_MEMBERENSURE, (tjs_int)idx, &cls, arr);
				}
			}
		}
		if (result) *result = tTJSVariant(arr, arr);
		arr->Release();
		return TJS_S_OK;
	}
};

class FnPropSet : public tTJSDispatch {
	tjs_error TJS_INTF_METHOD FuncCall(
		tjs_uint32, const tjs_char *m, tjs_uint32 *, tTJSVariant *result,
		tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *) override {
		if (m) return TJS_E_MEMBERNOTFOUND;
		if (numparams < 3) return TJS_E_BADPARAMCOUNT;
		if (param[0]->Type() != tvtObject) return TJS_E_INVALIDPARAM;
		tTJSVariantClosure clo = param[0]->AsObjectClosureNoAddRef();
		if (!clo.Object) return TJS_E_INVALIDPARAM;
		tjs_uint32 flags = (numparams >= 4 && param[3]->Type() != tvtVoid) ? (tjs_uint32)(tjs_int)*param[3] : TJS_MEMBERENSURE;
		iTJSDispatch2 *ctx = clo.ObjThis ? clo.ObjThis : clo.Object;
		tjs_error hr;
		if (param[1]->Type() == tvtInteger) {
			hr = clo.Object->PropSetByNum(flags, (tjs_int)*param[1], param[2], ctx);
		} else {
			ttstr key(*param[1]);
			hr = clo.Object->PropSet(flags, key.c_str(), NULL, param[2], ctx);
		}
		if (result) *result = (tjs_int)(TJS_SUCCEEDED(hr) ? 1 : 0);
		return TJS_S_OK;
	}
};

class FnPropGet : public tTJSDispatch {
	tjs_error TJS_INTF_METHOD FuncCall(
		tjs_uint32, const tjs_char *m, tjs_uint32 *, tTJSVariant *result,
		tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *) override {
		if (m) return TJS_E_MEMBERNOTFOUND;
		if (numparams < 2) return TJS_E_BADPARAMCOUNT;
		if (param[0]->Type() != tvtObject) return TJS_E_INVALIDPARAM;
		tTJSVariantClosure clo = param[0]->AsObjectClosureNoAddRef();
		if (!clo.Object) return TJS_E_INVALIDPARAM;
		tjs_uint32 flags = (numparams >= 3 && param[2]->Type() != tvtVoid) ? (tjs_uint32)(tjs_int)*param[2] : 0;
		iTJSDispatch2 *ctx = clo.ObjThis ? clo.ObjThis : clo.Object;
		tTJSVariant out;
		tjs_error hr;
		if (param[1]->Type() == tvtInteger) {
			hr = clo.Object->PropGetByNum(flags, (tjs_int)*param[1], &out, ctx);
		} else {
			ttstr key(*param[1]);
			hr = clo.Object->PropGet(flags, key.c_str(), NULL, &out, ctx);
		}
		if (result) {
			if (TJS_SUCCEEDED(hr)) *result = out;
			else result->Clear();
		}
		return TJS_S_OK;
	}
};

class FnSafeEvalStorage : public tTJSDispatch {
	tjs_error TJS_INTF_METHOD FuncCall(
		tjs_uint32, const tjs_char *m, tjs_uint32 *, tTJSVariant *result,
		tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *) override {
		if (m) return TJS_E_MEMBERNOTFOUND;
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;
		ttstr storage(*param[0]);
		iTJSDispatch2 *context = NULL;
		if (numparams >= 3 && param[2]->Type() == tvtObject) {
			context = param[2]->AsObjectNoAddRef();
		} else if (numparams >= 4 && param[3]->Type() == tvtObject) {
			context = param[3]->AsObjectNoAddRef();
		}
		tTJSVariant r;
		try {
			TVPExecuteStorage(storage, context, &r, true);
		} catch (...) {
			r.Clear();
		}
		if (result) *result = r;
		return TJS_S_OK;
	}
};

static ttstr FormatMd5Hex(const tjs_uint8 digest[16]) {
	static const char hex[] = "0123456789abcdef";
	tjs_char out[33];
	for (int i = 0; i < 16; ++i) {
		out[2 * i]     = (tjs_char)hex[(digest[i] >> 4) & 0x0f];
		out[2 * i + 1] = (tjs_char)hex[digest[i] & 0x0f];
	}
	out[32] = 0;
	return ttstr(out);
}

class FnScriptsMd5 : public tTJSDispatch {
	tjs_error TJS_INTF_METHOD FuncCall(
		tjs_uint32, const tjs_char *m, tjs_uint32 *, tTJSVariant *result,
		tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *) override {
		if (m) return TJS_E_MEMBERNOTFOUND;
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;
		TVP_md5_state_t st;
		TVP_md5_init(&st);
		if (param[0]->Type() == tvtOctet) {
			tTJSVariantOctet *oct = param[0]->AsOctetNoAddRef();
			if (oct && oct->GetLength() > 0) {
				TVP_md5_append(&st, oct->GetData(), (int)oct->GetLength());
			}
		} else {
			std::string u8 = ttstr(*param[0]).AsStdString();
			if (!u8.empty()) {
				TVP_md5_append(&st, reinterpret_cast<const tjs_uint8 *>(u8.data()), (int)u8.size());
			}
		}
		tjs_uint8 digest[16];
		TVP_md5_finish(&st, digest);
		if (result) *result = FormatMd5Hex(digest);
		return TJS_S_OK;
	}
};

class FnNoOp : public tTJSDispatch {
	int retVal;
	bool hasRet;
public:
	FnNoOp() : retVal(0), hasRet(false) {}
	explicit FnNoOp(int r) : retVal(r), hasRet(true) {}
	tjs_error TJS_INTF_METHOD FuncCall(
		tjs_uint32, const tjs_char *m, tjs_uint32 *, tTJSVariant *result,
		tjs_int, tTJSVariant **, iTJSDispatch2 *) override {
		if (m) return TJS_E_MEMBERNOTFOUND;
		if (result) {
			if (hasRet) *result = (tjs_int)retVal;
			else result->Clear();
		}
		return TJS_S_OK;
	}
};

//---------------------------------------------------------------------------
// Storages.* implementations
//---------------------------------------------------------------------------
static bool ResolveLocalPath(const ttstr &inPath, ttstr &localOut) {
	if (inPath.IsEmpty()) return false;
	if (TJS_strchr(inPath.c_str(), TVPArchiveDelimiter)) return false;
	ttstr norm = TVPNormalizeStorageName(inPath);
	localOut = TVPGetLocallyAccessibleName(norm);
	return !localOut.IsEmpty();
}

class FnIsExistentDirectory : public tTJSDispatch {
	tjs_error TJS_INTF_METHOD FuncCall(
		tjs_uint32, const tjs_char *m, tjs_uint32 *, tTJSVariant *result,
		tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *) override {
		if (m) return TJS_E_MEMBERNOTFOUND;
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;
		ttstr local;
		bool ok = false;
		if (ResolveLocalPath(*param[0], local)) {
			ok = TVPCheckExistentLocalFolder(local);
		}
		if (result) *result = (tjs_int)(ok ? 1 : 0);
		return TJS_S_OK;
	}
};

class FnIsExistentStorageNoSearchNoNormalize : public tTJSDispatch {
	tjs_error TJS_INTF_METHOD FuncCall(
		tjs_uint32, const tjs_char *m, tjs_uint32 *, tTJSVariant *result,
		tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *) override {
		if (m) return TJS_E_MEMBERNOTFOUND;
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;
		bool ok = TVPIsExistentStorageNoSearchNoNormalize(ttstr(*param[0]));
		if (result) *result = (tjs_int)(ok ? 1 : 0);
		return TJS_S_OK;
	}
};

class FnDirList : public tTJSDispatch {
	tjs_error TJS_INTF_METHOD FuncCall(
		tjs_uint32, const tjs_char *m, tjs_uint32 *, tTJSVariant *result,
		tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *) override {
		if (m) return TJS_E_MEMBERNOTFOUND;
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;
		iTJSDispatch2 *arr = TJSCreateArrayObject();
		ttstr local;
		if (ResolveLocalPath(*param[0], local)) {
			std::vector<ttstr> entries;
			TVPGetLocalFileListAt(local, [&entries](const ttstr &name, tTVPLocalFileInfo *info) {
				if (!info || name.IsEmpty() || name == TJS_W(".") || name == TJS_W("..")) return;
				if (info->Mode & S_IFDIR) {
					ttstr d = name;
					if (d.GetLastChar() != TJS_W('/')) d += TJS_W("/");
					entries.push_back(d);
				} else if (info->Mode & S_IFREG) {
					entries.push_back(name);
				}
			});
			for (size_t i = 0; i < entries.size(); ++i) {
				tTJSVariant v(entries[i]);
				arr->PropSetByNum(TJS_MEMBERENSURE, (tjs_int)i, &v, arr);
			}
		}
		if (result) *result = tTJSVariant(arr, arr);
		arr->Release();
		return TJS_S_OK;
	}
};

class FnDeleteFile : public tTJSDispatch {
	tjs_error TJS_INTF_METHOD FuncCall(
		tjs_uint32, const tjs_char *m, tjs_uint32 *, tTJSVariant *result,
		tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *) override {
		if (m) return TJS_E_MEMBERNOTFOUND;
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;
		ttstr local;
		bool ok = false;
		if (ResolveLocalPath(*param[0], local)) {
			ok = TVPDeleteFile(local.AsStdString());
		}
		if (result) *result = (tjs_int)(ok ? 1 : 0);
		return TJS_S_OK;
	}
};

class FnMoveFile : public tTJSDispatch {
	tjs_error TJS_INTF_METHOD FuncCall(
		tjs_uint32, const tjs_char *m, tjs_uint32 *, tTJSVariant *result,
		tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *) override {
		if (m) return TJS_E_MEMBERNOTFOUND;
		if (numparams < 2) return TJS_E_BADPARAMCOUNT;
		ttstr fromLocal, toLocal;
		bool ok = false;
		if (ResolveLocalPath(*param[0], fromLocal) && ResolveLocalPath(*param[1], toLocal)) {
			ttstr parent = TVPExtractStoragePath(toLocal);
			if (!parent.IsEmpty()) TVPCreateFolders(parent);
			ok = TVPRenameFile(fromLocal.AsStdString(), toLocal.AsStdString());
		}
		if (result) *result = (tjs_int)(ok ? 1 : 0);
		return TJS_S_OK;
	}
};

class FnCopyFile : public tTJSDispatch {
	tjs_error TJS_INTF_METHOD FuncCall(
		tjs_uint32, const tjs_char *m, tjs_uint32 *, tTJSVariant *result,
		tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *) override {
		if (m) return TJS_E_MEMBERNOTFOUND;
		if (numparams < 2) return TJS_E_BADPARAMCOUNT;
		ttstr src(*param[0]), dst(*param[1]);
		bool failIfExists = (numparams >= 3 && param[2]->Type() != tvtVoid) ? ((tjs_int)*param[2] != 0) : false;
		if (failIfExists && TVPIsExistentStorageNoSearch(dst)) {
			if (result) *result = (tjs_int)0;
			return TJS_S_OK;
		}
		bool ok = false;
		tTJSBinaryStream *in = NULL;
		tTJSBinaryStream *out = NULL;
		try {
			ttstr dstLocal;
			if (ResolveLocalPath(dst, dstLocal)) {
				ttstr parent = TVPExtractStoragePath(dstLocal);
				if (!parent.IsEmpty()) TVPCreateFolders(parent);
			}
			in = TVPCreateStream(src, TJS_BS_READ);
			out = TVPCreateStream(dst, TJS_BS_WRITE);
			if (in && out) {
				std::vector<tjs_uint8> buf(65536);
				while (true) {
					tjs_uint got = in->Read(&buf[0], (tjs_uint)buf.size());
					if (got == 0) break;
					out->WriteBuffer(&buf[0], got);
				}
				ok = true;
			}
		} catch (...) {
			ok = false;
		}
		if (in) delete in;
		if (out) delete out;
		if (result) *result = (tjs_int)(ok ? 1 : 0);
		return TJS_S_OK;
	}
};

class FnMkdir : public tTJSDispatch {
	tjs_error TJS_INTF_METHOD FuncCall(
		tjs_uint32, const tjs_char *m, tjs_uint32 *, tTJSVariant *result,
		tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *) override {
		if (m) return TJS_E_MEMBERNOTFOUND;
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;
		ttstr local;
		bool ok = false;
		if (ResolveLocalPath(*param[0], local)) {
			ok = TVPCreateFolders(local);
		}
		if (result) *result = (tjs_int)(ok ? 1 : 0);
		return TJS_S_OK;
	}
};

class FnLoadOctet : public tTJSDispatch {
	tjs_error TJS_INTF_METHOD FuncCall(
		tjs_uint32, const tjs_char *m, tjs_uint32 *, tTJSVariant *result,
		tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *) override {
		if (m) return TJS_E_MEMBERNOTFOUND;
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;
		if (result) result->Clear();
		tTJSBinaryStream *st = NULL;
		try {
			st = TVPCreateStream(ttstr(*param[0]), TJS_BS_READ);
			if (st) {
				tjs_uint64 sz = st->GetSize();
				std::vector<tjs_uint8> buf((size_t)sz);
				if (sz > 0) st->ReadBuffer(&buf[0], (tjs_uint)sz);
				if (result) {
					tTJSVariantOctet *oct = TJSAllocVariantOctet(sz > 0 ? &buf[0] : NULL, (tjs_uint)sz);
					*result = oct;
					oct->Release();
				}
			}
		} catch (...) {
		}
		if (st) delete st;
		return TJS_S_OK;
	}
};

class FnSaveOctet : public tTJSDispatch {
	tjs_error TJS_INTF_METHOD FuncCall(
		tjs_uint32, const tjs_char *m, tjs_uint32 *, tTJSVariant *result,
		tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *) override {
		if (m) return TJS_E_MEMBERNOTFOUND;
		if (numparams < 2) return TJS_E_BADPARAMCOUNT;
		tjs_int written = 0;
		tTJSBinaryStream *st = NULL;
		try {
			ttstr dst(*param[0]);
			ttstr dstLocal;
			if (ResolveLocalPath(dst, dstLocal)) {
				ttstr parent = TVPExtractStoragePath(dstLocal);
				if (!parent.IsEmpty()) TVPCreateFolders(parent);
			}
			st = TVPCreateStream(dst, TJS_BS_WRITE);
			tTJSVariantOctet *oct = param[1]->AsOctetNoAddRef();
			if (st && oct) {
				tjs_uint len = oct->GetLength();
				if (len > 0) st->WriteBuffer(oct->GetData(), len);
				written = (tjs_int)len;
			}
		} catch (...) {
			written = 0;
		}
		if (st) delete st;
		if (result) *result = written;
		return TJS_S_OK;
	}
};

class FnClearStorageCaches : public tTJSDispatch {
	tjs_error TJS_INTF_METHOD FuncCall(
		tjs_uint32, const tjs_char *m, tjs_uint32 *, tTJSVariant *result,
		tjs_int, tTJSVariant **, iTJSDispatch2 *) override {
		if (m) return TJS_E_MEMBERNOTFOUND;
		TVPClearStorageCaches();
		if (result) result->Clear();
		return TJS_S_OK;
	}
};

class FnGetLastModifiedFileTime : public tTJSDispatch {
	tjs_error TJS_INTF_METHOD FuncCall(
		tjs_uint32, const tjs_char *m, tjs_uint32 *, tTJSVariant *result,
		tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *) override {
		if (m) return TJS_E_MEMBERNOTFOUND;
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;
		tjs_int64 mtime = 0;
		ttstr local;
		if (ResolveLocalPath(*param[0], local)) {
			struct stat st;
			if (stat(local.AsStdString().c_str(), &st) == 0) {
				mtime = (tjs_int64)st.st_mtime * 1000LL;
			}
		}
		if (result) *result = mtime;
		return TJS_S_OK;
	}
};

class FnStoragesMd5 : public tTJSDispatch {
	tjs_error TJS_INTF_METHOD FuncCall(
		tjs_uint32, const tjs_char *m, tjs_uint32 *, tTJSVariant *result,
		tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *) override {
		if (m) return TJS_E_MEMBERNOTFOUND;
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;
		tTJSBinaryStream *st = NULL;
		TVP_md5_state_t md5;
		TVP_md5_init(&md5);
		try {
			st = TVPCreateStream(ttstr(*param[0]), TJS_BS_READ);
			if (st) {
				std::vector<tjs_uint8> buf(65536);
				while (true) {
					tjs_uint got = st->Read(&buf[0], (tjs_uint)buf.size());
					if (got == 0) break;
					TVP_md5_append(&md5, &buf[0], (int)got);
				}
			}
		} catch (...) {
		}
		if (st) delete st;
		tjs_uint8 digest[16];
		TVP_md5_finish(&md5, digest);
		if (result) *result = FormatMd5Hex(digest);
		return TJS_S_OK;
	}
};

//---------------------------------------------------------------------------
// Layer.* pixel manipulation implementations
//---------------------------------------------------------------------------
class FnLayerFillAlpha : public tTJSDispatch {
	tjs_error TJS_INTF_METHOD FuncCall(
		tjs_uint32, const tjs_char *m, tjs_uint32 *, tTJSVariant *result,
		tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *objthis) override {
		if (m) return TJS_E_MEMBERNOTFOUND;
		if (!objthis) return TJS_E_NATIVECLASSCRASH;
		NI_LayerExBase *ni = NI_LayerExBase::getNative(objthis);
		if (!ni) return TJS_S_OK;
		ni->reset(objthis);
		tjs_uint8 alpha = (numparams >= 1 && param[0]->Type() != tvtVoid) ? (tjs_uint8)(tjs_int)*param[0] : 0xff;
		if (ni->_buffer && ni->_width > 0 && ni->_height > 0) {
			for (tjs_int y = 0; y < ni->_height; ++y) {
				unsigned char *row = ni->_buffer + y * ni->_pitch;
				for (tjs_int x = 0; x < ni->_width; ++x) {
					row[x * 4 + 3] = alpha;
				}
			}
			ni->redraw(objthis);
		}
		if (result) result->Clear();
		return TJS_S_OK;
	}
};

class FnLayerCopyBottomBlueToTopAlpha : public tTJSDispatch {
	tjs_error TJS_INTF_METHOD FuncCall(
		tjs_uint32, const tjs_char *m, tjs_uint32 *, tTJSVariant *result,
		tjs_int, tTJSVariant **, iTJSDispatch2 *objthis) override {
		if (m) return TJS_E_MEMBERNOTFOUND;
		if (!objthis) return TJS_E_NATIVECLASSCRASH;
		NI_LayerExBase *ni = NI_LayerExBase::getNative(objthis);
		if (!ni) return TJS_S_OK;
		ni->reset(objthis);
		tjs_int halfH = ni->_height / 2;
		if (ni->_buffer && ni->_width > 0 && halfH > 0) {
			for (tjs_int y = 0; y < halfH; ++y) {
				unsigned char *top = ni->_buffer + y * ni->_pitch;
				const unsigned char *bot = ni->_buffer + (y + halfH) * ni->_pitch;
				for (tjs_int x = 0; x < ni->_width; ++x) {
					top[x * 4 + 3] = bot[x * 4 + 0];
				}
			}
			tTJSVariant vh(halfH);
			objthis->PropSet(0, TJS_W("imageHeight"), NULL, &vh, objthis);
			ni->redraw(objthis);
		}
		if (result) result->Clear();
		return TJS_S_OK;
	}
};

class FnLayerCopyRightBlueToLeftAlpha : public tTJSDispatch {
	tjs_error TJS_INTF_METHOD FuncCall(
		tjs_uint32, const tjs_char *m, tjs_uint32 *, tTJSVariant *result,
		tjs_int, tTJSVariant **, iTJSDispatch2 *objthis) override {
		if (m) return TJS_E_MEMBERNOTFOUND;
		if (!objthis) return TJS_E_NATIVECLASSCRASH;
		NI_LayerExBase *ni = NI_LayerExBase::getNative(objthis);
		if (!ni) return TJS_S_OK;
		ni->reset(objthis);
		tjs_int halfW = ni->_width / 2;
		if (ni->_buffer && halfW > 0 && ni->_height > 0) {
			for (tjs_int y = 0; y < ni->_height; ++y) {
				unsigned char *row = ni->_buffer + y * ni->_pitch;
				for (tjs_int x = 0; x < halfW; ++x) {
					row[x * 4 + 3] = row[(x + halfW) * 4 + 0];
				}
			}
			tTJSVariant vw(halfW);
			objthis->PropSet(0, TJS_W("imageWidth"), NULL, &vw, objthis);
			ni->redraw(objthis);
		}
		if (result) result->Clear();
		return TJS_S_OK;
	}
};

class FnLayerCopyAlphaToProvince : public tTJSDispatch {
	tjs_error TJS_INTF_METHOD FuncCall(
		tjs_uint32, const tjs_char *m, tjs_uint32 *, tTJSVariant *result,
		tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *objthis) override {
		if (m) return TJS_E_MEMBERNOTFOUND;
		if (!objthis) return TJS_E_NATIVECLASSCRASH;
		NI_LayerExBase *ni = NI_LayerExBase::getNative(objthis);
		if (!ni) return TJS_S_OK;
		ni->reset(objthis);
		tjs_int threshold = (numparams >= 1 && param[0]->Type() != tvtVoid) ? (tjs_int)*param[0] : 1;
		tjs_int matchedVal = (numparams >= 2 && param[1]->Type() != tvtVoid) ? (tjs_int)*param[1] : 1;
		tjs_int clearVal = (numparams >= 3 && param[2]->Type() != tvtVoid) ? (tjs_int)*param[2] : 0;

		tTJSVariant vpBuf, vpPitch;
		if (ni->_buffer &&
		    TJS_SUCCEEDED(objthis->PropGet(0, TJS_W("provinceImageBufferForWrite"), NULL, &vpBuf, objthis)) &&
		    TJS_SUCCEEDED(objthis->PropGet(0, TJS_W("provinceImageBufferPitch"), NULL, &vpPitch, objthis))) {
			unsigned char *pbuf = reinterpret_cast<unsigned char *>((tjs_intptr_t)(tjs_int64)vpBuf);
			tjs_int ppitch = (tjs_int)vpPitch;
			if (pbuf) {
				for (tjs_int y = 0; y < ni->_height; ++y) {
					const unsigned char *srow = ni->_buffer + y * ni->_pitch;
					unsigned char *prow = pbuf + y * ppitch;
					for (tjs_int x = 0; x < ni->_width; ++x) {
						tjs_int a = srow[x * 4 + 3];
						if (a >= threshold) prow[x] = (unsigned char)matchedVal;
						else if (clearVal >= 0) prow[x] = (unsigned char)clearVal;
					}
				}
			}
		}
		if (result) result->Clear();
		return TJS_S_OK;
	}
};

class FnLayerClipAlphaRect : public tTJSDispatch {
	tjs_error TJS_INTF_METHOD FuncCall(
		tjs_uint32, const tjs_char *m, tjs_uint32 *, tTJSVariant *result,
		tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *objthis) override {
		if (m) return TJS_E_MEMBERNOTFOUND;
		if (!objthis) return TJS_E_NATIVECLASSCRASH;
		if (numparams < 7) return TJS_E_BADPARAMCOUNT;
		tjs_int dleft = (tjs_int)*param[0];
		tjs_int dtop = (tjs_int)*param[1];
		iTJSDispatch2 *srcObj = param[2]->AsObjectNoAddRef();
		tjs_int sleft = (tjs_int)*param[3];
		tjs_int stop = (tjs_int)*param[4];
		tjs_int sw = (tjs_int)*param[5];
		tjs_int sh = (tjs_int)*param[6];
		tjs_int clearVal = (numparams >= 8 && param[7]->Type() != tvtVoid) ? (tjs_int)*param[7] : -1;

		NI_LayerExBase *dst = NI_LayerExBase::getNative(objthis);
		NI_LayerExBase *src = srcObj ? NI_LayerExBase::getNative(srcObj) : NULL;
		if (!dst || !src) return TJS_S_OK;
		dst->reset(objthis);
		src->reset(srcObj);
		if (!dst->_buffer || !src->_buffer) return TJS_S_OK;

		for (tjs_int y = 0; y < dst->_height; ++y) {
			unsigned char *drow = dst->_buffer + y * dst->_pitch;
			tjs_int sy = y - dtop + stop;
			bool inY = (y >= dtop && y < dtop + sh && sy >= 0 && sy < src->_height);
			const unsigned char *srow = inY ? (src->_buffer + sy * src->_pitch) : NULL;
			for (tjs_int x = 0; x < dst->_width; ++x) {
				tjs_int sx = x - dleft + sleft;
				if (inY && x >= dleft && x < dleft + sw && sx >= 0 && sx < src->_width) {
					unsigned int da = drow[x * 4 + 3];
					unsigned int sa = srow[sx * 4 + 3];
					drow[x * 4 + 3] = (unsigned char)((da * sa + 255u) >> 8);
				} else if (clearVal >= 0) {
					drow[x * 4 + 3] = (unsigned char)clearVal;
				}
			}
		}
		dst->redraw(objthis);
		if (result) result->Clear();
		return TJS_S_OK;
	}
};

class FnLayerOverwrapRect : public tTJSDispatch {
	tjs_error TJS_INTF_METHOD FuncCall(
		tjs_uint32, const tjs_char *m, tjs_uint32 *, tTJSVariant *result,
		tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *objthis) override {
		if (m) return TJS_E_MEMBERNOTFOUND;
		if (!objthis) return TJS_E_NATIVECLASSCRASH;
		if (numparams < 7) return TJS_E_BADPARAMCOUNT;
		tjs_int dleft = (tjs_int)*param[0];
		tjs_int dtop = (tjs_int)*param[1];
		iTJSDispatch2 *srcObj = param[2]->AsObjectNoAddRef();
		tjs_int sleft = (tjs_int)*param[3];
		tjs_int stop = (tjs_int)*param[4];
		tjs_int sw = (tjs_int)*param[5];
		tjs_int sh = (tjs_int)*param[6];
		tjs_int opa = (numparams >= 8 && param[7]->Type() != tvtVoid) ? (tjs_int)*param[7] : 255;
		if (opa <= 0) return TJS_S_OK;
		if (opa > 255) opa = 255;

		NI_LayerExBase *dst = NI_LayerExBase::getNative(objthis);
		NI_LayerExBase *src = srcObj ? NI_LayerExBase::getNative(srcObj) : NULL;
		if (!dst || !src) return TJS_S_OK;
		dst->reset(objthis);
		src->reset(srcObj);
		if (!dst->_buffer || !src->_buffer) return TJS_S_OK;

		for (tjs_int y = 0; y < sh; ++y) {
			tjs_int dy = dtop + y, sy = stop + y;
			if (dy < 0 || dy >= dst->_height || sy < 0 || sy >= src->_height) continue;
			unsigned char *drow = dst->_buffer + dy * dst->_pitch;
			const unsigned char *srow = src->_buffer + sy * src->_pitch;
			for (tjs_int x = 0; x < sw; ++x) {
				tjs_int dx = dleft + x, sx = sleft + x;
				if (dx < 0 || dx >= dst->_width || sx < 0 || sx >= src->_width) continue;
				unsigned char *dp = drow + dx * 4;
				const unsigned char *sp = srow + sx * 4;
				unsigned int sa = (sp[3] * (unsigned int)opa + 255u) >> 8;
				if (sa == 0) continue;
				if (sa == 255) {
					std::memcpy(dp, sp, 4);
				} else {
					unsigned int da = dp[3];
					unsigned int outA = sa + ((da * (255u - sa) + 255u) >> 8);
					if (outA > 0) {
						dp[0] = (unsigned char)((sp[0] * sa + ((dp[0] * da * (255u - sa)) >> 8)) / outA);
						dp[1] = (unsigned char)((sp[1] * sa + ((dp[1] * da * (255u - sa)) >> 8)) / outA);
						dp[2] = (unsigned char)((sp[2] * sa + ((dp[2] * da * (255u - sa)) >> 8)) / outA);
					}
					dp[3] = (unsigned char)outA;
				}
			}
		}
		dst->redraw(objthis);
		if (result) result->Clear();
		return TJS_S_OK;
	}
};

class FnLayerShrinkCopy : public tTJSDispatch {
	tjs_error TJS_INTF_METHOD FuncCall(
		tjs_uint32, const tjs_char *m, tjs_uint32 *, tTJSVariant *result,
		tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *objthis) override {
		if (m) return TJS_E_MEMBERNOTFOUND;
		if (!objthis) return TJS_E_NATIVECLASSCRASH;
		if (numparams < 9) return TJS_E_BADPARAMCOUNT;
		tjs_int dleft = (tjs_int)*param[0];
		tjs_int dtop = (tjs_int)*param[1];
		tjs_int dw = (tjs_int)*param[2];
		tjs_int dh = (tjs_int)*param[3];
		iTJSDispatch2 *srcObj = param[4]->AsObjectNoAddRef();
		tjs_int sleft = (tjs_int)*param[5];
		tjs_int stop = (tjs_int)*param[6];
		tjs_int sw = (tjs_int)*param[7];
		tjs_int sh = (tjs_int)*param[8];
		if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0 || !srcObj) return TJS_S_OK;

		NI_LayerExBase *dst = NI_LayerExBase::getNative(objthis);
		NI_LayerExBase *src = NI_LayerExBase::getNative(srcObj);
		if (!dst || !src) return TJS_S_OK;
		dst->reset(objthis);
		src->reset(srcObj);
		if (!dst->_buffer || !src->_buffer) return TJS_S_OK;

		// Snapshot source region if shrinking in-place (srcObj == objthis)
		std::vector<unsigned char> tempSrc;
		const unsigned char *srcBase = src->_buffer;
		tjs_int srcPitch = src->_pitch;
		tjs_int srcW = src->_width, srcH = src->_height;
		if (srcObj == objthis) {
			srcPitch = srcW * 4;
			tempSrc.resize((size_t)srcPitch * (size_t)srcH);
			for (tjs_int y = 0; y < srcH; ++y) {
				std::memcpy(&tempSrc[(size_t)y * (size_t)srcPitch], src->_buffer + y * src->_pitch, (size_t)srcPitch);
			}
			srcBase = &tempSrc[0];
		}

		for (tjs_int dy = 0; dy < dh; ++dy) {
			tjs_int outY = dtop + dy;
			if (outY < 0 || outY >= dst->_height) continue;
			tjs_int sy0 = stop + (tjs_int)((tjs_int64)dy * sh / dh);
			tjs_int sy1 = stop + (tjs_int)(((tjs_int64)(dy + 1) * sh + dh - 1) / dh);
			if (sy0 < 0) sy0 = 0;
			if (sy1 > srcH) sy1 = srcH;
			if (sy1 <= sy0) sy1 = std::min(srcH, sy0 + 1);

			unsigned char *drow = dst->_buffer + outY * dst->_pitch;
			for (tjs_int dx = 0; dx < dw; ++dx) {
				tjs_int outX = dleft + dx;
				if (outX < 0 || outX >= dst->_width) continue;
				tjs_int sx0 = sleft + (tjs_int)((tjs_int64)dx * sw / dw);
				tjs_int sx1 = sleft + (tjs_int)(((tjs_int64)(dx + 1) * sw + dw - 1) / dw);
				if (sx0 < 0) sx0 = 0;
				if (sx1 > srcW) sx1 = srcW;
				if (sx1 <= sx0) sx1 = std::min(srcW, sx0 + 1);

				unsigned int sumB = 0, sumG = 0, sumR = 0, sumA = 0, cnt = 0;
				for (tjs_int sy = sy0; sy < sy1; ++sy) {
					const unsigned char *srow = srcBase + sy * srcPitch;
					for (tjs_int sx = sx0; sx < sx1; ++sx) {
						const unsigned char *p = srow + sx * 4;
						unsigned int a = p[3];
						sumB += p[0] * a;
						sumG += p[1] * a;
						sumR += p[2] * a;
						sumA += a;
						++cnt;
					}
				}
				unsigned char *dp = drow + outX * 4;
				if (cnt == 0 || sumA == 0) {
					dp[0] = dp[1] = dp[2] = dp[3] = 0;
				} else {
					dp[0] = (unsigned char)(sumB / sumA);
					dp[1] = (unsigned char)(sumG / sumA);
					dp[2] = (unsigned char)(sumR / sumA);
					dp[3] = (unsigned char)(sumA / cnt);
				}
			}
		}
		dst->redraw(objthis);
		if (result) result->Clear();
		return TJS_S_OK;
	}
};

class FnLayerCopyRaster : public tTJSDispatch {
	bool inPlace;
public:
	explicit FnLayerCopyRaster(bool ip) : inPlace(ip) {}
	tjs_error TJS_INTF_METHOD FuncCall(
		tjs_uint32, const tjs_char *m, tjs_uint32 *, tTJSVariant *result,
		tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *objthis) override {
		if (m) return TJS_E_MEMBERNOTFOUND;
		if (!objthis) return TJS_E_NATIVECLASSCRASH;
		int idx = 0;
		iTJSDispatch2 *srcObj = objthis;
		if (!inPlace) {
			if (numparams < 5) return TJS_E_BADPARAMCOUNT;
			srcObj = param[idx++]->AsObjectNoAddRef();
		} else {
			if (numparams < 4) return TJS_E_BADPARAMCOUNT;
		}
		double maxOffset = (double)(tTVReal)*param[idx++];
		tjs_int lines = (tjs_int)*param[idx++];
		tjs_int cycle = (tjs_int)*param[idx++];
		tjs_int64 timeMs = (tjs_int64)*param[idx++];
		if (lines <= 0) lines = 1;
		if (cycle <= 0) cycle = 1;

		NI_LayerExBase *dst = NI_LayerExBase::getNative(objthis);
		NI_LayerExBase *src = srcObj ? NI_LayerExBase::getNative(srcObj) : NULL;
		if (!dst || !src) return TJS_S_OK;
		dst->reset(objthis);
		src->reset(srcObj);
		if (!dst->_buffer || !src->_buffer) return TJS_S_OK;

		std::vector<unsigned char> tempSrc;
		const unsigned char *srcBase = src->_buffer;
		tjs_int srcPitch = src->_pitch;
		if (srcObj == objthis) {
			srcPitch = src->_width * 4;
			tempSrc.resize((size_t)srcPitch * (size_t)src->_height);
			for (tjs_int y = 0; y < src->_height; ++y) {
				std::memcpy(&tempSrc[(size_t)y * (size_t)srcPitch], src->_buffer + y * src->_pitch, (size_t)srcPitch);
			}
			srcBase = &tempSrc[0];
		}

		const double twoPi = 6.28318530717958647692;
		const double phaseBase = (double)(timeMs % cycle) / (double)cycle;
		const tjs_int h = std::min(dst->_height, src->_height);
		const tjs_int w = std::min(dst->_width, src->_width);
		for (tjs_int y = 0; y < h; ++y) {
			double rad = twoPi * ((double)(y % lines) / (double)lines + phaseBase);
			tjs_int dx = (tjs_int)std::lround(maxOffset * std::sin(rad));
			unsigned char *drow = dst->_buffer + y * dst->_pitch;
			const unsigned char *srow = srcBase + y * srcPitch;
			for (tjs_int x = 0; x < w; ++x) {
				tjs_int sx = x + dx;
				if (sx >= 0 && sx < src->_width) {
					std::memcpy(drow + x * 4, srow + sx * 4, 4);
				} else {
					std::memset(drow + x * 4, 0, 4);
				}
			}
		}
		dst->redraw(objthis);
		if (result) result->Clear();
		return TJS_S_OK;
	}
};

static void PostRegistPackinOne() {
	// Initialize NI_LayerExBase property handles & native class ID
	if ((NI_LayerExBase::classId = TJSFindNativeClassID(TJS_W("LayerExBase"))) <= 0) {
		NI_LayerExBase::classId = TJSRegisterNativeClass(TJS_W("LayerExBase"));
	}
	{
		iTJSDispatch2 *global = TVPGetScriptDispatch();
		if (global) {
			tTJSVariant layerVar;
			if (TJS_SUCCEEDED(global->PropGet(0, TJS_W("Layer"), NULL, &layerVar, global)) &&
			    layerVar.Type() == tvtObject && layerVar.AsObjectNoAddRef()) {
				NI_LayerExBase::init(layerVar.AsObjectNoAddRef());
			}
			global->Release();
		}
	}

	// Scripts constants & methods
	RegisterIntConstant(TJS_W("Scripts"), TJS_W("pfMemberEnsure"), TJS_MEMBERENSURE);
	RegisterIntConstant(TJS_W("Scripts"), TJS_W("pfMemberMustExist"), TJS_MEMBERMUSTEXIST);
	RegisterIntConstant(TJS_W("Scripts"), TJS_W("pfIgnoreProp"), TJS_IGNOREPROP);
	RegisterIntConstant(TJS_W("Scripts"), TJS_W("pfHiddenMember"), TJS_HIDDENMEMBER);
	RegisterIntConstant(TJS_W("Scripts"), TJS_W("pfStaticMember"), TJS_STATICMEMBER);
	RegisterDispatchMember(TJS_W("Scripts"), TJS_W("getObjectKeys"), new FnGetObjectKeys());
	RegisterDispatchMember(TJS_W("Scripts"), TJS_W("getObjectCount"), new FnGetObjectCount());
	RegisterDispatchMember(TJS_W("Scripts"), TJS_W("foreach"), new FnForeach());
	RegisterDispatchMember(TJS_W("Scripts"), TJS_W("clone"), new FnClone());
	RegisterDispatchMember(TJS_W("Scripts"), TJS_W("equalStruct"), new FnEqualStruct(false));
	RegisterDispatchMember(TJS_W("Scripts"), TJS_W("equalStructNumericLoose"), new FnEqualStruct(true));
	RegisterDispatchMember(TJS_W("Scripts"), TJS_W("getObjectContext"), new FnGetObjectContext());
	RegisterDispatchMember(TJS_W("Scripts"), TJS_W("isNullContext"), new FnIsNullContext());
	RegisterDispatchMember(TJS_W("Scripts"), TJS_W("setCallMissing"), new FnSetCallMissing());
	RegisterDispatchMember(TJS_W("Scripts"), TJS_W("getClassNames"), new FnGetClassNames());
	RegisterDispatchMember(TJS_W("Scripts"), TJS_W("propSet"), new FnPropSet());
	RegisterDispatchMember(TJS_W("Scripts"), TJS_W("propGet"), new FnPropGet());
	RegisterDispatchMember(TJS_W("Scripts"), TJS_W("safeEvalStorage"), new FnSafeEvalStorage());
	RegisterDispatchMember(TJS_W("Scripts"), TJS_W("getMD5HashString"), new FnScriptsMd5());
	RegisterDispatchMember(TJS_W("Scripts"), TJS_W("rehash"), new FnNoOp());
	RegisterDispatchMember(TJS_W("Scripts"), TJS_W("setEvalErrorLog"), new FnNoOp());

	// Storages methods
	RegisterDispatchMember(TJS_W("Storages"), TJS_W("isExistentDirectory"), new FnIsExistentDirectory());
	RegisterDispatchMember(TJS_W("Storages"), TJS_W("isExistentStorageNoSearchNoNormalize"), new FnIsExistentStorageNoSearchNoNormalize());
	RegisterDispatchMember(TJS_W("Storages"), TJS_W("dirlist"), new FnDirList());
	RegisterDispatchMember(TJS_W("Storages"), TJS_W("deleteFile"), new FnDeleteFile());
	RegisterDispatchMember(TJS_W("Storages"), TJS_W("moveFile"), new FnMoveFile());
	RegisterDispatchMember(TJS_W("Storages"), TJS_W("copyFile"), new FnCopyFile());
	RegisterDispatchMember(TJS_W("Storages"), TJS_W("copyFileNoNormalize"), new FnCopyFile());
	RegisterDispatchMember(TJS_W("Storages"), TJS_W("mkdir"), new FnMkdir());
	RegisterDispatchMember(TJS_W("Storages"), TJS_W("loadOctet"), new FnLoadOctet());
	RegisterDispatchMember(TJS_W("Storages"), TJS_W("saveOctet"), new FnSaveOctet());
	RegisterDispatchMember(TJS_W("Storages"), TJS_W("clearStorageCaches"), new FnClearStorageCaches());
	RegisterDispatchMember(TJS_W("Storages"), TJS_W("clearCache"), new FnClearStorageCaches());
	RegisterDispatchMember(TJS_W("Storages"), TJS_W("getLastModifiedFileTime"), new FnGetLastModifiedFileTime());
	RegisterDispatchMember(TJS_W("Storages"), TJS_W("getMD5HashString"), new FnStoragesMd5());

	// Layer methods
	RegisterDispatchMember(TJS_W("Layer"), TJS_W("fillAlpha"), new FnLayerFillAlpha());
	RegisterDispatchMember(TJS_W("Layer"), TJS_W("copyBottomBlueToTopAlpha"), new FnLayerCopyBottomBlueToTopAlpha());
	RegisterDispatchMember(TJS_W("Layer"), TJS_W("copyRightBlueToLeftAlpha"), new FnLayerCopyRightBlueToLeftAlpha());
	RegisterDispatchMember(TJS_W("Layer"), TJS_W("copyAlphaToProvince"), new FnLayerCopyAlphaToProvince());
	RegisterDispatchMember(TJS_W("Layer"), TJS_W("clipAlphaRect"), new FnLayerClipAlphaRect());
	RegisterDispatchMember(TJS_W("Layer"), TJS_W("overwrapRect"), new FnLayerOverwrapRect());
	RegisterDispatchMember(TJS_W("Layer"), TJS_W("shrinkCopy"), new FnLayerShrinkCopy());
	RegisterDispatchMember(TJS_W("Layer"), TJS_W("copyRaster"), new FnLayerCopyRaster(false));
	RegisterDispatchMember(TJS_W("Layer"), TJS_W("raster"), new FnLayerCopyRaster(true));

	// System stubs for PC-only calls
	RegisterDispatchMember(TJS_W("System"), TJS_W("createAppLock"), new FnNoOp(1));
	RegisterDispatchMember(TJS_W("System"), TJS_W("setDpiAwareness"), new FnNoOp(1));
}

} // namespace

NCB_POST_REGIST_CALLBACK(PostRegistPackinOne);
