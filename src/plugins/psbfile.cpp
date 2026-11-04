// psbfile.cpp â€” PSB (pimg) container plugin for Kirikiroid2
//
// Provides the "PSBFile" TJS class and the "psb://" storage media so games
// whose UI is packed in PSB image containers (.pimg) can render on
// Kirikiroid2 (the original game ships psbfile.dll, a Windows-only plugin).
//
// TJS surface expected by game scripts (see psdlayer.tjs loadPIMG):
//   var psb = new PSBFile();
//   if (!psb.load("message.pimg")) throw ...;
//   psb.root.layers   -> Array of layer dictionaries
//   psb.root.width    -> int
//   psb.root.height   -> int
//   layer members: name(string), layer_id, layer_type, left, top, width,
//                  height, opacity, visible, type, [group_layer_id],
//                  [diff_id], [same_image]
//   layer images are then loaded from "psb://<name>/<layer_id>.tlg"
//   which this plugin serves from the parsed container.
//
// Binary format ported from tools/vendor/hxv4/tools/psb_parser.py
// (reverse engineered from psbfile.dll): magic "PSB\0", 40-byte header,
// psb packed arrays, compact name b-tree, string pool, chunk table and a
// tagged value tree at offset_entries.

#include "PluginStub.h"
#include "ncbind/ncbind.hpp"

#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <zlib.h>

#ifdef TJS_NATIVE_CLASSID_NAME
#undef TJS_NATIVE_CLASSID_NAME
#undef TJS_NCM_REG_THIS
#undef TJS_NATIVE_SET_ClassID
#endif
#define TJS_NCM_REG_THIS classobj
#define TJS_NATIVE_SET_ClassID TJS_NATIVE_CLASSID_NAME = TJS_NCM_CLASSID;
#define TJS_NATIVE_CLASSID_NAME ClassID_PSBFile

#define NCB_MODULE_NAME TJS_W("psbfile.dll")

//---------------------------------------------------------------------------
// PSB binary parsing
//---------------------------------------------------------------------------

namespace {

struct PsbArray {
	tjs_uint end = 0; // offset right after this array
	std::vector<tjs_uint> values;
};

class PsbReader {
public:
	std::vector<tjs_uint8> buf;
	// header fields
	tjs_uint off_names = 0, off_strings = 0, off_strings_data = 0;
	tjs_uint off_chunk_offsets = 0, off_chunk_lengths = 0;
	tjs_uint off_chunk_data = 0, off_entries = 0;

	// parsed tables
	PsbArray nameA1, nameA2, nameA3;
	PsbArray stringOffsets;
	PsbArray chunkOffsets, chunkLengths;
	bool tablesParsed = false;

	// root top-level members: key -> value offset
	std::vector<std::pair<ttstr, tjs_uint>> rootMembers;
	// "NNNN.tlg" -> chunk index
	std::map<ttstr, tjs_uint> tlgMap;
	bool rootParsed = false;

	static tjs_uint readUint(const tjs_uint8 *p, tjs_uint width) {
		tjs_uint v = 0;
		for (tjs_uint i = 0; i < width; i++) v |= (tjs_uint)p[i] << (8 * i);
		return v;
	}

	bool readArray(tjs_uint offset, PsbArray &out) {
		if (offset >= buf.size()) return false;
		tjs_uint cursor = offset;
		tjs_uint countType = buf[cursor];
		cursor += 1;
		if (countType < 0x0D || countType > 0x14) return false;
		tjs_uint countWidth = countType - 0x0C;
		if (countWidth < 1 || countWidth > 8) return false;
		if (cursor + countWidth > buf.size()) return false;
		tjs_uint count = readUint(&buf[cursor], countWidth);
		cursor += countWidth;
		if (cursor >= buf.size()) return false;
		tjs_uint entryType = buf[cursor];
		cursor += 1;
		if (entryType < 0x0D || entryType > 0x14) return false;
		tjs_uint entryWidth = entryType - 0x0C;
		if (entryWidth < 1 || entryWidth > 8) return false;
		if (count > 0x2000000) return false;
		if (cursor + count * entryWidth > buf.size()) return false;
		out.values.clear();
		out.values.reserve(count);
		for (tjs_uint i = 0; i < count; i++) {
			out.values.push_back(readUint(&buf[cursor], entryWidth));
			cursor += entryWidth;
		}
		out.end = cursor;
		return true;
	}

