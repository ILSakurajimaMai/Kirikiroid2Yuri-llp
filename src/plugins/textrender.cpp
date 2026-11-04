// textrender.cpp - Built-in native implementation of Wamsoft's textrender.dll (TextRenderBase)
// Powers TextRender, LangRender, MsgwinRender, and Backlog layout & word-wrapping in Lip lipples.

#define NCB_MODULE_NAME TJS_W("textrender.dll")

#include "PluginStub.h"
#include "ncbind/ncbind.hpp"
#include "textrender_core.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#ifdef TJS_NATIVE_CLASSID_NAME
#undef TJS_NATIVE_CLASSID_NAME
#undef TJS_NCM_REG_THIS
#undef TJS_NATIVE_SET_ClassID
#endif
#define TJS_NCM_REG_THIS classobj
#define TJS_NATIVE_SET_ClassID TJS_NATIVE_CLASSID_NAME = TJS_NCM_CLASSID;
#define TJS_NATIVE_CLASSID_NAME ClassID_TextRenderBase

static tjs_int32 ClassID_TextRenderBase = -1;

namespace {

static std::u16string TtstrToU16(const ttstr &s) {
	const tjs_char *p = s.c_str();
	tjs_int len = s.GetLen();
	std::u16string out;
	if (!p || len <= 0) return out;
	out.resize((size_t)len);
	for (tjs_int i = 0; i < len; ++i) {
		out[(size_t)i] = (char16_t)p[i];
	}
	return out;
}

static ttstr U16ToTtstr(const std::u16string &u) {
	if (u.empty()) return ttstr();
	std::vector<tjs_char> buf(u.size() + 1);
	for (size_t i = 0; i < u.size(); ++i) {
		buf[i] = (tjs_char)u[i];
	}
	buf[u.size()] = 0;
	return ttstr(&buf[0]);
}

static bool DictGetProp(iTJSDispatch2 *dic, const tjs_char *key, tTJSVariant &out) {
	if (!dic) return false;
	out.Clear();
	if (TJS_FAILED(dic->PropGet(TJS_MEMBERMUSTEXIST, key, NULL, &out, dic))) {
		return false;
	}
	return out.Type() != tvtVoid;
}

static bool DictGetProp2(iTJSDispatch2 *dic, const tjs_char *k1, const tjs_char *k2, tTJSVariant &out) {
	if (DictGetProp(dic, k1, out)) return true;
	if (k2 && DictGetProp(dic, k2, out)) return true;
	return false;
}

static void DictSetInt(iTJSDispatch2 *dic, const tjs_char *key, tjs_int64 val) {
	tTJSVariant v(val);
	dic->PropSet(TJS_MEMBERENSURE, key, NULL, &v, dic);
}

static void DictSetReal(iTJSDispatch2 *dic, const tjs_char *key, tTVReal val) {
	tTJSVariant v(val);
	dic->PropSet(TJS_MEMBERENSURE, key, NULL, &v, dic);
}

static void DictSetStr(iTJSDispatch2 *dic, const tjs_char *key, const ttstr &val) {
	tTJSVariant v(val);
	dic->PropSet(TJS_MEMBERENSURE, key, NULL, &v, dic);
}

static void ArrayPushObj(iTJSDispatch2 *arr, tjs_int idx, iTJSDispatch2 *item) {
	tTJSVariant v(item, item);
	arr->PropSetByNum(TJS_MEMBERENSURE, idx, &v, arr);
}

class NI_TextRenderBase : public tTJSNativeInstance {
public:
	textrender::Engine eng;
	iTJSDispatch2 *selfObj = nullptr; // weak reference to owning TJS object (avoid ref cycle)

	NI_TextRenderBase() = default;

	tjs_error TJS_INTF_METHOD Construct(tjs_int /*numparams*/, tTJSVariant ** /*param*/, iTJSDispatch2 *tjs_obj) override {
		selfObj = tjs_obj;

		eng.onEval = [this](const std::u16string &expr) -> std::u16string {
			if (!selfObj) return std::u16string();
			tTJSVariant arg(U16ToTtstr(expr));
			tTJSVariant *pargs[1] = { &arg };
			tTJSVariant res;
			if (TJS_SUCCEEDED(selfObj->FuncCall(0, TJS_W("onEval"), NULL, &res, 1, pargs, selfObj)) &&
			    res.Type() != tvtVoid) {
				return TtstrToU16(ttstr(res));
			}
			return std::u16string();
		};

		eng.onLabel = [this](const std::u16string &label) -> double {
			if (!selfObj) return 0.0;
			tTJSVariant arg(U16ToTtstr(label));
			tTJSVariant *pargs[1] = { &arg };
			tTJSVariant res;
			if (TJS_SUCCEEDED(selfObj->FuncCall(0, TJS_W("onLabel"), NULL, &res, 1, pargs, selfObj)) &&
			    (res.Type() == tvtInteger || res.Type() == tvtReal)) {
				return (double)res.AsReal();
			}
			return 0.0;
		};

		eng.onFontChange = [this](const std::u16string &face, int bold, int italic) {
			if (!selfObj) return;
			iTJSDispatch2 *dic = TJSCreateDictionaryObject();
			if (!dic) return;
			DictSetStr(dic, TJS_W("face"), U16ToTtstr(face));
			DictSetInt(dic, TJS_W("bold"), bold);
			DictSetInt(dic, TJS_W("italic"), italic);
			tTJSVariant arg(dic, dic);
			dic->Release();
			tTJSVariant *pargs[1] = { &arg };
			selfObj->FuncCall(0, TJS_W("onFontChange"), NULL, NULL, 1, pargs, selfObj);
		};

		eng.onGetTextWidth = [this](const std::u16string &s, int pxSize) -> double {
			if (!selfObj) return 0.0;
			tTJSVariant arg0(U16ToTtstr(s));
			tTJSVariant arg1((tjs_int)pxSize);
			tTJSVariant *pargs[2] = { &arg0, &arg1 };
			tTJSVariant res;
			if (TJS_SUCCEEDED(selfObj->FuncCall(0, TJS_W("onGetTextWidth"), NULL, &res, 2, pargs, selfObj)) &&
			    (res.Type() == tvtInteger || res.Type() == tvtReal)) {
				return (double)res.AsReal();
			}
			return 0.0;
		};

		eng.onGetGraphSize = [this](const std::u16string &name, int &w, int &h) {
			if (!selfObj) return;
			tTJSVariant arg(U16ToTtstr(name));
			tTJSVariant *pargs[1] = { &arg };
			tTJSVariant res;
			if (TJS_SUCCEEDED(selfObj->FuncCall(0, TJS_W("onGetGraphSize"), NULL, &res, 1, pargs, selfObj)) &&
			    res.Type() == tvtObject && res.AsObjectNoAddRef()) {
				iTJSDispatch2 *dic = res.AsObjectNoAddRef();
				tTJSVariant vw, vh;
				if (DictGetProp(dic, TJS_W("width"), vw)) w = (int)(tjs_int)vw;
				if (DictGetProp(dic, TJS_W("height"), vh)) h = (int)(tjs_int)vh;
			}
		};

		return TJS_S_OK;
	}

