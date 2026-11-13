#pragma once
#include <array>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <strings.h>

namespace recorder {
enum Kind {Boot=1,RawRx=2,Local=5};
struct Storage {uint8_t flags; uint64_t used,total; uint32_t drops;};
}
using QueueHandle_t = void *;
using portMUX_TYPE = int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) (void)x
#define portEXIT_CRITICAL(x) (void)x
struct VFSImpl {};
namespace fs {
using FSImplPtr = std::shared_ptr<VFSImpl>;
struct SDFS {
    unsigned _pdrv=0;
    SDFS(FSImplPtr) {}
    template<class... Args> bool begin(Args&&...) {return available;}
    void end() {}
    unsigned sectorSize() {return 512;}
    static inline bool available=true;
};
}
constexpr int HSPI=1;
struct SPIClass {SPIClass(int) {}};
inline uint64_t esp_timer_get_time() {return 0;}
using DWORD=uint32_t;
using UINT=unsigned;
enum FRESULT {FR_OK,FR_EXIST,FR_DENIED};
enum {FA_READ=1, FA_WRITE=2, FA_CREATE_NEW=4, AM_DIR=16};
namespace fixture {
inline std::map<std::string,std::vector<uint8_t>> files;
inline unsigned clusters=64;
inline bool writeFail=false;
inline unsigned allocated() {
    unsigned n=0;
    for (const auto &f:files) n+=(f.second.size()+511)/512;
    return n;
}
}
struct FATFS {unsigned csize,n_fatent;};
inline FRESULT f_getfree(const char*,DWORD *free,FATFS **fs){
    static FATFS value;
    value={1,fixture::clusters+2};*fs=&value;
    *free=fixture::clusters-fixture::allocated();return FR_OK;
}
struct FIL {std::string name;size_t pos=0;};
struct FF_DIR {std::vector<std::string> names;size_t pos=0;};
struct FILINFO {unsigned fattrib=0;char fname[128];};
inline FRESULT f_open(FIL *f,const char *path,int mode) {
    auto &files=fixture::files;
    if(mode&FA_CREATE_NEW) {if(files.count(path))return FR_EXIST;files[path]={};}
    if(!files.count(path))return FR_DENIED;
    *f={path,0};return FR_OK;
}
inline FRESULT f_read(FIL *f,void *dst,UINT size,UINT *got) {
    auto &b=fixture::files.at(f->name);*got=std::min<size_t>(size,b.size()-f->pos);
    if(*got)memcpy(dst,b.data()+f->pos,*got);
    f->pos+=*got;return FR_OK;
}
inline FRESULT f_write(FIL *f,const void *src,UINT size,UINT *got) {
    *got=0;
    if(fixture::writeFail)return FR_DENIED;
    auto &b=fixture::files.at(f->name);
    size_t length=std::max(b.size(),f->pos+size);
    if(fixture::allocated()+(length+511)/512-(b.size()+511)/512>fixture::clusters)return FR_OK;
    b.resize(length);memcpy(b.data()+f->pos,src,size);f->pos+=size;*got=size;return FR_OK;
}
inline size_t f_size(FIL *f){return fixture::files.at(f->name).size();}
inline bool f_eof(FIL *f){return f->pos==f_size(f);}
inline FRESULT f_close(FIL*){return FR_OK;}
inline FRESULT f_sync(FIL*){return fixture::writeFail?FR_DENIED:FR_OK;}
inline FRESULT f_mkdir(const char*){return FR_OK;}
inline FRESULT f_unlink(const char *p){return fixture::files.erase(p)?FR_OK:FR_DENIED;}
inline FRESULT f_opendir(FF_DIR *dir,const char *p){
    *dir={};
    const std::string prefix=std::string(p)+"/";
    for(const auto &f:fixture::files)if(f.first.find(prefix)==0)dir->names.push_back(f.first.substr(prefix.size()));
    return FR_OK;
}
inline FRESULT f_readdir(FF_DIR *d,FILINFO *i){
    snprintf(i->fname,sizeof(i->fname),"%s",d->pos==d->names.size()?"":d->names[d->pos++].c_str());return FR_OK;
}
inline FRESULT f_closedir(FF_DIR*){return FR_OK;}