	bool parseTables() {
		if (tablesParsed) return true;
		// name b-tree: three consecutive arrays at off_names
		if (!readArray(off_names, nameA1)) return false;
		if (!readArray(nameA1.end, nameA2)) return false;
		if (!readArray(nameA2.end, nameA3)) return false;
		// string pool offsets
		if (!readArray(off_strings, stringOffsets)) return false;
		// chunk tables
		if (!readArray(off_chunk_offsets, chunkOffsets)) return false;
		if (!readArray(off_chunk_lengths, chunkLengths)) return false;
		if (chunkOffsets.values.size() != chunkLengths.values.size()) return false;
		tablesParsed = true;
		return true;
	}

	static ttstr Utf8ToTtstr(const tjs_uint8 *p, size_t len) {
		ttstr out;
		size_t i = 0;
		while (i < len) {
			tjs_uint32 b0 = p[i++];
			tjs_uint32 cp = 0;
			if (b0 < 0x80) {
				cp = b0;
			} else if ((b0 & 0xE0) == 0xC0 && i < len) {
				cp = ((b0 & 0x1F) << 6) | (p[i++] & 0x3F);
			} else if ((b0 & 0xF0) == 0xE0 && i + 1 < len) {
				cp = ((b0 & 0x0F) << 12) | ((tjs_uint32)(p[i] & 0x3F) << 6) | (p[i + 1] & 0x3F);
				i += 2;
			} else if ((b0 & 0xF8) == 0xF0 && i + 2 < len) {
				cp = ((b0 & 0x07) << 18) | ((tjs_uint32)(p[i] & 0x3F) << 12) |
				     ((tjs_uint32)(p[i + 1] & 0x3F) << 6) | (p[i + 2] & 0x3F);
				i += 3;
			} else {
				cp = 0xFFFD;
			}
			if (cp <= 0xFFFF) {
				out += (tjs_char)cp;
			} else if (cp <= 0x10FFFF) {
				cp -= 0x10000;
				out += (tjs_char)(0xD800 + (cp >> 10));
				out += (tjs_char)(0xDC00 + (cp & 0x3FF));
			}
		}
		return out;
	}

	// property name from the compact b-tree (see psb_parser.get_name)
	bool getName(tjs_uint index, ttstr &out) {
		if (index >= nameA3.values.size()) return false;
		tjs_uint a = nameA3.values[index];
		if (a >= nameA2.values.size()) return false;
		tjs_int64 b = nameA2.values[a];
		std::vector<tjs_uint8> accum;
		int guard = 0;
		while (true) {
			if (++guard > 4096) return false;
			if (b < 0 || (tjs_uint64)b >= nameA2.values.size()) return false;
			tjs_int64 c = nameA2.values[(tjs_uint)b];
			if (c < 0 || (tjs_uint64)c >= nameA1.values.size()) return false;
			tjs_int64 d = nameA1.values[(tjs_uint)c];
			tjs_int64 ch = b - d;
			if (ch < 0 || ch > 0x10FFFF) return false;
			accum.push_back((tjs_uint8)ch);
			b = c;
			if (b == 0) break;
		}
		std::reverse(accum.begin(), accum.end());
		out = accum.empty() ? ttstr() : Utf8ToTtstr(accum.data(), accum.size());
		return true;
	}

	bool getString(tjs_uint index, ttstr &out) {
		if (index >= stringOffsets.values.size()) return false;
		tjs_uint start = off_strings_data + stringOffsets.values[index];
		if (start >= buf.size()) return false;
		tjs_uint end = start;
		while (end < buf.size() && buf[end] != 0) end++;
		if (end >= buf.size()) return false;
		out = Utf8ToTtstr(&buf[start], end - start);
		return true;
	}