	void TJS_INTF_METHOD Invalidate() override {
		selfObj = nullptr;
	}

	void SetOption(iTJSDispatch2 *dic) {
		if (!dic) return;
		tTJSVariant v;
		if (DictGetProp(dic, TJS_W("following"), v)) eng.optFollowing = TtstrToU16(ttstr(v));
		if (DictGetProp(dic, TJS_W("leading"), v)) eng.optLeading = TtstrToU16(ttstr(v));
		if (DictGetProp(dic, TJS_W("begin"), v)) eng.optBegin = TtstrToU16(ttstr(v));
		if (DictGetProp(dic, TJS_W("end"), v)) eng.optEnd = TtstrToU16(ttstr(v));
		if (DictGetProp(dic, TJS_W("vertical"), v)) eng.vertical = ((tjs_int)v != 0) ? 1 : 0;
		if (DictGetProp2(dic, TJS_W("kinsoku_max"), TJS_W("kinsokuMax"), v)) eng.kinsokuMax = (int)(tjs_int)v;
		if (DictGetProp2(dic, TJS_W("word_break"), TJS_W("wordbreak"), v) || DictGetProp(dic, TJS_W("wordBreak"), v)) {
			bool wb = ((tjs_int)v != 0);
			eng.defaultWordBreak = wb;
			eng.wordBreak = wb;
		}
		if (DictGetProp2(dic, TJS_W("width_time_scale"), TJS_W("widthTimeScale"), v)) {
			eng.widthTimeScale = (double)v.AsReal();
		}
		if (DictGetProp2(dic, TJS_W("ignore_color"), TJS_W("ignoreColor"), v)) eng.ignoreColor = ((tjs_int)v != 0);
		if (DictGetProp2(dic, TJS_W("ignore_size"), TJS_W("ignoreSize"), v)) eng.ignoreSize = ((tjs_int)v != 0);
		if (DictGetProp2(dic, TJS_W("ignore_delay"), TJS_W("ignoreDelay"), v)) eng.ignoreDelay = ((tjs_int)v != 0);
		if (DictGetProp2(dic, TJS_W("ignore_over"), TJS_W("ignoreOver"), v)) eng.ignoreOverY = ((tjs_int)v != 0);
		if (DictGetProp2(dic, TJS_W("ignore_overy"), TJS_W("ignoreOverY"), v)) eng.ignoreOverY = ((tjs_int)v != 0);
		if (DictGetProp2(dic, TJS_W("ignore_overx"), TJS_W("ignoreOverX"), v)) eng.ignoreOverX = ((tjs_int)v != 0);
		if (DictGetProp2(dic, TJS_W("ignore_ruby"), TJS_W("ignoreRuby"), v)) eng.ignoreRuby = ((tjs_int)v != 0);
		if (DictGetProp2(dic, TJS_W("ignore_type"), TJS_W("ignoreType"), v)) eng.ignoreType = ((tjs_int)v != 0);
		if (DictGetProp2(dic, TJS_W("ignore_face"), TJS_W("ignoreFace"), v)) eng.ignoreFace = ((tjs_int)v != 0);
		if (DictGetProp2(dic, TJS_W("ignore_style"), TJS_W("ignoreStyle"), v)) eng.ignoreStyle = ((tjs_int)v != 0);
		if (DictGetProp2(dic, TJS_W("ignore_xr"), TJS_W("ignoreXr"), v)) eng.ignoreXr = ((tjs_int)v != 0);
	}

