#include "ncbind/ncbind.hpp"

void TVPLoadInternalPlugins()
{
	ncbAutoRegister::AllRegist();
	ncbAutoRegister::LoadModule(TJS_W("xp3filter.dll"));
	ncbAutoRegister::LoadModule(TJS_W("saveStruct.dll"));
	ncbAutoRegister::LoadModule(TJS_W("addFont.dll"));
	ncbAutoRegister::LoadModule(TJS_W("csvParser.dll"));
	ncbAutoRegister::LoadModule(TJS_W("getSample.dll"));
	ncbAutoRegister::LoadModule(TJS_W("psbfile.dll"));
	ncbAutoRegister::LoadModule(TJS_W("dirlist.dll"));
	ncbAutoRegister::LoadModule(TJS_W("packinone.dll"));
	ncbAutoRegister::LoadModule(TJS_W("textrender.dll"));
}

void TVPUnloadInternalPlugins()
{
	ncbAutoRegister::AllUnregist();
}

bool TVPLoadInternalPlugin(const ttstr &_name)
{
	return ncbAutoRegister::LoadModule(TVPExtractStorageName(_name));
}