	bool getChunk(tjs_uint index, tjs_uint &offset, tjs_uint &length) {
		if (index >= chunkOffsets.values.size()) return false;
		tjs_uint rel = chunkOffsets.values[index];
		tjs_uint len = chunkLengths.values[index];
		tjs_uint fileOff = off_chunk_data + rel;
		if (fileOff > buf.size() || len > buf.size() - fileOff) return false;
		offset = fileOff;
		length = len;
		return true;
	}

	// walk the root object's top level: member key -> value offset
	bool parseRootMembers() {
		if (rootParsed) return true;
		if (!parseTables()) return false;
		if (off_entries >= buf.size()) return false;
		if (buf[off_entries] != 0x21) return false; // root must be an object
		tjs_uint cursor = off_entries + 1;
		PsbArray names, offsets;
		if (!readArray(cursor, names)) return false;
		if (!readArray(names.end, offsets)) return false;
		if (names.values.size() != offsets.values.size()) return false;
		tjs_uint base = offsets.end;
		for (size_t i = 0; i < names.values.size(); i++) {
			ttstr key;
			if (!getName(names.values[i], key)) return false;
			tjs_uint voff = base + offsets.values[i];
			if (voff >= buf.size()) return false;
			rootMembers.push_back(std::make_pair(key, voff));
		}
		rootParsed = true;
		return true;
	}

	// resolve resource reference at offset -> chunk index
	bool parseResourceRef(tjs_uint offset, tjs_uint &chunkIndex) {
		if (offset >= buf.size()) return false;
		tjs_uint tag = buf[offset];
		if (tag < 0x19 || tag > 0x1C) return false;
		tjs_uint width = tag - 0x18;
		if (offset + 1 + width > buf.size()) return false;
		chunkIndex = readUint(&buf[offset + 1], width);
		return true;
	}

	// map "<layer_id>.tlg" -> chunk location
	void buildTlgMap() {
		if (!tlgMap.empty()) return;
		for (size_t i = 0; i < rootMembers.size(); i++) {
			const ttstr &key = rootMembers[i].first;
			tjs_uint voff = rootMembers[i].second;
			tjs_uint chunkIndex;
			if (!parseResourceRef(voff, chunkIndex)) continue;
			if (key.length() < 5) continue;
			if (key.SubString(key.length() - 4, 4) != TJS_W(".tlg")) continue;
			tlgMap[key] = chunkIndex;
		}
	}

	bool findTlg(const ttstr &tlgName, tjs_uint &offset, tjs_uint &length) {
		if (!parseRootMembers()) return false;
		buildTlgMap();
		std::map<ttstr, tjs_uint>::iterator it = tlgMap.find(tlgName);
		if (it == tlgMap.end()) return false;
		return getChunk(it->second, offset, length);
	}