	void SetDefault(iTJSDispatch2 *dic) {
		if (!dic) return;
		tTJSVariant v;
		if (DictGetProp(dic, TJS_W("face"), v)) eng.defaultFace = TtstrToU16(ttstr(v));
		if (DictGetProp(dic, TJS_W("bold"), v)) eng.defaultBold = ((tjs_int)v != 0) ? 1 : 0;
		if (DictGetProp(dic, TJS_W("italic"), v)) eng.defaultItalic = ((tjs_int)v != 0) ? 1 : 0;

		bool hasFontSize = DictGetProp2(dic, TJS_W("fontsize"), TJS_W("fontSize"), v);
		if (hasFontSize) {
			int fs = std::max(1, (int)std::lround(v.AsReal()));
			eng.defaultFontSize = fs;
			tTJSVariant tmp;
			if (!DictGetProp2(dic, TJS_W("bigfontsize"), TJS_W("bigFontSize"), tmp)) eng.bigFontSize = fs * 2;
			if (!DictGetProp2(dic, TJS_W("smallfontsize"), TJS_W("smallFontSize"), tmp)) eng.smallFontSize = std::max(1, fs / 2);
			if (!DictGetProp2(dic, TJS_W("rubysize"), TJS_W("rubySize"), tmp)) eng.defaultRubySize = std::max(1, fs / 3);
			if (!DictGetProp2(dic, TJS_W("linesize"), TJS_W("lineSize"), tmp)) eng.defaultLineSize = fs;
		}
		if (DictGetProp2(dic, TJS_W("bigfontsize"), TJS_W("bigFontSize"), v)) eng.bigFontSize = std::max(1, (int)std::lround(v.AsReal()));
		if (DictGetProp2(dic, TJS_W("smallfontsize"), TJS_W("smallFontSize"), v)) eng.smallFontSize = std::max(1, (int)std::lround(v.AsReal()));
		if (DictGetProp2(dic, TJS_W("rubysize"), TJS_W("rubySize"), v)) eng.defaultRubySize = std::max(1, (int)std::lround(v.AsReal()));
		if (DictGetProp2(dic, TJS_W("rubyoffset"), TJS_W("rubyOffset"), v)) eng.defaultRubyOffset = (int)std::lround(v.AsReal());
		if (DictGetProp2(dic, TJS_W("color"), TJS_W("chColor"), v)) eng.defaultChColor = (uint32_t)(tjs_int64)v | 0xff000000u;
		if (DictGetProp(dic, TJS_W("shadow"), v)) eng.defaultShadow = ((tjs_int)v != 0) ? 1 : 0;
		if (DictGetProp2(dic, TJS_W("shadowcolor"), TJS_W("shadowColor"), v)) eng.defaultShadowColor = (uint32_t)(tjs_int64)v | 0xff000000u;
		if (DictGetProp2(dic, TJS_W("shadowdiff"), TJS_W("shadowDiff"), v)) eng.defaultShadowDiff = (int)(tjs_int)v;
		if (DictGetProp(dic, TJS_W("edge"), v)) eng.defaultEdge = ((tjs_int)v != 0) ? 1 : 0;
		if (DictGetProp2(dic, TJS_W("edgecolor"), TJS_W("edgeColor"), v)) eng.defaultEdgeColor = (uint32_t)(tjs_int64)v | 0xff000000u;
		if (DictGetProp2(dic, TJS_W("linespacing"), TJS_W("lineSpacing"), v)) eng.defaultLineSpacing = (int)std::lround(v.AsReal());
		if (DictGetProp(dic, TJS_W("pitch"), v)) eng.defaultPitch = (int)std::lround(v.AsReal());
		if (DictGetProp2(dic, TJS_W("linesize"), TJS_W("lineSize"), v)) eng.defaultLineSize = (int)std::lround(v.AsReal());
		if (DictGetProp(dic, TJS_W("align"), v)) eng.defaultAlign = (int)(tjs_int)v;
		if (DictGetProp(dic, TJS_W("valign"), v)) eng.defaultValign = (int)(tjs_int)v;
		if (DictGetProp2(dic, TJS_W("wordbreak"), TJS_W("wordBreak"), v) || DictGetProp(dic, TJS_W("word_break"), v)) {
			eng.defaultWordBreak = ((tjs_int)v != 0);
		}
	}

	void SetFont(iTJSDispatch2 *dic) {
		if (!dic) return;
		tTJSVariant v;
		bool fontNotifyNeeded = false;
		if (DictGetProp(dic, TJS_W("face"), v)) {
			std::u16string newFace = TtstrToU16(ttstr(v));
			if (newFace.empty()) newFace = eng.defaultFace;
			if (newFace != eng.face) {
				eng.face = newFace;
				fontNotifyNeeded = true;
			}
		}
		if (DictGetProp(dic, TJS_W("bold"), v)) {
			int nb = ((tjs_int)v != 0) ? 1 : 0;
			if (nb != eng.bold) { eng.bold = nb; fontNotifyNeeded = true; }
		}
		if (DictGetProp(dic, TJS_W("italic"), v)) {
			int ni = ((tjs_int)v != 0) ? 1 : 0;
			if (ni != eng.italic) { eng.italic = ni; fontNotifyNeeded = true; }
		}
		if (DictGetProp2(dic, TJS_W("fontsize"), TJS_W("fontSize"), v)) {
			eng.fontSize = std::max(1, (int)std::lround(v.AsReal() * eng.fontScale));
		}
		if (DictGetProp2(dic, TJS_W("rubysize"), TJS_W("rubySize"), v)) {
			eng.rubySize = std::max(1, (int)std::lround(v.AsReal() * eng.fontScale));
		}
		if (DictGetProp2(dic, TJS_W("rubyoffset"), TJS_W("rubyOffset"), v)) {
			eng.rubyOffset = (int)std::lround(v.AsReal() * eng.fontScale);
		}
		if (DictGetProp2(dic, TJS_W("color"), TJS_W("chColor"), v)) eng.chColor = (uint32_t)(tjs_int64)v | 0xff000000u;
		if (DictGetProp(dic, TJS_W("shadow"), v)) eng.shadow = ((tjs_int)v != 0) ? 1 : 0;
		if (DictGetProp2(dic, TJS_W("shadowcolor"), TJS_W("shadowColor"), v)) eng.shadowColor = (uint32_t)(tjs_int64)v | 0xff000000u;
		if (DictGetProp2(dic, TJS_W("shadowdiff"), TJS_W("shadowDiff"), v)) eng.shadowDiff = (int)(tjs_int)v;
		if (DictGetProp(dic, TJS_W("edge"), v)) eng.edge = ((tjs_int)v != 0) ? 1 : 0;
		if (DictGetProp2(dic, TJS_W("edgecolor"), TJS_W("edgeColor"), v)) eng.edgeColor = (uint32_t)(tjs_int64)v | 0xff000000u;
		if (fontNotifyNeeded) eng.notifyFontChange();
	}

	void SetStyle(iTJSDispatch2 *dic) {
		if (!dic) return;
		tTJSVariant v;
		if (DictGetProp2(dic, TJS_W("linespacing"), TJS_W("lineSpacing"), v)) {
			eng.lineSpacing = (int)std::lround(v.AsReal() * eng.lineScale);
		}
		if (DictGetProp(dic, TJS_W("pitch"), v)) eng.pitch = (int)std::lround(v.AsReal());
		if (DictGetProp2(dic, TJS_W("linesize"), TJS_W("lineSize"), v)) {
			eng.lineSize = std::max(1, (int)std::lround(v.AsReal() * eng.fontScale));
		} else if (DictGetProp2(dic, TJS_W("fontsize"), TJS_W("fontSize"), v)) {
			eng.lineSize = std::max(1, (int)std::lround(v.AsReal() * eng.fontScale));
		}
		if (DictGetProp(dic, TJS_W("align"), v)) eng.align = (int)(tjs_int)v;
		if (DictGetProp(dic, TJS_W("valign"), v)) eng.valign = (int)(tjs_int)v;
		if (DictGetProp2(dic, TJS_W("wordbreak"), TJS_W("wordBreak"), v) || DictGetProp(dic, TJS_W("word_break"), v)) {
			eng.wordBreak = ((tjs_int)v != 0);
		}
	}

