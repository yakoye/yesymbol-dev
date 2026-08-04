#include "storage.h"
#include <strsafe.h>
#include <wchar.h>
#define YS_COMMON_MAGIC 0x31435359u
static const WCHAR *YS_REG_PATH=L"Software\\YeTools\\YeSymbol";
typedef struct YSCommonPersist { uint32_t magic,version,count,next_serial; YSCommonItem items[YS_MAX_COMMON]; } YSCommonPersist;
static BOOL eq(const WCHAR*a,const WCHAR*b){return a&&b&&wcscmp(a,b)==0;} static void cp(WCHAR*d,const WCHAR*s){StringCchCopyW(d,YS_MAX_SEQUENCE,s?s:L"");}
void ys_storage_init_list(YSDynamicList*l,size_t c){if(!l)return;ZeroMemory(l,sizeof(*l));l->capacity=c>YS_MAX_CUSTOM?YS_MAX_CUSTOM:c;}
BOOL ys_list_add_front_unique(YSDynamicList*l,const WCHAR*t){size_t i,f=(size_t)-1,n;if(!l||!t||!t[0]||!l->capacity)return FALSE;for(i=0;i<l->count;i++)if(eq(l->items[i],t)){f=i;break;}if(f==0)return FALSE;if(f!=(size_t)-1){for(i=f;i>0;i--)cp(l->items[i],l->items[i-1]);}else{n=l->count<l->capacity?l->count:l->capacity-1;for(i=n;i>0;i--)cp(l->items[i],l->items[i-1]);if(l->count<l->capacity)l->count++;}cp(l->items[0],t);return TRUE;}
BOOL ys_list_add_back_unique(YSDynamicList*l,const WCHAR*t){size_t i;if(!l||!t||!t[0]||l->count>=l->capacity)return FALSE;for(i=0;i<l->count;i++)if(eq(l->items[i],t))return FALSE;cp(l->items[l->count++],t);return TRUE;}
BOOL ys_list_remove(YSDynamicList*l,size_t x){size_t i;if(!l||x>=l->count)return FALSE;for(i=x;i+1<l->count;i++)cp(l->items[i],l->items[i+1]);if(l->count)l->count--;l->items[l->count][0]=0;return TRUE;}
static void load_multi(const WCHAR*n,YSDynamicList*l){HKEY k;DWORD ty=0,b=0;WCHAR*buf,*p;if(RegOpenKeyExW(HKEY_CURRENT_USER,YS_REG_PATH,0,KEY_QUERY_VALUE,&k)!=ERROR_SUCCESS)return;if(RegQueryValueExW(k,n,0,&ty,0,&b)!=ERROR_SUCCESS||ty!=REG_MULTI_SZ||b<2){RegCloseKey(k);return;}buf=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,b+4);if(buf&&RegQueryValueExW(k,n,0,&ty,(BYTE*)buf,&b)==ERROR_SUCCESS){for(p=buf;*p&&l->count<l->capacity;p+=wcslen(p)+1)ys_list_add_back_unique(l,p);}if(buf)HeapFree(GetProcessHeap(),0,buf);RegCloseKey(k);}
static void save_multi(const WCHAR*n,const YSDynamicList*l){HKEY k;DWORD d;size_t i,total=2,off=0;WCHAR*buf;for(i=0;i<l->count;i++)total+=wcslen(l->items[i])+1;buf=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,total*sizeof(WCHAR));if(!buf)return;for(i=0;i<l->count;i++){size_t z=wcslen(l->items[i]);memcpy(buf+off,l->items[i],(z+1)*sizeof(WCHAR));off+=z+1;}if(RegCreateKeyExW(HKEY_CURRENT_USER,YS_REG_PATH,0,0,0,KEY_SET_VALUE,0,&k,&d)==ERROR_SUCCESS){RegSetValueExW(k,n,0,REG_MULTI_SZ,(BYTE*)buf,(DWORD)(total*sizeof(WCHAR)));RegCloseKey(k);}HeapFree(GetProcessHeap(),0,buf);}
void ys_storage_load_recent(YSDynamicList*l){load_multi(L"Recent",l);}void ys_storage_load_custom(YSDynamicList*l){load_multi(L"Custom",l);}void ys_storage_save_recent(const YSDynamicList*l){save_multi(L"Recent",l);}void ys_storage_save_custom(const YSDynamicList*l){save_multi(L"Custom",l);}
void ys_common_init(YSCommonList*l){if(l){ZeroMemory(l,sizeof(*l));l->next_serial=1;}}
static int cmp(const YSCommonItem*a,const YSCommonItem*b){if(a->use_count!=b->use_count)return a->use_count>b->use_count?-1:1;if(a->serial!=b->serial)return a->serial<b->serial?-1:1;return wcscmp(a->text,b->text);}void ys_common_sort(YSCommonList*l){size_t i;if(!l)return;for(i=1;i<l->count;i++){YSCommonItem key=l->items[i];size_t j=i;while(j&&cmp(&key,&l->items[j-1])<0){l->items[j]=l->items[j-1];j--;}l->items[j]=key;}}
int ys_common_find(const YSCommonList*l,const WCHAR*t){size_t i;if(!l||!t)return-1;for(i=0;i<l->count;i++)if(!wcscmp(l->items[i].text,t))return(int)i;return-1;}
BOOL ys_common_add(YSCommonList*l,const WCHAR*t,uint32_t c){YSCommonItem*i;if(!l||!t||!t[0]||l->count>=YS_MAX_COMMON||ys_common_find(l,t)>=0)return FALSE;i=&l->items[l->count++];ZeroMemory(i,sizeof(*i));cp(i->text,t);i->use_count=c;i->serial=l->next_serial++;ys_common_sort(l);return TRUE;}
BOOL ys_common_increment(YSCommonList*l,const WCHAR*t){int i=ys_common_find(l,t);if(i<0)return FALSE;if(l->items[i].use_count!=UINT32_MAX)l->items[i].use_count++;ys_common_sort(l);return TRUE;}BOOL ys_common_remove(YSCommonList*l,size_t x){size_t i;if(!l||x>=l->count)return FALSE;for(i=x;i+1<l->count;i++)l->items[i]=l->items[i+1];if(l->count)l->count--;ZeroMemory(&l->items[l->count],sizeof(l->items[0]));return TRUE;}
BOOL ys_storage_load_common(YSCommonList*l){HKEY k;DWORD ty=0,b=sizeof(YSCommonPersist);YSCommonPersist*p;BOOL ok=FALSE;if(!l||RegOpenKeyExW(HKEY_CURRENT_USER,YS_REG_PATH,0,KEY_QUERY_VALUE,&k)!=ERROR_SUCCESS)return FALSE;p=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*p));if(p&&RegQueryValueExW(k,L"CommonV1",0,&ty,(BYTE*)p,&b)==ERROR_SUCCESS&&ty==REG_BINARY&&b==sizeof(*p)&&p->magic==YS_COMMON_MAGIC&&p->version==1&&p->count<=YS_MAX_COMMON){l->count=p->count;l->next_serial=p->next_serial?p->next_serial:1;memcpy(l->items,p->items,p->count*sizeof(YSCommonItem));ys_common_sort(l);ok=TRUE;}if(p)HeapFree(GetProcessHeap(),0,p);RegCloseKey(k);return ok;}
void ys_storage_save_common(const YSCommonList*l){HKEY k;DWORD d;YSCommonPersist*p;if(!l)return;p=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*p));if(!p)return;p->magic=YS_COMMON_MAGIC;p->version=1;p->count=(uint32_t)l->count;p->next_serial=l->next_serial;memcpy(p->items,l->items,l->count*sizeof(YSCommonItem));if(RegCreateKeyExW(HKEY_CURRENT_USER,YS_REG_PATH,0,0,0,KEY_SET_VALUE,0,&k,&d)==ERROR_SUCCESS){RegSetValueExW(k,L"CommonV1",0,REG_BINARY,(BYTE*)p,sizeof(*p));RegCloseKey(k);}HeapFree(GetProcessHeap(),0,p);}