	bool Load(const ttstr &storageName) {
		tTJSBinaryStream *stream = NULL;
		try {
			stream = TVPCreateStream(storageName, TJS_BS_READ);
		} catch (...) {
			return false;
		}
		if (!stream) return false;
		tjs_uint64 size64 = stream->GetSize();
		if (size64 < 12 || size64 > 0x20000000ull) { // 512MB sanity cap
			delete stream;
			return false;
		}
		tjs_uint size = (tjs_uint)size64;
		buf.resize(size);
		bool readOk = stream->Read(&buf[0], size) == size;
		delete stream;
		if (!readOk) return false;

		if (size >= 12 && buf[0] == 'm' && buf[1] == 'd' && buf[2] == 'f' && buf[3] == 0) {
			tjs_uint uncompSize = readUint(&buf[4], 4);
			if (uncompSize < 40 || uncompSize > 0x20000000u) return false;
			std::vector<tjs_uint8> out(uncompSize);
			uLongf destLen = (uLongf)uncompSize;
			if (uncompress(&out[0], &destLen, &buf[8], (uLong)(size - 8)) != Z_OK)
				return false;
			out.resize((size_t)destLen);
			buf.swap(out);
			size = (tjs_uint)buf.size();
		}

		if (size < 40 || buf[0] != 'P' || buf[1] != 'S' || buf[2] != 'B' || buf[3] != 0)
			return false;
		// header: <4sHH8I> -> the eight u32 offsets start at byte 8
		if (size < 40) return false;
		off_names = readUint(&buf[8 + 4], 4);
		off_strings = readUint(&buf[8 + 8], 4);
		off_strings_data = readUint(&buf[8 + 12], 4);
		off_chunk_offsets = readUint(&buf[8 + 16], 4);
		off_chunk_lengths = readUint(&buf[8 + 20], 4);
		off_chunk_data = readUint(&buf[8 + 24], 4);
		off_entries = readUint(&buf[8 + 28], 4);

		if (!parseTables()) return false;
		if (!parseRootMembers()) return false;
		return true;
	}
};

//---------------------------------------------------------------------------
// loaded PSB registry + psb:// storage media
//---------------------------------------------------------------------------

static std::map<ttstr, std::shared_ptr<PsbReader>> g_psbRegistry;

static ttstr ToLowerName(const ttstr &src) {
	tjs_uint len = src.length();
	const tjs_char *p = src.c_str();
	ttstr lower;
	for (tjs_uint i = 0; i < len; i++) {
		tjs_char c = p[i];
		if (c >= TJS_W('A') && c <= TJS_W('Z'))
			c = (tjs_char)(c - TJS_W('A') + TJS_W('a'));
		lower += c;
	}
	return lower;
}

class PsbChunkStream : public tTJSBinaryStream {
	std::shared_ptr<PsbReader> data;
	tjs_uint offset, length, pos;

public:
	PsbChunkStream(const std::shared_ptr<PsbReader> &d, tjs_uint off, tjs_uint len)
		: data(d), offset(off), length(len), pos(0) {}

	tjs_uint64 TJS_INTF_METHOD Seek(tjs_int64 _offset, tjs_int whence) {
		tjs_int64 npos;
		switch (whence) {
			case TJS_BS_SEEK_SET: npos = _offset; break;
			case TJS_BS_SEEK_CUR: npos = (tjs_int64)pos + _offset; break;
			case TJS_BS_SEEK_END: npos = (tjs_int64)length + _offset; break;
			default: npos = pos; break;
		}
		if (npos < 0) npos = 0;
		if (npos > (tjs_int64)length) npos = length;
		pos = (tjs_uint)npos;
		return pos;
	}

	tjs_uint TJS_INTF_METHOD Read(void *buffer, tjs_uint readSize) {
		if (pos >= length) return 0;
		if (readSize > length - pos) readSize = length - pos;
		memcpy(buffer, &data->buf[offset + pos], readSize);
		pos += readSize;
		return readSize;
	}

	tjs_uint TJS_INTF_METHOD Write(const void *, tjs_uint) { return 0; }
	void TJS_INTF_METHOD SetEndOfStorage() { pos = length; }
	tjs_uint64 TJS_INTF_METHOD GetSize() { return length; }
};

class PsbStorageMedia : public iTVPStorageMedia {
	tjs_uint refCount;

public:
	PsbStorageMedia() : refCount(1) {}

	void TJS_INTF_METHOD AddRef() { refCount++; }
	void TJS_INTF_METHOD Release() {
		if (--refCount == 0) delete this;
	}

	void TJS_INTF_METHOD GetName(ttstr &name) { name = TJS_W("psb"); }
	void TJS_INTF_METHOD NormalizeDomainName(ttstr &) {}
	void TJS_INTF_METHOD NormalizePathName(ttstr &) {}