	iTJSDispatch2 *BuildCharDict(const textrender::RenderedChar &rc) const {
		iTJSDispatch2 *d = TJSCreateDictionaryObject();
		if (!d) return NULL;
		DictSetInt(d, TJS_W("x"), rc.x);
		DictSetInt(d, TJS_W("y"), rc.y);
		DictSetInt(d, TJS_W("cw"), rc.cw);
		DictSetInt(d, TJS_W("size"), rc.size);
		DictSetStr(d, TJS_W("text"), U16ToTtstr(rc.text));
		DictSetInt(d, TJS_W("bold"), rc.bold);
		DictSetInt(d, TJS_W("italic"), rc.italic);
		DictSetStr(d, TJS_W("face"), U16ToTtstr(rc.face));
		DictSetInt(d, TJS_W("color"), (tjs_int64)rc.color);
		DictSetInt(d, TJS_W("shadowColor"), (tjs_int64)rc.shadowColor);
		DictSetInt(d, TJS_W("shadowDiff"), rc.shadowDiff);
		DictSetInt(d, TJS_W("edgeColor"), (tjs_int64)rc.edgeColor);

		if (rc.graph != 0) {
			if (rc.shadow) DictSetInt(d, TJS_W("shadow"), (tjs_int64)rc.shadowColor);
			if (rc.edge) DictSetInt(d, TJS_W("edge"), (tjs_int64)rc.edgeColor);
		} else {
			DictSetInt(d, TJS_W("shadow"), rc.shadow);
			DictSetInt(d, TJS_W("edge"), rc.edge);
		}

		if (rc.hasRuby && !rc.rubies.empty()) {
			iTJSDispatch2 *rArr = TJSCreateArrayObject();
			if (rArr) {
				for (size_t r = 0; r < rc.rubies.size(); ++r) {
					iTJSDispatch2 *rd = TJSCreateDictionaryObject();
					if (rd) {
						DictSetReal(rd, TJS_W("x"), rc.rubies[r].x);
						DictSetReal(rd, TJS_W("y"), rc.rubies[r].y);
						DictSetInt(rd, TJS_W("size"), rc.rubies[r].size);
						DictSetStr(rd, TJS_W("text"), U16ToTtstr(rc.rubies[r].text));
						ArrayPushObj(rArr, (tjs_int)r, rd);
						rd->Release();
					}
				}
				tTJSVariant rv(rArr, rArr);
				rArr->Release();
				d->PropSet(TJS_MEMBERENSURE, TJS_W("ruby"), NULL, &rv, d);
			}
		}

		DictSetInt(d, TJS_W("graph"), rc.graph);
		DictSetInt(d, TJS_W("vertical"), rc.vertical);
		ttstr linkStr = U16ToTtstr(rc.link);
		DictSetStr(d, TJS_W("link"), linkStr);
		if (!rc.link.empty()) {
			DictSetStr(d, TJS_W("linkName"), linkStr);
		}
		DictSetInt(d, TJS_W("line"), rc.line);
		DictSetReal(d, TJS_W("delay"), rc.delay);
		return d;
	}