BOOL ys_storage_load_bool(const WCHAR *value_name, BOOL default_value) {
    HKEY key;
    DWORD type = 0;
    DWORD value = 0;
    DWORD bytes = sizeof(value);
    if (!value_name || !value_name[0]) return default_value;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, YS_REG_PATH, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return default_value;
    if (RegQueryValueExW(key, value_name, 0, &type, (BYTE *)&value, &bytes) != ERROR_SUCCESS ||
        type != REG_DWORD || bytes != sizeof(value)) {
        RegCloseKey(key);
        return default_value;
    }
    RegCloseKey(key);
    return value ? TRUE : FALSE;
}

void ys_storage_save_bool(const WCHAR *value_name, BOOL value) {
    HKEY key;
    DWORD disposition;
    DWORD data = value ? 1u : 0u;
    if (!value_name || !value_name[0]) return;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, YS_REG_PATH, 0, NULL, 0, KEY_SET_VALUE, NULL, &key, &disposition) == ERROR_SUCCESS) {
        RegSetValueExW(key, value_name, 0, REG_DWORD, (const BYTE *)&data, sizeof(data));
        RegCloseKey(key);
    }
}


void ys_search_history_init(YSSearchHistory *history) {
    if (history) ZeroMemory(history, sizeof(*history));
}