	// name here is "<psbfile>.<ext>/<tlgname>", e.g. "title.pimg/5863.tlg"
	static bool Resolve(const ttstr &name, std::shared_ptr<PsbReader> *outData,
		tjs_uint *outOffset, tjs_uint *outLength) {
		const tjs_char *p = name.c_str();
		const tjs_char *slash = TJS_strchr(p, TJS_W('/'));
		if (!slash || slash == p) return false;
		ttstr psbName(p, (tjs_int)(slash - p));
		ttstr tlgName(slash + 1);
		std::map<ttstr, std::shared_ptr<PsbReader>>::iterator it =
			g_psbRegistry.find(ToLowerName(psbName));
		if (it == g_psbRegistry.end()) return false;
		tjs_uint off, len;
		if (!it->second->findTlg(tlgName, off, len)) return false;
		if (outData) *outData = it->second;
		if (outOffset) *outOffset = off;
		if (outLength) *outLength = len;
		return true;
	}

	bool TJS_INTF_METHOD CheckExistentStorage(const ttstr &name) {
		return Resolve(name, NULL, NULL, NULL);
	}

	tTJSBinaryStream *TJS_INTF_METHOD Open(const ttstr &name, tjs_uint32) {
		std::shared_ptr<PsbReader> data;
		tjs_uint off, len;
		if (!Resolve(name, &data, &off, &len))
			TVPThrowExceptionMessage(TJS_W("cannot open psb storage:%1"), name);
		return new PsbChunkStream(data, off, len);
	}

	void TJS_INTF_METHOD GetListAt(const ttstr &, iTVPStorageLister *) {}
	void TJS_INTF_METHOD GetLocallyAccessibleName(ttstr &name) { name = ttstr(); }
};

static PsbStorageMedia *g_psbMedia = NULL;

} // namespace

//---------------------------------------------------------------------------
// PSBFile TJS native class
//---------------------------------------------------------------------------

#ifdef TJS_NATIVE_CLASSID_NAME
#undef TJS_NATIVE_CLASSID_NAME
#endif
#define TJS_NATIVE_CLASSID_NAME ClassID_PSBFile
static tjs_int32 TJS_NATIVE_CLASSID_NAME = -1;

class NI_PSBFile : public tTJSNativeInstance {
	std::shared_ptr<PsbReader> data;
	iTJSDispatch2 *rootObject;

	static const int kMaxDepth = 64;

public:
	NI_PSBFile() : rootObject(NULL) {}

	~NI_PSBFile() { Clear(); }

	void Clear() {
		data.reset();
		if (rootObject) {
			rootObject->Release();
			rootObject = NULL;
		}
	}

	void TJS_INTF_METHOD Invalidate() { Clear(); }

	tjs_error TJS_INTF_METHOD Construct(tjs_int, tTJSVariant **, iTJSDispatch2 *) {
		return TJS_S_OK;
	}

	bool Load(const ttstr &storageName) {
		Clear();
		data = std::make_shared<PsbReader>();
		if (!data->Load(storageName)) {
			data.reset();
			return false;
		}
		g_psbRegistry[ToLowerName(TVPExtractStorageName(storageName))] = data;
		return true;
	}