	int ResolveLinkArg(const tTJSVariant &arg) const {
		if (arg.Type() == tvtString) {
			return eng.findLink(TtstrToU16(ttstr(arg)));
		}
		if (arg.Type() == tvtInteger || arg.Type() == tvtReal) {
			return (int)(tjs_int)arg;
		}
		return -1;
	}
};

static iTJSNativeInstance *TJS_INTF_METHOD Create_NI_TextRenderBase() {
	return new NI_TextRenderBase();
}

#define DECL_INT_PROP(PropName, ExprGet, StmtSet) \
	TJS_BEGIN_NATIVE_PROP_DECL(PropName) { \
		TJS_BEGIN_NATIVE_PROP_GETTER { \
			TJS_GET_NATIVE_INSTANCE(_this, NI_TextRenderBase); \
			*result = (tjs_int64)(ExprGet); \
			return TJS_S_OK; \
		} TJS_END_NATIVE_PROP_GETTER \
		TJS_BEGIN_NATIVE_PROP_SETTER { \
			TJS_GET_NATIVE_INSTANCE(_this, NI_TextRenderBase); \
			StmtSet; \
			return TJS_S_OK; \
		} TJS_END_NATIVE_PROP_SETTER \
	} TJS_END_NATIVE_PROP_DECL(PropName)

#define DECL_RO_INT_PROP(PropName, ExprGet) \
	TJS_BEGIN_NATIVE_PROP_DECL(PropName) { \
		TJS_BEGIN_NATIVE_PROP_GETTER { \
			TJS_GET_NATIVE_INSTANCE(_this, NI_TextRenderBase); \
			*result = (tjs_int64)(ExprGet); \
			return TJS_S_OK; \
		} TJS_END_NATIVE_PROP_GETTER \
		TJS_DENY_NATIVE_PROP_SETTER \
	} TJS_END_NATIVE_PROP_DECL(PropName)

#define DECL_REAL_PROP(PropName, ExprGet, StmtSet) \
	TJS_BEGIN_NATIVE_PROP_DECL(PropName) { \
		TJS_BEGIN_NATIVE_PROP_GETTER { \
			TJS_GET_NATIVE_INSTANCE(_this, NI_TextRenderBase); \
			*result = (tTVReal)(ExprGet); \
			return TJS_S_OK; \
		} TJS_END_NATIVE_PROP_GETTER \
		TJS_BEGIN_NATIVE_PROP_SETTER { \
			TJS_GET_NATIVE_INSTANCE(_this, NI_TextRenderBase); \
			StmtSet; \
			return TJS_S_OK; \
		} TJS_END_NATIVE_PROP_SETTER \
	} TJS_END_NATIVE_PROP_DECL(PropName)

#define DECL_RO_REAL_PROP(PropName, ExprGet) \
	TJS_BEGIN_NATIVE_PROP_DECL(PropName) { \
		TJS_BEGIN_NATIVE_PROP_GETTER { \
			TJS_GET_NATIVE_INSTANCE(_this, NI_TextRenderBase); \
			*result = (tTVReal)(ExprGet); \
			return TJS_S_OK; \
		} TJS_END_NATIVE_PROP_GETTER \
		TJS_DENY_NATIVE_PROP_SETTER \
	} TJS_END_NATIVE_PROP_DECL(PropName)

static iTJSDispatch2 *Create_NC_TextRenderBase() {
	tTJSNativeClassForPlugin *classobj =
		TJSCreateNativeClassForPlugin(TJS_W("TextRenderBase"), Create_NI_TextRenderBase);

	TJS_BEGIN_NATIVE_MEMBERS(TextRenderBase)

		TJS_DECL_EMPTY_FINALIZE_METHOD

		TJS_BEGIN_NATIVE_CONSTRUCTOR_DECL(_this, NI_TextRenderBase, TextRenderBase)
		{
			return TJS_S_OK;
		}
		TJS_END_NATIVE_CONSTRUCTOR_DECL(TextRenderBase)

		TJS_BEGIN_NATIVE_METHOD_DECL(clear)
		{
			TJS_GET_NATIVE_INSTANCE(_this, NI_TextRenderBase);
			_this->eng.clear();
			return TJS_S_OK;
		}
		TJS_END_NATIVE_METHOD_DECL(clear)

		TJS_BEGIN_NATIVE_METHOD_DECL(setRenderSize)
		{
			TJS_GET_NATIVE_INSTANCE(_this, NI_TextRenderBase);
			if (numparams < 2) return TJS_E_BADPARAMCOUNT;
			_this->eng.setRenderSize((int)(tjs_int)*param[0], (int)(tjs_int)*param[1]);
			return TJS_S_OK;
		}
		TJS_END_NATIVE_METHOD_DECL(setRenderSize)

		TJS_BEGIN_NATIVE_METHOD_DECL(setOption)
		{
			TJS_GET_NATIVE_INSTANCE(_this, NI_TextRenderBase);
			if (numparams >= 1 && param[0]->Type() == tvtObject) {
				_this->SetOption(param[0]->AsObjectNoAddRef());
			}
			return TJS_S_OK;
		}
		TJS_END_NATIVE_METHOD_DECL(setOption)

		TJS_BEGIN_NATIVE_METHOD_DECL(setDefault)
		{
			TJS_GET_NATIVE_INSTANCE(_this, NI_TextRenderBase);
			if (numparams >= 1 && param[0]->Type() == tvtObject) {
				_this->SetDefault(param[0]->AsObjectNoAddRef());
			}
			return TJS_S_OK;
		}
		TJS_END_NATIVE_METHOD_DECL(setDefault)

		TJS_BEGIN_NATIVE_METHOD_DECL(resetFont)
		{
			TJS_GET_NATIVE_INSTANCE(_this, NI_TextRenderBase);
			_this->eng.resetFont(true);
			return TJS_S_OK;
		}
		TJS_END_NATIVE_METHOD_DECL(resetFont)

		TJS_BEGIN_NATIVE_METHOD_DECL(resetStyle)
		{
			TJS_GET_NATIVE_INSTANCE(_this, NI_TextRenderBase);
			_this->eng.resetStyle();
			return TJS_S_OK;
		}
		TJS_END_NATIVE_METHOD_DECL(resetStyle)

		TJS_BEGIN_NATIVE_METHOD_DECL(setFont)
		{
			TJS_GET_NATIVE_INSTANCE(_this, NI_TextRenderBase);
			if (numparams >= 1 && param[0]->Type() == tvtObject) {
				_this->SetFont(param[0]->AsObjectNoAddRef());
			}
			return TJS_S_OK;
		}
		TJS_END_NATIVE_METHOD_DECL(setFont)

		TJS_BEGIN_NATIVE_METHOD_DECL(setStyle)
		{
			TJS_GET_NATIVE_INSTANCE(_this, NI_TextRenderBase);
			if (numparams >= 1 && param[0]->Type() == tvtObject) {
				_this->SetStyle(param[0]->AsObjectNoAddRef());
			}
			return TJS_S_OK;
		}
		TJS_END_NATIVE_METHOD_DECL(setStyle)

		TJS_BEGIN_NATIVE_METHOD_DECL(render)
		{
			TJS_GET_NATIVE_INSTANCE(_this, NI_TextRenderBase);
			if (numparams < 1) return TJS_E_BADPARAMCOUNT;
			std::u16string text = TtstrToU16(ttstr(*param[0]));
			int autoIndent = (numparams >= 2 && param[1]->Type() != tvtVoid) ? (int)(tjs_int)*param[1] : 0;
			double chSpeed = (numparams >= 3 && param[2]->Type() != tvtVoid) ? (double)param[2]->AsReal() : 0.0;
			int waitMode = (numparams >= 4 && param[3]->Type() != tvtVoid) ? (int)(tjs_int)*param[3] : 0;
			int same = (numparams >= 5 && param[4]->Type() != tvtVoid) ? (int)(tjs_int)*param[4] : 0;
			int plain = (numparams >= 6 && param[5]->Type() != tvtVoid) ? (int)(tjs_int)*param[5] : 0;
			int ov = _this->eng.render(text, autoIndent, chSpeed, waitMode, same, plain);
			if (result) *result = (tjs_int)ov;
			return TJS_S_OK;
		}
		TJS_END_NATIVE_METHOD_DECL(render)

		TJS_BEGIN_NATIVE_METHOD_DECL(newline)
		{
			TJS_GET_NATIVE_INSTANCE(_this, NI_TextRenderBase);
			_this->eng.newline();
			return TJS_S_OK;
		}
		TJS_END_NATIVE_METHOD_DECL(newline)

		TJS_BEGIN_NATIVE_METHOD_DECL(done)
		{
			TJS_GET_NATIVE_INSTANCE(_this, NI_TextRenderBase);
			_this->eng.done();
			return TJS_S_OK;
		}
		TJS_END_NATIVE_METHOD_DECL(done)

		TJS_BEGIN_NATIVE_METHOD_DECL(contains)
		{
			TJS_GET_NATIVE_INSTANCE(_this, NI_TextRenderBase);
			if (numparams < 2) return TJS_E_BADPARAMCOUNT;
			bool ok = _this->eng.contains((int)(tjs_int)*param[0], (int)(tjs_int)*param[1]);
			if (result) *result = (tjs_int)(ok ? 1 : 0);
			return TJS_S_OK;
		}
		TJS_END_NATIVE_METHOD_DECL(contains)

		TJS_BEGIN_NATIVE_METHOD_DECL(getCharacters)
		{
			TJS_GET_NATIVE_INSTANCE(_this, NI_TextRenderBase);
			size_t total = _this->eng.chars.size();
			int start = (numparams >= 1 && param[0]->Type() != tvtVoid) ? (int)(tjs_int)*param[0] : 0;
			int count = (numparams >= 2 && param[1]->Type() != tvtVoid) ? (int)(tjs_int)*param[1] : 0;
			if (start < 0) start = 0;
			size_t sIdx = std::min((size_t)start, total);
			size_t eIdx = (count > 0) ? std::min(total, sIdx + (size_t)count) : total;

			iTJSDispatch2 *arr = TJSCreateArrayObject();
			tjs_int outIdx = 0;
			for (size_t i = sIdx; i < eIdx; ++i) {
				iTJSDispatch2 *cd = _this->BuildCharDict(_this->eng.chars[i]);
				if (cd) {
					ArrayPushObj(arr, outIdx++, cd);
					cd->Release();
				}
			}
			if (result) {
				result->SetObject(arr, arr);
			}
			arr->Release();
			return TJS_S_OK;
		}
		TJS_END_NATIVE_METHOD_DECL(getCharacters)

		TJS_BEGIN_NATIVE_METHOD_DECL(getKeyWait)
		{
			TJS_GET_NATIVE_INSTANCE(_this, NI_TextRenderBase);
			iTJSDispatch2 *arr = TJSCreateArrayObject();
			for (size_t i = 0; i < _this->eng.keyWaits.size(); ++i) {
				iTJSDispatch2 *kd = TJSCreateDictionaryObject();
				if (kd) {
					DictSetInt(kd, TJS_W("pos"), _this->eng.keyWaits[i].pos);
					DictSetReal(kd, TJS_W("time"), _this->eng.keyWaits[i].time);
					ArrayPushObj(arr, (tjs_int)i, kd);
					kd->Release();
				}
			}
			if (result) {
				result->SetObject(arr, arr);
			}
			arr->Release();
			return TJS_S_OK;
		}
		TJS_END_NATIVE_METHOD_DECL(getKeyWait)

		TJS_BEGIN_NATIVE_METHOD_DECL(calcLineOffset)
		{
			TJS_GET_NATIVE_INSTANCE(_this, NI_TextRenderBase);
			int line = (numparams >= 1) ? (int)(tjs_int)*param[0] : 0;
			if (result) *result = (tjs_int)_this->eng.calcLineOffset(line);
			return TJS_S_OK;
		}
		TJS_END_NATIVE_METHOD_DECL(calcLineOffset)

		TJS_BEGIN_NATIVE_METHOD_DECL(calcShowCount)
		{
			TJS_GET_NATIVE_INSTANCE(_this, NI_TextRenderBase);
			double elapsed = (numparams >= 1) ? (double)param[0]->AsReal() : 0.0;
			if (result) *result = (tjs_int)_this->eng.calcShowCount(elapsed);
			return TJS_S_OK;
		}
		TJS_END_NATIVE_METHOD_DECL(calcShowCount)

		TJS_BEGIN_NATIVE_METHOD_DECL(getLinkCount)
		{
			TJS_GET_NATIVE_INSTANCE(_this, NI_TextRenderBase);
			if (result) *result = (tjs_int)_this->eng.links.size();
			return TJS_S_OK;
		}
		TJS_END_NATIVE_METHOD_DECL(getLinkCount)

		TJS_BEGIN_NATIVE_METHOD_DECL(getLinkNames)
		{
			TJS_GET_NATIVE_INSTANCE(_this, NI_TextRenderBase);
			iTJSDispatch2 *arr = TJSCreateArrayObject();
			for (size_t i = 0; i < _this->eng.links.size(); ++i) {
				tTJSVariant v(U16ToTtstr(_this->eng.links[i].name));
				arr->PropSetByNum(TJS_MEMBERENSURE, (tjs_int)i, &v, arr);
			}
			if (result) result->SetObject(arr, arr);
			arr->Release();
			return TJS_S_OK;
		}
		TJS_END_NATIVE_METHOD_DECL(getLinkNames)

		TJS_BEGIN_NATIVE_METHOD_DECL(findLink)
		{
			TJS_GET_NATIVE_INSTANCE(_this, NI_TextRenderBase);
			if (numparams < 1) return TJS_E_BADPARAMCOUNT;
			int idx = _this->eng.findLink(TtstrToU16(ttstr(*param[0])));
			if (result) *result = (tjs_int)idx;
			return TJS_S_OK;
		}
		TJS_END_NATIVE_METHOD_DECL(findLink)

		TJS_BEGIN_NATIVE_METHOD_DECL(getLinkRects)
		{
			TJS_GET_NATIVE_INSTANCE(_this, NI_TextRenderBase);
			int idx = (numparams >= 1) ? _this->ResolveLinkArg(*param[0]) : -1;
			iTJSDispatch2 *arr = TJSCreateArrayObject();
			if (idx >= 0 && (size_t)idx < _this->eng.links.size()) {
				const auto &rects = _this->eng.links[(size_t)idx].rects;
				for (size_t i = 0; i < rects.size(); ++i) {
					iTJSDispatch2 *rd = TJSCreateDictionaryObject();
					if (rd) {
						DictSetInt(rd, TJS_W("left"), rects[i].left);
						DictSetInt(rd, TJS_W("top"), rects[i].top);
						DictSetInt(rd, TJS_W("width"), rects[i].width);
						DictSetInt(rd, TJS_W("height"), rects[i].height);
						ArrayPushObj(arr, (tjs_int)i, rd);
						rd->Release();
					}
				}
			}
			if (result) result->SetObject(arr, arr);
			arr->Release();
			return TJS_S_OK;
		}
		TJS_END_NATIVE_METHOD_DECL(getLinkRects)

		TJS_BEGIN_NATIVE_METHOD_DECL(getLinkCharacters)
		{
			TJS_GET_NATIVE_INSTANCE(_this, NI_TextRenderBase);
			int idx = (numparams >= 1) ? _this->ResolveLinkArg(*param[0]) : -1;
			iTJSDispatch2 *arr = TJSCreateArrayObject();
			if (idx >= 0 && (size_t)idx < _this->eng.links.size()) {
				const std::u16string &targetName = _this->eng.links[(size_t)idx].name;
				tjs_int outIdx = 0;
				for (const auto &rc : _this->eng.chars) {
					if (rc.link == targetName) {
						iTJSDispatch2 *cd = _this->BuildCharDict(rc);
						if (cd) {
							ArrayPushObj(arr, outIdx++, cd);
							cd->Release();
						}
					}
				}
			}
			if (result) result->SetObject(arr, arr);
			arr->Release();
			return TJS_S_OK;
		}
		TJS_END_NATIVE_METHOD_DECL(getLinkCharacters)

		TJS_BEGIN_NATIVE_METHOD_DECL(isLinkContains)
		{
			TJS_GET_NATIVE_INSTANCE(_this, NI_TextRenderBase);
			if (numparams < 3) return TJS_E_BADPARAMCOUNT;
			int idx = _this->ResolveLinkArg(*param[0]);
			int x = (int)(tjs_int)*param[1];
			int y = (int)(tjs_int)*param[2];
			bool hit = _this->eng.isLinkContains(idx, x, y);
			if (result) *result = (tjs_int)(hit ? 1 : 0);
			return TJS_S_OK;
		}
		TJS_END_NATIVE_METHOD_DECL(isLinkContains)

		TJS_BEGIN_NATIVE_METHOD_DECL(getLinkOfPosition)
		{
			TJS_GET_NATIVE_INSTANCE(_this, NI_TextRenderBase);
			if (numparams < 2) return TJS_E_BADPARAMCOUNT;
			int x = (int)(tjs_int)*param[0];
			int y = (int)(tjs_int)*param[1];
			std::u16string hit = _this->eng.getLinkOfPosition(x, y);
			if (result) *result = U16ToTtstr(hit);
			return TJS_S_OK;
		}
		TJS_END_NATIVE_METHOD_DECL(getLinkOfPosition)

		// Read-only layout result properties (including both render* and short aliases)
		DECL_RO_INT_PROP(renderCount, _this->eng.chars.size())
		DECL_INT_PROP(showCount, _this->eng.showCount, _this->eng.showCount = (int)(tjs_int)*param)
		DECL_RO_INT_PROP(renderOver, _this->eng.over)
		DECL_RO_INT_PROP(over, _this->eng.over)
		DECL_RO_INT_PROP(renderLines, _this->eng.lineCount)
		DECL_RO_INT_PROP(lineCount, _this->eng.lineCount)
		DECL_RO_REAL_PROP(renderDelay, _this->eng.totalDelay)
		DECL_RO_REAL_PROP(delay, _this->eng.totalDelay)
		DECL_INT_PROP(indent, _this->eng.indent, _this->eng.indent = (int)(tjs_int)*param)
		DECL_RO_INT_PROP(renderLeft, _this->eng.renderLeft)
		DECL_RO_INT_PROP(renderTop, _this->eng.renderTop)
		DECL_RO_INT_PROP(renderRight, _this->eng.renderRight)
		DECL_RO_INT_PROP(renderBottom, _this->eng.renderBottom)
		DECL_RO_INT_PROP(maxScrollOffset, _this->eng.maxScrollOffset())
		DECL_RO_INT_PROP(maxScrollLine, _this->eng.maxScrollLine())

		TJS_BEGIN_NATIVE_PROP_DECL(renderText)
		{
			TJS_BEGIN_NATIVE_PROP_GETTER
			{
				TJS_GET_NATIVE_INSTANCE(_this, NI_TextRenderBase);
				*result = U16ToTtstr(_this->eng.renderText);
				return TJS_S_OK;
			}
			TJS_END_NATIVE_PROP_GETTER
			TJS_DENY_NATIVE_PROP_SETTER
		}
		TJS_END_NATIVE_PROP_DECL(renderText)

		// Scale properties
		DECL_REAL_PROP(factor, _this->eng.factor, _this->eng.factor = (double)param->AsReal())
		DECL_REAL_PROP(timeScale, _this->eng.factor, _this->eng.factor = (double)param->AsReal())
		DECL_REAL_PROP(fontScale, _this->eng.fontScale, _this->eng.fontScale = (double)param->AsReal())
		DECL_REAL_PROP(lineScale, _this->eng.lineScale, _this->eng.lineScale = (double)param->AsReal())

		// Default properties
		TJS_BEGIN_NATIVE_PROP_DECL(defaultFace)
		{
			TJS_BEGIN_NATIVE_PROP_GETTER
			{
				TJS_GET_NATIVE_INSTANCE(_this, NI_TextRenderBase);
				*result = U16ToTtstr(_this->eng.defaultFace);
				return TJS_S_OK;
			}
			TJS_END_NATIVE_PROP_GETTER
			TJS_BEGIN_NATIVE_PROP_SETTER
			{
				TJS_GET_NATIVE_INSTANCE(_this, NI_TextRenderBase);
				_this->eng.defaultFace = TtstrToU16(ttstr(*param));
				return TJS_S_OK;
			}
			TJS_END_NATIVE_PROP_SETTER
		}
		TJS_END_NATIVE_PROP_DECL(defaultFace)

		DECL_INT_PROP(defaultBold, _this->eng.defaultBold, _this->eng.defaultBold = ((tjs_int)*param != 0) ? 1 : 0)
		DECL_INT_PROP(defaultItalic, _this->eng.defaultItalic, _this->eng.defaultItalic = ((tjs_int)*param != 0) ? 1 : 0)
		DECL_INT_PROP(defaultFontSize, _this->eng.defaultFontSize, _this->eng.defaultFontSize = std::max(1, (int)(tjs_int)*param))
		DECL_INT_PROP(defaultBigFontSize, _this->eng.bigFontSize, _this->eng.bigFontSize = std::max(1, (int)(tjs_int)*param))
		DECL_INT_PROP(defaultSmallFontSize, _this->eng.smallFontSize, _this->eng.smallFontSize = std::max(1, (int)(tjs_int)*param))
		DECL_INT_PROP(defaultRubySize, _this->eng.defaultRubySize, _this->eng.defaultRubySize = std::max(1, (int)(tjs_int)*param))
		DECL_INT_PROP(defaultRubyOffset, _this->eng.defaultRubyOffset, _this->eng.defaultRubyOffset = (int)(tjs_int)*param)
		DECL_INT_PROP(defaultChColor, _this->eng.defaultChColor, _this->eng.defaultChColor = (uint32_t)(tjs_int64)*param | 0xff000000u)
		DECL_INT_PROP(defaultShadow, _this->eng.defaultShadow, _this->eng.defaultShadow = ((tjs_int)*param != 0) ? 1 : 0)
		DECL_INT_PROP(defaultShadowColor, _this->eng.defaultShadowColor, _this->eng.defaultShadowColor = (uint32_t)(tjs_int64)*param | 0xff000000u)
		DECL_INT_PROP(defaultShadowDiff, _this->eng.defaultShadowDiff, _this->eng.defaultShadowDiff = (int)(tjs_int)*param)
		DECL_INT_PROP(defaultEdge, _this->eng.defaultEdge, _this->eng.defaultEdge = ((tjs_int)*param != 0) ? 1 : 0)
		DECL_INT_PROP(defaultEdgeColor, _this->eng.defaultEdgeColor, _this->eng.defaultEdgeColor = (uint32_t)(tjs_int64)*param | 0xff000000u)
		DECL_INT_PROP(defaultLineSpacing, _this->eng.defaultLineSpacing, _this->eng.defaultLineSpacing = (int)(tjs_int)*param)
		DECL_INT_PROP(defaultPitch, _this->eng.defaultPitch, _this->eng.defaultPitch = (int)(tjs_int)*param)
		DECL_INT_PROP(defaultLineSize, _this->eng.defaultLineSize, _this->eng.defaultLineSize = (int)(tjs_int)*param)
		DECL_INT_PROP(defaultAlign, _this->eng.defaultAlign, _this->eng.defaultAlign = (int)(tjs_int)*param)
		DECL_INT_PROP(defaultValign, _this->eng.defaultValign, _this->eng.defaultValign = (int)(tjs_int)*param)

		// Active font & style properties
		TJS_BEGIN_NATIVE_PROP_DECL(face)
		{
			TJS_BEGIN_NATIVE_PROP_GETTER
			{
				TJS_GET_NATIVE_INSTANCE(_this, NI_TextRenderBase);
				*result = U16ToTtstr(_this->eng.face);
				return TJS_S_OK;
			}
			TJS_END_NATIVE_PROP_GETTER
			TJS_BEGIN_NATIVE_PROP_SETTER
			{
				TJS_GET_NATIVE_INSTANCE(_this, NI_TextRenderBase);
				_this->eng.face = TtstrToU16(ttstr(*param));
				_this->eng.notifyFontChange();
				return TJS_S_OK;
			}
			TJS_END_NATIVE_PROP_SETTER
		}
		TJS_END_NATIVE_PROP_DECL(face)

		DECL_INT_PROP(vertical, _this->eng.vertical, _this->eng.vertical = ((tjs_int)*param != 0) ? 1 : 0)
		DECL_INT_PROP(bold, _this->eng.bold, { _this->eng.bold = ((tjs_int)*param != 0) ? 1 : 0; _this->eng.notifyFontChange(); })
		DECL_INT_PROP(italic, _this->eng.italic, { _this->eng.italic = ((tjs_int)*param != 0) ? 1 : 0; _this->eng.notifyFontChange(); })
		DECL_INT_PROP(fontSize, _this->eng.fontSize, _this->eng.fontSize = std::max(1, (int)(tjs_int)*param))
		DECL_INT_PROP(rubySize, _this->eng.rubySize, _this->eng.rubySize = std::max(1, (int)(tjs_int)*param))
		DECL_INT_PROP(rubyOffset, _this->eng.rubyOffset, _this->eng.rubyOffset = (int)(tjs_int)*param)
		DECL_INT_PROP(chColor, _this->eng.chColor, _this->eng.chColor = (uint32_t)(tjs_int64)*param | 0xff000000u)
		DECL_INT_PROP(shadow, _this->eng.shadow, _this->eng.shadow = ((tjs_int)*param != 0) ? 1 : 0)
		DECL_INT_PROP(shadowColor, _this->eng.shadowColor, _this->eng.shadowColor = (uint32_t)(tjs_int64)*param | 0xff000000u)
		DECL_INT_PROP(shadowDiff, _this->eng.shadowDiff, _this->eng.shadowDiff = (int)(tjs_int)*param)
		DECL_INT_PROP(edge, _this->eng.edge, _this->eng.edge = ((tjs_int)*param != 0) ? 1 : 0)
		DECL_INT_PROP(edgeColor, _this->eng.edgeColor, _this->eng.edgeColor = (uint32_t)(tjs_int64)*param | 0xff000000u)
		DECL_INT_PROP(lineSpacing, _this->eng.lineSpacing, _this->eng.lineSpacing = (int)(tjs_int)*param)
		DECL_INT_PROP(pitch, _this->eng.pitch, _this->eng.pitch = (int)(tjs_int)*param)
		DECL_INT_PROP(lineSize, _this->eng.lineSize, _this->eng.lineSize = (int)(tjs_int)*param)
		DECL_INT_PROP(align, _this->eng.align, _this->eng.align = (int)(tjs_int)*param)
		DECL_INT_PROP(valign, _this->eng.valign, _this->eng.valign = (int)(tjs_int)*param)

	TJS_END_NATIVE_MEMBERS

	return classobj;
}

static void InitPlugin_TextRender() {
	iTJSDispatch2 *global = TVPGetScriptDispatch();
	if (global) {
		iTJSDispatch2 *cls = Create_NC_TextRenderBase();
		tTJSVariant var(cls);
		cls->Release();
		global->PropSet(TJS_MEMBERENSURE, TJS_W("TextRenderBase"), NULL, &var, global);
		global->Release();
	}
}

} // namespace

NCB_PRE_REGIST_CALLBACK(InitPlugin_TextRender);
