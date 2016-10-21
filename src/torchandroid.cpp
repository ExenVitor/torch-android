#include "torchandroid.h"

#include <android/asset_manager.h>
#include "TH/TH.h"
#include "THApkFile.h"

void android_fopen_set_asset_manager(AAssetManager* manager);
FILE* android_fopen(const char* fname, const char* mode);

// static const luaL_reg lualibs[] =
//   {
//     { "base",       luaopen_base },
//     { NULL,         NULL }
//   };

// function to open up all the Lua libraries you declared above
static lua_State* openlualibs(lua_State *l)
{
  //just open all lua standard libs
  luaL_openlibs(l);
  //no need to call luaopen_base, so remove code below
  // const luaL_reg *lib;
  // int ret;
  // for (lib = lualibs; lib->func != NULL; lib++)
  //   {
  //     lib->func(l);
  //     lua_settop(l, 0);
  //   }
  return l;
}

// function to redirect prints to logcat
static int landroid_print(lua_State* L) {
  int nargs = lua_gettop(L);
  for (int i=1; i <= nargs; i++) {
    if (lua_isstring(L, i)) {
      D(lua_tostring(L, i));
    }
  }
  return 0;
}

static const struct luaL_reg androidprint [] = {
  {"print", landroid_print},
  {NULL, NULL} /* end of array */
};

extern int luaopen_landroidprint(lua_State *L)
{
  lua_getglobal(L, "_G");
  luaL_register(L, NULL, androidprint);
  lua_pop(L, 1);
}

long android_asset_get_size(const char *name) {
  FILE *fl = android_fopen(name, "r");
  if (fl == NULL)
    return -1;
  fseek(fl, 0, SEEK_END);
  long len = ftell(fl);
  return len;
}

char* android_asset_get_bytes(const char *name) {
  FILE *fl = android_fopen(name, "r");
  if (fl == NULL)
    return NULL;
  fseek(fl, 0, SEEK_END);
  long len = ftell(fl);
  char *buf = (char *)malloc(len);
  fseek(fl, 0, SEEK_SET);
  size_t loaded = fread(buf, 1, len, fl);
  if (loaded != len) {
    fclose(fl);
    return NULL;
  }
  fclose(fl);
  return buf;
}

int try_load_lua_file(lua_State *L,const char* base_path, const char* suffix, const char* module_name){
  char pname[4096];
  long size;
  char *filebytes;

  strlcpy(pname, base_path, sizeof(pname));
  strlcat(pname, module_name, sizeof(pname));
  strlcat(pname, suffix, sizeof(pname));
  size = android_asset_get_size(pname);
  if(size != -1){
    filebytes = android_asset_get_bytes(pname);
    luaL_loadbuffer(L, filebytes, size, module_name);
    return 1;
  }
  return 0;
}

int try_load_module(lua_State *L, const char* base_path, const char* module_name){
  const char* file_suffix = ".lua";
  const char* init_suffix = "/init.lua";

  if(try_load_lua_file(L, base_path, file_suffix, module_name)){
    return 1;
  }

  if(try_load_lua_file(L, base_path, init_suffix, module_name)){
    return 1;
  }

  return 0;
}

extern int loader_android (lua_State *L) {

  const char* sys_base_path = "lua/5.1/";
  //our project dir
  const char* project_ref_path = "proj_ref/";

  const char* name = lua_tostring(L, -1);
 
  name = luaL_gsub(L, name, ".", LUA_DIRSEP);

  //try lua/5.1/{name}.lua or lua/5.1/{name}/init.lua
  if(try_load_module(L, sys_base_path, name)){
    return 1;
  }

  //try proj_ref/{name}.lua or proj_ref/{name}/init.lua
  if(try_load_module(L, project_ref_path, name)){
    return 1;
  }
  
  D("loader_android: name=%s failed", name);
  return 1;
}

lua_State* inittorch(AAssetManager* manager, const char* libpath) {
  /* Declare a Lua State, open the Lua State */
  lua_State *L;
  L = lua_open();
  // set the asset manager
  android_fopen_set_asset_manager(manager);
  THApkFile_setAAssetManager((void *) manager);
  openlualibs(L);
  luaopen_landroidprint(L);

  // concat libpath to package.cpath
  lua_getglobal(L, "package");
  lua_getfield(L, -1, "cpath");
  const char* current_cpath = lua_tostring(L, -1);
  lua_pop(L, 1);
  char final_cpath[4096];
  strcpy(final_cpath, libpath);
  strcat(final_cpath, "/?.so;");
  strcat(final_cpath, current_cpath);
  lua_pushstring(L, final_cpath);
  lua_setfield(L, -2, "cpath");
  lua_pop(L, 1); // balance stack
  // add an android module loader to package.loaders
  lua_getglobal(L, "package");
  lua_getfield(L, -1, "loaders");
  int numloaders = lua_objlen(L, -1);
  lua_pushcfunction(L, loader_android);
  lua_rawseti(L, -2, numloaders+1);
  lua_pop(L, 1);
  return L;
}