	iTJSDispatch2 *GetRoot() {
		if (!data) return NULL;
		if (!rootObject) {
			tTJSVariant v;
			if (!BuildValue(data->off_entries, 0, v)) return NULL;
			if (v.Type() != tvtObject) return NULL;
			rootObject = v.AsObject(); // holds one reference
		}
		if (rootObject) rootObject->AddRef();
		return rootObject;
	}

private:
	// convert a value at offset into a TJS variant; false on parse error
	bool BuildValue(tjs_uint offset, int depth, tTJSVariant &result) {
		if (depth > kMaxDepth) return false;
		if (offset >= data->buf.size()) return false;
		tjs_uint tag = data->buf[offset];

		if (tag == 0x01) { result.Clear(); return true; }      // null
		if (tag == 0x02) { result = (tjs_int)0; return true; } // false
		if (tag == 0x03) { result = (tjs_int)1; return true; } // true

		if (tag >= 0x04 && tag <= 0x0C) {
			tjs_uint size = tag - 0x04;
			if (offset + 1 + size > data->buf.size()) return false;
			tjs_uint64 v = 0;
			for (tjs_uint i = 0; i < size; i++)
				v |= (tjs_uint64)data->buf[offset + 1 + i] << (8 * i);
			if (size > 0 && size < 8) {
				tjs_uint64 sign = 1ull << (size * 8 - 1);
				if (v & sign) v -= (1ull << (size * 8));
			}
			result = (tjs_int64)v;
			return true;
		}

		if (tag >= 0x0D && tag <= 0x14) {
			// packed uint array
			PsbArray arr;
			if (!data->readArray(offset, arr)) return false;
			iTJSDispatch2 *arrObj = TJSCreateArrayObject();
			for (size_t i = 0; i < arr.values.size(); i++) {
				tTJSVariant v;
				v = (tjs_int64)arr.values[i];
				arrObj->PropSetByNum(TJS_MEMBERENSURE, (tjs_uint)i, &v, arrObj);
			}
			result.Clear();
			result.SetObject(arrObj, arrObj);
			arrObj->Release();
			return true;
		}

		if (tag >= 0x15 && tag <= 0x18) {
			// string reference
			tjs_uint width = tag - 0x14;
			if (offset + 1 + width > data->buf.size()) return false;
			tjs_uint idx = PsbReader::readUint(&data->buf[offset + 1], width);
			ttstr s;
			if (!data->getString(idx, s)) return false;
			result = s;
			return true;
		}

		if (tag >= 0x19 && tag <= 0x1C) {
			// resource reference -> expose chunk index as int
			tjs_uint width = tag - 0x18;
			if (offset + 1 + width > data->buf.size()) return false;
			tjs_uint idx = PsbReader::readUint(&data->buf[offset + 1], width);
			result = (tjs_int64)idx;
			return true;
		}

		if (tag == 0x1D) { result = (double)0.0; return true; }
		if (tag == 0x1E) {
			if (offset + 5 > data->buf.size()) return false;
			float f;
			memcpy(&f, &data->buf[offset + 1], 4);
			result = (double)f;
			return true;
		}
		if (tag == 0x1F) {
			if (offset + 9 > data->buf.size()) return false;
			double d;
			memcpy(&d, &data->buf[offset + 1], 8);
			result = d;
			return true;
		}

		if (tag == 0x20 || tag == 0x21) {
			iTJSDispatch2 *obj = BuildTree(offset, depth);
			if (!obj) return false;
			result.Clear();
		result.SetObject(obj, obj);
			obj->Release();
			return true;
		}

		return false;
	}

	// build an Array (0x20) or Dictionary (0x21) node
	iTJSDispatch2 *BuildTree(tjs_uint offset, int depth) {
		if (depth > kMaxDepth) return NULL;
		if (offset >= data->buf.size()) return NULL;
		tjs_uint tag = data->buf[offset];

		if (tag == 0x20) {
			PsbArray offsets;
			if (!data->readArray(offset + 1, offsets)) return NULL;
			iTJSDispatch2 *arr = TJSCreateArrayObject();
			tjs_uint base = offsets.end;
			for (size_t i = 0; i < offsets.values.size(); i++) {
				tTJSVariant v;
				if (!BuildValue(base + offsets.values[i], depth + 1, v)) {
					arr->Release();
					return NULL;
				}
				arr->PropSetByNum(TJS_MEMBERENSURE, (tjs_uint)i, &v, arr);
			}
			return arr;
		}

		if (tag == 0x21) {
			PsbArray names, offsets;
			if (!data->readArray(offset + 1, names)) return NULL;
			if (!data->readArray(names.end, offsets)) return NULL;
			if (names.values.size() != offsets.values.size()) return NULL;
			iTJSDispatch2 *dic = TJSCreateDictionaryObject();
			tjs_uint base = offsets.end;
			for (size_t i = 0; i < names.values.size(); i++) {
				ttstr key;
				if (!data->getName(names.values[i], key)) {
					dic->Release();
					return NULL;
				}
				tTJSVariant v;
				if (!BuildValue(base + offsets.values[i], depth + 1, v)) {
					dic->Release();
					return NULL;
				}
				dic->PropSet(TJS_MEMBERENSURE, key.c_str(), NULL, &v, dic);
			}
			return dic;
		}

		return NULL;
	}
};