static void ys_search_history_copy(WCHAR *destination, const WCHAR *source) {
    StringCchCopyW(destination, YS_MAX_QUERY, source ? source : L"");
}

BOOL ys_search_history_add_front(YSSearchHistory *history, const WCHAR *query) {
    size_t index;
    size_t found = (size_t)-1;
    size_t last;
    if (!history || !query || !query[0]) return FALSE;
    for (index = 0; index < history->count; ++index) {
        if (_wcsicmp(history->items[index], query) == 0) {
            found = index;
            break;
        }
    }
    if (found == 0) return FALSE;
    if (found != (size_t)-1) {
        for (index = found; index > 0; --index) {
            ys_search_history_copy(history->items[index], history->items[index - 1]);
        }
    } else {
        last = history->count < YS_MAX_SEARCH_HISTORY ? history->count : YS_MAX_SEARCH_HISTORY - 1u;
        for (index = last; index > 0; --index) {
            ys_search_history_copy(history->items[index], history->items[index - 1]);
        }
        if (history->count < YS_MAX_SEARCH_HISTORY) ++history->count;
    }
    ys_search_history_copy(history->items[0], query);
    return TRUE;
}

void ys_storage_load_search_history(YSSearchHistory *history) {
    HKEY key;
    DWORD type = 0;
    DWORD bytes = 0;
    WCHAR *buffer;
    WCHAR *cursor;
    if (!history) return;
    ys_search_history_init(history);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, YS_REG_PATH, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return;
    if (RegQueryValueExW(key, L"SearchHistory", NULL, &type, NULL, &bytes) != ERROR_SUCCESS ||
        type != REG_MULTI_SZ || bytes < sizeof(WCHAR) * 2u) {
        RegCloseKey(key);
        return;
    }
    buffer = (WCHAR *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes + sizeof(WCHAR) * 2u);
    if (buffer && RegQueryValueExW(key, L"SearchHistory", NULL, &type, (BYTE *)buffer, &bytes) == ERROR_SUCCESS) {
        for (cursor = buffer; *cursor && history->count < YS_MAX_SEARCH_HISTORY; cursor += wcslen(cursor) + 1u) {
            if (*cursor) {
                ys_search_history_copy(history->items[history->count], cursor);
                ++history->count;
            }
        }
    }
    if (buffer) HeapFree(GetProcessHeap(), 0, buffer);
    RegCloseKey(key);
}

void ys_storage_save_search_history(const YSSearchHistory *history) {
    HKEY key;
    DWORD disposition;
    size_t index;
    size_t total = 2u;
    size_t offset = 0;
    WCHAR *buffer;
    if (!history) return;
    for (index = 0; index < history->count; ++index) total += wcslen(history->items[index]) + 1u;
    buffer = (WCHAR *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, total * sizeof(WCHAR));
    if (!buffer) return;
    for (index = 0; index < history->count; ++index) {
        size_t length = wcslen(history->items[index]);
        memcpy(buffer + offset, history->items[index], (length + 1u) * sizeof(WCHAR));
        offset += length + 1u;
    }
    if (RegCreateKeyExW(HKEY_CURRENT_USER, YS_REG_PATH, 0, NULL, 0, KEY_SET_VALUE, NULL, &key, &disposition) == ERROR_SUCCESS) {
        RegSetValueExW(key, L"SearchHistory", 0, REG_MULTI_SZ, (const BYTE *)buffer,
                       (DWORD)(total * sizeof(WCHAR)));
        RegCloseKey(key);
    }
    HeapFree(GetProcessHeap(), 0, buffer);
}