//---------------------------------------------------------------------------
// class registration (mirrors csvParser.cpp style)
//---------------------------------------------------------------------------

static iTJSNativeInstance *TJS_INTF_METHOD Create_NI_PSBFile() {
	return new NI_PSBFile();
}

static iTJSDispatch2 *Create_NC_PSBFile() {
	tTJSNativeClassForPlugin *classobj =
		TJSCreateNativeClassForPlugin(TJS_W("PSBFile"), Create_NI_PSBFile);

	TJS_BEGIN_NATIVE_MEMBERS(PSBFile)

		TJS_DECL_EMPTY_FINALIZE_METHOD

		TJS_BEGIN_NATIVE_CONSTRUCTOR_DECL(
			/*var.name*/_this,
			/*var.type*/NI_PSBFile,
			/*TJS class name*/PSBFile)
		{
			if (numparams >= 1 && param[0]->Type() != tvtVoid) {
				_this->Load(param[0]->AsStringNoAddRef());
			}
			return TJS_S_OK;
		}
		TJS_END_NATIVE_CONSTRUCTOR_DECL(/*TJS class name*/PSBFile)

		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/load)
		{
			TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/NI_PSBFile);
			if (numparams < 1) return TJS_E_BADPARAMCOUNT;
			bool ok = _this->Load(param[0]->AsStringNoAddRef());
			if (result) *result = (tjs_int)(ok ? 1 : 0);
			return TJS_S_OK;
		}
		TJS_END_NATIVE_METHOD_DECL(/*func. name*/load)

		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/clear)
		{
			TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/NI_PSBFile);
			_this->Clear();
			return TJS_S_OK;
		}
		TJS_END_NATIVE_METHOD_DECL(/*func. name*/clear)

		TJS_BEGIN_NATIVE_PROP_DECL(root)
		{
			TJS_BEGIN_NATIVE_PROP_GETTER
			{
				TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/NI_PSBFile);
				iTJSDispatch2 *root = _this->GetRoot();
				if (!root)
					TVPThrowExceptionMessage(TJS_W("PSBFile: no loaded psb data"));
				result->Clear();
			result->SetObject(root, root);
				root->Release();
				return TJS_S_OK;
			}
			TJS_END_NATIVE_PROP_GETTER
			TJS_DENY_NATIVE_PROP_SETTER
		}
		TJS_END_NATIVE_PROP_DECL(root)

	TJS_END_NATIVE_MEMBERS

	return classobj;
}

static void addMemberGlobal(iTJSDispatch2 *dispatch, const tjs_char *name, iTJSDispatch2 *member) {
	tTJSVariant var(member);
	member->Release();
	dispatch->PropSet(TJS_MEMBERENSURE, name, NULL, &var, dispatch);
}

static void InitPlugin_PSBFile() {
	iTJSDispatch2 *global = TVPGetScriptDispatch();
	if (global) {
		addMemberGlobal(global, TJS_W("PSBFile"), Create_NC_PSBFile());
		global->Release();
	}
	if (g_psbMedia == NULL) {
		g_psbMedia = new PsbStorageMedia();
		TVPRegisterStorageMedia(g_psbMedia);
	}
}

static void UninitPlugin_PSBFile() {
	if (g_psbMedia != NULL) {
		TVPUnregisterStorageMedia(g_psbMedia);
		g_psbMedia->Release();
		g_psbMedia = NULL;
	}
	g_psbRegistry.clear();
}

NCB_PRE_REGIST_CALLBACK(InitPlugin_PSBFile);
NCB_POST_UNREGIST_CALLBACK(UninitPlugin_PSBFile);
