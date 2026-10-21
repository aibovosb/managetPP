/* ============================================================
 * 隐藏进程管理器 HiddenProcManager  v1.0
 * 托盘常驻 + 子程序隐藏启动管理（C + Win32 API）
 * 编译: tcc -subsystem windows main.c -o HiddenProcManager.exe
 * 链接: user32 gdi32 shell32 advapi32 comctl32 ole32
 * 说明: TinyCC 精简头文件缺 shellapi/commctrl，已用自声明兼容层补齐
 * ============================================================ */
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <wchar.h>
/* ============================================================
 * 兼容层：自声明缺失的 API（shellapi / commctrl / 编码转换）
 * ============================================================ */
/* ---- 注册表常量 ---- */
#ifndef KEY_READ
#define KEY_READ 0x20019L
#endif
#ifndef KEY_SET_VALUE
#define KEY_SET_VALUE 0x0002L
#endif
#ifndef REG_SZ
#define REG_SZ 1
#endif
/* ---- 编码转换 API ---- */
#ifndef CP_UTF8
#define CP_UTF8 65001
#endif
int __stdcall WideCharToMultiByte(UINT codepage, DWORD flags, const WCHAR *w, int cch,
                                  char *out, int outmax, const char *defchar, int *useddef);
/* ---- commctrl（列表控件）替代 ---- */
#define LVM_FIRST 0x1000
#define LVM_SETITEMSTATE             (LVM_FIRST + 43)
#define LVM_DELETEALLITEMS            (LVM_FIRST + 9)
#define LVM_INSERTITEMW               (LVM_FIRST + 77)
#define LVM_SETITEMW                  (LVM_FIRST + 76)
#define LVM_INSERTCOLUMNW             (LVM_FIRST + 97)
#define LVM_SETITEMTEXTW              (LVM_FIRST + 116)
#define LVM_GETITEMSTATE              (LVM_FIRST + 44)
#define LVM_SETEXTENDEDLISTVIEWSTYLE  (LVM_FIRST + 54)
#define LVM_GETSUBITEMRECT            (LVM_FIRST + 56)
#define LVIF_TEXT  0x0001
#define LVIF_STATE 0x0008
#define LVIS_SELECTED       0x0002
#define LVIS_STATEIMAGEMASK 0xF000
#define INDEXTOSTATEIMAGEMASK(i) ((i) << 12)
#define LVS_REPORT        0x0001
#define LVS_SHOWSELALWAYS 0x0008
#define LVS_EX_GRIDLINES      0x0001
#define LVS_EX_CHECKBOXES     0x0004
#define LVS_EX_FULLROWSELECT  0x0020
#define LVS_EX_DOUBLEBUFFER   0x00010000
#define NM_FIRST     0
#define NM_DBLCLK    (NM_FIRST - 3)
#define LVN_FIRST    (0U - 100U)
#define LVN_ITEMCHANGED ((UINT)(LVN_FIRST - 1))
#define LVCF_FMT     0x0001
#define LVCF_WIDTH   0x0002
#define LVCF_TEXT    0x0004
#define LVCF_SUBITEM 0x0008
#define LVIR_BOUNDS 0
typedef struct {
    UINT mask; int fmt; int cx; WCHAR *pszText; int cchTextMax; int iSubItem;
    int iImage; int iOrder; int cxMin; int cxDefault; int cxIdeal;
} LVCOLUMNW;
typedef struct {
    UINT mask; int iItem; int iSubItem; UINT state; UINT stateMask;
    WCHAR *pszText; int cchTextMax; int iImage; LPARAM lParam;
    int iIndent; int iGroupId; UINT cColumns; UINT *puColumns;
    UINT *piColFmt; int iGroup;
} LVITEMW;
typedef struct {
    NMHDR hdr; int iItem; int iSubItem; UINT uNewState; UINT uOldState;
    UINT uChanged; POINT ptAction; LPARAM lParam;
} NMLISTVIEW;
typedef struct {
    NMHDR hdr; int iItem; int iSubItem; UINT uNewState; UINT uOldState;
    UINT uChanged; POINT ptAction; LPARAM lParam; UINT uKeyFlags;
} NMITEMACTIVATE;
void __stdcall InitCommonControls(void);
static int LvInsItem(HWND h, const LVITEMW *it) {
    return (int)SendMessageW(h, LVM_INSERTITEMW, 0, (LPARAM)it);
}
static int LvInsColumn(HWND h, int i, const LVCOLUMNW *c) {
    return (int)SendMessageW(h, LVM_INSERTCOLUMNW, (WPARAM)i, (LPARAM)c);
}
static void LvDelAll(HWND h) {
    SendMessageW(h, LVM_DELETEALLITEMS, 0, 0);
}
static void LvSetItemText(HWND h, int i, int sub, WCHAR *txt) {
    LVITEMW it;
    memset(&it, 0, sizeof(it));
    it.iSubItem = sub;
    it.pszText = txt;
    SendMessageW(h, LVM_SETITEMTEXTW, (WPARAM)i, (LPARAM)&it);
}
static UINT LvGetItemState(HWND h, int i, UINT mask) {
    return (UINT)SendMessageW(h, LVM_GETITEMSTATE, (WPARAM)i, (LPARAM)mask);
}
static int LvGetCheck(HWND h, int i) {
    return (((int)SendMessageW(h, LVM_GETITEMSTATE, (WPARAM)i, (LPARAM)LVIS_STATEIMAGEMASK) >> 12) - 1) ? 1 : 0;
}
/* 注意：设置勾选状态必须用 LVM_SETITEMSTATE（条目索引在 wParam，
 * 按 stateMask 生效）；若用 LVM_SETITEMW 则 mask=0 时什么都不会设置，
 * 导致启动时勾选状态无法恢复（已修复的 Bug） */
static void LvSetItemState(HWND h, int i, UINT data, UINT mask) {
    LVITEMW it;
    memset(&it, 0, sizeof(it));
    it.state = data;
    it.stateMask = mask;
    SendMessageW(h, LVM_SETITEMSTATE, (WPARAM)i, (LPARAM)&it);
}
/* ---- shellapi（托盘）替代 ---- */
#define NIM_ADD    0
#define NIM_MODIFY 1
#define NIM_DELETE 2
#define NIF_MESSAGE 0x00000001
#define NIF_ICON    0x00000002
#define NIF_TIP     0x00000004
typedef struct {
    DWORD cbSize; HWND hWnd; UINT uID; UINT uFlags; UINT uCallbackMessage; HICON hIcon;
    WCHAR szTip[128];
    DWORD dwState; DWORD dwStateMask;
    WCHAR szInfo[256];
    UINT uVersion;
    WCHAR szInfoTitle[64];
    DWORD dwInfoFlags;
    GUID guidItem;
} MY_NOTIFYICONDATAW;
int __stdcall Shell_NotifyIconW(DWORD msg, MY_NOTIFYICONDATAW *data);
/* ---- 版本信息（自动提取程序名称用） ---- */
typedef struct {
    WORD  wLength;
    WORD  wValueLength;
    WORD  wType;
    WCHAR szKey[1];
} MY_VS_FIXEDFILEINFO_PAD;
typedef struct {
    DWORD dwSignature;
    DWORD dwStrucVersion;
    DWORD dwFileVersionMS;
    DWORD dwFileVersionLS;
    DWORD dwProductVersionMS;
    DWORD dwProductVersionLS;
    DWORD dwFileFlagsMask;
    DWORD dwFileFlags;
    DWORD dwFileOS;
    DWORD dwFileType;
    DWORD dwFileSubtype;
    DWORD dwFileDateMS;
    DWORD dwFileDateLS;
} MY_VS_FIXEDFILEINFO;
DWORD __stdcall GetFileVersionInfoSizeW(const WCHAR *file, DWORD *handle);
int   __stdcall GetFileVersionInfoW(const WCHAR *file, DWORD handle, DWORD len, void *data);
#ifndef VS_FF_DEBUG
#define VS_FF_DEBUG 0x00000001L
#endif
#ifndef VS_FF_PRERELEASE
#define VS_FF_PRERELEASE 0x00000002L
#endif
/* ---- 图标提取（结果显示用） ---- */
typedef struct {
    HICON hIcon;
    int   iIcon;
    DWORD dwAttributes;
    WCHAR szDisplayName[260];
    WCHAR szTypeName[80];
} MY_SHFILEINFOW;
int __stdcall SHGetFileInfoW(const WCHAR *path, DWORD attrs, MY_SHFILEINFOW *sfi, UINT cb, UINT flags);
void* __stdcall ImageList_Create(int cx, int cy, UINT flags, int cInitial, int cGrow);
int __stdcall ImageList_ReplaceIcon(void *himl, int index, HICON hicon);
#define SHGFI_ICON      0x00000100
#define SHGFI_SMALLICON 0x00000001
#define ILC_COLOR32     0x0020
#define ILC_MASK        0x0001
#define LVM_SETIMAGELIST (LVM_FIRST + 3)
#define LVSIL_SMALL     1
#define LVIF_IMAGE      0x0002
/* ---- 自声明的外部 API（不依赖 OLE 头文件） ---- */
long __stdcall CoInitialize(void *pvReserved);
void __stdcall CoUninitialize(void);
long __stdcall CoCreateInstance(const GUID *clsid, void *outer, DWORD ctx, const GUID *iid, void **ppv);
long __stdcall SHGetFolderPathW(HWND hwnd, int csidl, HANDLE token, DWORD flags, WCHAR *path);
#ifndef CLSCTX_INPROC_SERVER
#define CLSCTX_INPROC_SERVER 1
#endif
#define SLGP_UNCPRIORITY 0x0004
static const GUID g_CLSID_ShellLink  = {0x00021401,0x0000,0x0000,{0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};
static const GUID g_IID_IShellLinkW  = {0x000214F9,0x0000,0x0000,{0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};
static const GUID g_IID_IPersistFile = {0x0000010B,0x0000,0x0000,{0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};
/* IShellLinkW / IPersistFile 最小 vtable（只声明用到的方法，偏移必须正确） */
typedef struct {
    long (__stdcall *QueryInterface)(void*, const GUID*, void**);
    unsigned long (__stdcall *AddRef)(void*);
    unsigned long (__stdcall *Release)(void*);
    long (__stdcall *GetPath)(void*, WCHAR*, int, void*, DWORD);
} ShellLinkVtbl;
typedef struct { ShellLinkVtbl *lpVtbl; } ShellLinkObj;
typedef struct {
    long (__stdcall *QueryInterface)(void*, const GUID*, void**);
    unsigned long (__stdcall *AddRef)(void*);
    unsigned long (__stdcall *Release)(void*);
    long (__stdcall *GetClassID)(void*, GUID*);
    long (__stdcall *IsDirty)(void*);
    long (__stdcall *Load)(void*, const WCHAR*, DWORD);
} PersistFileVtbl;
typedef struct { PersistFileVtbl *lpVtbl; } PersistFileObj;
/* ---------------- 常量 ---------------- */
#define APP_NAME     L"隐藏进程管理器"
#define APP_MUTEX    L"HiddenProcManager_SingleInstance_Mutex"
#define APP_AUTORUN  L"HiddenProcManager"
#define WM_TRAY      (WM_USER + 100)
#define IDM_SHOW     2001
#define IDM_EXIT     2002
#define IDM_AUTORUN  2003
#define IDC_LIST     1101
#define IDC_LOG      1102
#define IDC_BTN_ADD  1103
#define IDC_BTN_DEL  1104
#define IDC_BTN_SAVE 1105
#define IDC_EDIT_INPLACE 900
#define IDC_DLG_SEARCH 1201
#define IDC_DLG_LIST   1202
#define IDC_DLG_ADD    1203
#define IDC_DLG_CANCEL 1204
#define IDC_DLG_FIND   1205
#define IDC_DLG_STATUS 1206
#define IDC_DLG_BTN_SEARCH 1207
#ifndef EN_CHANGE
#define EN_CHANGE 0x0300
#endif
#define WM_APP_SEARCH_DONE     (WM_APP + 1)
#define WM_APP_SEARCH_PROGRESS (WM_APP + 2)
#define MAX_SEARCH 500
#define TIMER_ID 1
#define COL_NAME  0
#define COL_PATH  1
#define COL_ARGS  2
#define COL_DELAY 3
#define COL_HIDE  4
#define COL_STATE 5
#define HM_NONE    0
#define HM_WINDOW  1
#define HM_TASKBAR 2
#define ST_NOTSTART 0
#define ST_RUNNING  1
#define ST_HIDDEN   2
#define ST_STOPPED  3
#define ST_FAILED   4
#define MAX_PROGS 128
/* ---------------- 数据结构 ---------------- */
typedef struct {
    int    enabled;
    WCHAR  name[256];
    WCHAR  path[520];
    WCHAR  args[512];
    double delay;        /* 启动延迟（秒） */
    int    hideMode;     /* HM_NONE / HM_WINDOW / HM_TASKBAR */
    int    status;
    HANDLE hProcess;
    DWORD  pid;
    int    hideAttempts;
    int    trayNoteDone;
} ProgEntry;
static ProgEntry g_progs[MAX_PROGS];
static int g_count = 0;
static HINSTANCE g_hInst;
static HWND g_hMain, g_hList, g_hLog;
static HFONT g_hFont;
static HICON g_hAppIcon = NULL;   /* 程序图标（透明单眼睛） */
static MY_NOTIFYICONDATAW g_nid;
static BOOL g_trayAdded = FALSE;
static int g_queueActive = FALSE;
static int g_queueIndex = 0;
static DWORD g_queueNextTick = 0;
static int g_tick = 0;
static HWND g_hEdit = NULL;
static int g_editItem = -1, g_editSub = -1;
static WNDPROC g_oldEditProc = NULL;
typedef struct { WCHAR name[256]; WCHAR path[520]; } SearchResult;
static SearchResult g_results[MAX_SEARCH];
static int g_resultCount = 0;
static CRITICAL_SECTION g_cs;
static HANDLE g_searchThread = NULL;
static volatile int g_searchStop = 0;
static WCHAR g_keyword[128];
static HWND g_hDlg = NULL;
static HWND g_hDlgList = NULL;
static WNDPROC g_oldDlgSearchProc = NULL;
static HWND g_hDlgSearch = NULL;
static void *g_hImgList = NULL;   /* HIMAGELIST（结果图标） */
static int g_suppressNotify = 0;  /* >0 时忽略勾选变化通知（刷新/插入行期间） */
static int g_searchBusy = 0;      /* 搜索进行中标志 */
static int g_autoFind = 0;        /* 1=打开弹窗时的自动查找（区别于手动全盘搜索） */
static int g_filterMode = 0;      /* 1=本地实时过滤（不重新扫盘） */
static SearchResult g_allResults[MAX_SEARCH];  /* 未过滤的完整结果 */
static int g_allCount = 0;
static DWORD g_hidePid = 0;
static int g_hideFound = 0;
static const WCHAR *g_hideNames[]  = { L"不隐藏", L"仅隐藏窗口", L"隐藏窗口+任务栏" };
static const WCHAR *g_stateNames[] = { L"未启动", L"运行中", L"已隐藏", L"已停止", L"启动失败" };
/* ---------------- 前置声明 ---------------- */
static void LogLine(const WCHAR *text);
static void LogLine2(const WCHAR *fmt, ...);
static void SetProgStatus(int idx, int st);
static void RefreshListItems(void);
static void AddProgramEntry(const WCHAR *name, const WCHAR *path);
static void StartProgram(int idx);
static void UnhideProgram(int idx);
static void KillAllChildren(void);
static void CommitEdit(void);
static BOOL CALLBACK SetChildFont(HWND h, LPARAM lp);
static BOOL CALLBACK HideEnumProc(HWND hwnd, LPARAM lp);
static BOOL CALLBACK UnhideEnumProc(HWND hwnd, LPARAM lp);
static void DoSearch(HWND hwnd);
/* ---------------- 日志 ---------------- */
static void LogLine(const WCHAR *text) {
    if (!g_hLog) return;
    SYSTEMTIME st;
    WCHAR head[48];
    GetLocalTime(&st);
    wsprintfW(head, L"[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
    /* 只读 Edit 会拒绝 EM_REPLACESEL，需临时取消只读 */
    SendMessageW(g_hLog, EM_SETREADONLY, FALSE, 0);
    int len = GetWindowTextLengthW(g_hLog);
    SendMessageW(g_hLog, EM_SETSEL, len, len);
    SendMessageW(g_hLog, EM_REPLACESEL, FALSE, (LPARAM)head);
    len = GetWindowTextLengthW(g_hLog);
    SendMessageW(g_hLog, EM_SETSEL, len, len);
    SendMessageW(g_hLog, EM_REPLACESEL, FALSE, (LPARAM)text);
    len = GetWindowTextLengthW(g_hLog);
    SendMessageW(g_hLog, EM_SETSEL, len, len);
    SendMessageW(g_hLog, EM_REPLACESEL, FALSE, (LPARAM)L"\r\n");
    SendMessageW(g_hLog, EM_SETREADONLY, TRUE, 0);
    /* 滚动到底部 */
    SendMessageW(g_hLog, EM_SCROLLCARET, 0, 0);
}
static void LogLine2(const WCHAR *fmt, ...) {
    WCHAR buf[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(buf, 1024, fmt, ap);
    buf[1023] = 0;
    va_end(ap);
    LogLine(buf);
}
/* ---------------- 编码转换 ---------------- */
static int WideToUtf8(const WCHAR *w, char *out, int outmax) {
    return WideCharToMultiByte(CP_UTF8, 0, w, -1, out, outmax, NULL, NULL);
}
/* ---------------- JSON 配置 ---------------- */
static void GetExeDir(WCHAR *dir, int maxn) {
    WCHAR full[1024];
    GetModuleFileNameW(NULL, full, 1024);
    WCHAR *p = wcsrchr(full, L'\\');
    if (p) *p = 0;
    lstrcpynW(dir, full, maxn);
}
/* 程序图标：优先使用 exe 内嵌的 ICON 资源（app.rc 里的 101 号），
 * 这样「exe 文件图标」与「托盘/窗口图标」完全同源、绝不会不一致；
 * 若编译时没带资源，再回退到 exe 同目录的 icon.ico，最后回退系统图标。 */
#define APP_ICON_RES 101
static HICON LoadAppIcon(int cx, int cy) {
    HICON h = NULL;
    /* 1. exe 内嵌资源（与资源管理器显示的 exe 图标是同一张图） */
    if (g_hInst)
        h = (HICON)LoadImageW(g_hInst, (const WCHAR *)(INT_PTR)APP_ICON_RES,
                              IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR);
    if (!h)
        h = (HICON)LoadIconW(g_hInst, (const WCHAR *)(INT_PTR)APP_ICON_RES);
    /* 2. 回退：exe 同目录 icon.ico */
    if (!h) {
        WCHAR dir[1024], ico[1100];
        GetExeDir(dir, 1024);
        _snwprintf(ico, 1100, L"%s\\icon.ico", dir);
        ico[1099] = 0;
        h = (HICON)LoadImageW(NULL, ico, IMAGE_ICON, cx, cy, LR_LOADFROMFILE);
    }
    /* 3. 最后回退系统图标 */
    if (!h) h = LoadIconW(NULL, IDI_APPLICATION);
    return h;
}
static void GetConfigPath(WCHAR *buf, int maxn) {
    WCHAR dir[1024];
    GetExeDir(dir, 1024);
    _snwprintf(buf, maxn, L"%s\\config.json", dir);
    buf[maxn - 1] = 0;
}
static void JsonAppendEscaped(FILE *f, const WCHAR *w) {
    char buf[2048];
    WideToUtf8(w, buf, 2048);
    fputc('"', f);
    for (char *p = buf; *p; p++) {
        if (*p == '"' || *p == '\\') { fputc('\\', f); fputc(*p, f); }
        else if ((unsigned char)*p < 0x20) fprintf(f, "\\u%04x", (unsigned char)*p);
        else fputc(*p, f);
    }
    fputc('"', f);
}
static int SaveConfig(void) {
    WCHAR cfg[1100];
    GetConfigPath(cfg, 1100);
    FILE *f = _wfopen(cfg, L"wb");
    if (!f) { LogLine(L"保存失败：无法写入 config.json"); return 0; }
    fprintf(f, "{\n  \"programs\": [\n");
    for (int i = 0; i < g_count; i++) {
        ProgEntry *p = &g_progs[i];
        fprintf(f, "    {\"enabled\": %s, \"name\": ", p->enabled ? "true" : "false");
        JsonAppendEscaped(f, p->name);
        fprintf(f, ", \"path\": ");
        JsonAppendEscaped(f, p->path);
        fprintf(f, ", \"args\": ");
        JsonAppendEscaped(f, p->args);
        fprintf(f, ", \"delay\": %g, \"hide\": %d}%s\n",
                p->delay, p->hideMode, (i + 1 < g_count) ? "," : "");
    }
    fprintf(f, "  ]\n}\n");
    fclose(f);
    LogLine(L"列表已保存到 config.json（下次启动自动加载）");
    return 1;
}
static const char *SkipWs(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    return p;
}
/* 解析 JSON 字符串（含转义），返回闭引号之后的位置 */
static const char *JsonParseString(const char *p, WCHAR *out, int outmax) {
    int oi = 0;
    p = SkipWs(p);
    if (*p != '"') { out[0] = 0; return p; }
    p++;
    while (*p && *p != '"') {
        unsigned int cp;
        if (*p == '\\') {
            p++;
            switch (*p) {
                case '"':  cp = '"';  p++; break;
                case '\\': cp = '\\'; p++; break;
                case '/':  cp = '/';  p++; break;
                case 'n':  cp = '\n'; p++; break;
                case 't':  cp = '\t'; p++; break;
                case 'r':  cp = '\r'; p++; break;
                case 'b':  cp = '\b'; p++; break;
                case 'f':  cp = '\f'; p++; break;
                case 'u': {
                    p++; cp = 0;
                    for (int k = 0; k < 4 && *p; k++, p++) {
                        char c = *p; int v;
                        if (c >= '0' && c <= '9') v = c - '0';
                        else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
                        else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
                        else break;
                        cp = cp * 16 + (unsigned)v;
                    }
                    break;
                }
                default: cp = (unsigned char)*p; p++; break;
            }
        } else if ((unsigned char)*p < 0x80) {
            cp = (unsigned char)*p; p++;
        } else {
            int len; unsigned char c = (unsigned char)*p;
            if      ((c & 0xE0) == 0xC0) len = 2;
            else if ((c & 0xF0) == 0xE0) len = 3;
            else if ((c & 0xF8) == 0xF0) len = 4;
            else len = 1;
            cp = c & (unsigned)(0xFF >> (len + 1));
            p++;
            int ok = 1;
            for (int k = 1; k < len; k++) {
                if ((*p & 0xC0) != 0x80) { ok = 0; break; }
                cp = (cp << 6) | (unsigned)(*p & 0x3F);
                p++;
            }
            if (!ok) cp = '?';
        }
        if (oi < outmax - 1) out[oi++] = (WCHAR)cp;
    }
    if (*p == '"') p++;
    out[oi] = 0;
    return p;
}
static int JsonObjGetStr(const char *obj, const char *key, WCHAR *out, int outmax) {
    char pat[64];
    _snprintf(pat, 64, "\"%s\"", key); pat[63] = 0;
    const char *p = strstr(obj, pat);
    if (!p) { out[0] = 0; return 0; }
    p += strlen(pat);
    p = SkipWs(p);
    if (*p != ':') { out[0] = 0; return 0; }
    JsonParseString(p + 1, out, outmax);
    return 1;
}
static double JsonObjGetNum(const char *obj, const char *key, double defv) {
    char pat[64];
    _snprintf(pat, 64, "\"%s\"", key); pat[63] = 0;
    const char *p = strstr(obj, pat);
    if (!p) return defv;
    p += strlen(pat);
    p = SkipWs(p);
    if (*p != ':') return defv;
    return atof(SkipWs(p + 1));
}
static int JsonObjGetBool(const char *obj, const char *key, int defv) {
    char pat[64];
    _snprintf(pat, 64, "\"%s\"", key); pat[63] = 0;
    const char *p = strstr(obj, pat);
    if (!p) return defv;
    p += strlen(pat);
    p = SkipWs(p);
    if (*p != ':') return defv;
    p = SkipWs(p + 1);
    if (p[0] == 't') return 1;
    if (p[0] == 'f') return 0;
    return defv;
}
/* 提取平衡的 {...} 对象文本（跳过字符串内的花括号） */
static const char *ExtractObject(const char *p, char *out, int outmax) {
    int depth = 0, i = 0, inStr = 0;
    while (*p) {
        char c = *p;
        if (inStr) {
            if (c == '\\' && p[1]) {
                if (i < outmax - 2) { out[i++] = c; out[i++] = p[1]; }
                p += 2; continue;
            }
            if (c == '"') inStr = 0;
        } else {
            if (c == '"') inStr = 1;
            else if (c == '{') depth++;
            else if (c == '}') {
                depth--;
                if (depth == 0) { out[i++] = '}'; out[i] = 0; return p + 1; }
            }
        }
        if (i < outmax - 1) out[i++] = c;
        p++;
    }
    out[i] = 0;
    return p;
}
/* 规范化路径：把连续多个反斜杠压缩为一个（保留开头的 UNC 双反斜杠），
 * 防止配置文件里被双重转义的坏路径导致启动失败 */
static void NormalizePath(WCHAR *path) {
    WCHAR out[520];
    int i = 0, o = 0, n = lstrlenW(path);
    if (n >= 2 && path[0] == L'\\' && path[1] == L'\\') {
        out[o++] = L'\\'; out[o++] = L'\\';
        i = 2;
    }
    while (i < n && o < 519) {
        if (path[i] == L'\\') {
            out[o++] = L'\\';
            while (i < n && path[i] == L'\\') i++;
        } else {
            out[o++] = path[i++];
        }
    }
    out[o] = 0;
    lstrcpynW(path, out, 520);
}
static void LoadConfig(void) {
    WCHAR cfg[1100];
    GetConfigPath(cfg, 1100);
    FILE *f = _wfopen(cfg, L"rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 4 * 1024 * 1024) { fclose(f); return; }
    char *buf = (char *)malloc(sz + 1);
    if (!buf) { fclose(f); return; }
    if (fread(buf, 1, sz, f) != (size_t)sz) { free(buf); fclose(f); return; }
    buf[sz] = 0;
    fclose(f);
    g_count = 0;
    const char *p = strstr(buf, "\"programs\"");
    if (p) {
        p = strchr(p + 10, '[');
        if (p) {
            p++;
            char obj[8192];
            while (g_count < MAX_PROGS) {
                p = SkipWs(p);
                if (*p != '{') break;
                p = ExtractObject(p, obj, 8192);
                ProgEntry *e = &g_progs[g_count];
                memset(e, 0, sizeof(ProgEntry));
                e->enabled  = JsonObjGetBool(obj, "enabled", 1);
                JsonObjGetStr(obj, "name", e->name, 256);
                JsonObjGetStr(obj, "path", e->path, 520);
                NormalizePath(e->path);
                JsonObjGetStr(obj, "args", e->args, 512);
                e->delay = JsonObjGetNum(obj, "delay", 1.0);
                if (e->delay < 0 || e->delay > 3600) e->delay = 1.0;
                e->hideMode = (int)JsonObjGetNum(obj, "hide", HM_TASKBAR);
                if (e->hideMode < 0 || e->hideMode > 2) e->hideMode = HM_TASKBAR;
                e->status = ST_NOTSTART;
                g_count++;
                p = SkipWs(p);
                if (*p == ',') p++;
            }
        }
    }
    free(buf);
}
/* ---------------- 程序名称提取 ---------------- */
/* 从 exe 的版本资源中读取 FileDescription（友好名称），
 * 取不到时回退到文件名（去掉 .exe）。返回 1 表示拿到了版本描述。 */
static int GetFriendlyName(const WCHAR *path, WCHAR *out, int outmax) {
    out[0] = 0;
    DWORD dummy = 0;
    DWORD sz = GetFileVersionInfoSizeW(path, &dummy);
    if (sz > 0 && sz < 8 * 1024 * 1024) {
        void *buf = malloc(sz);
        if (buf) {
            if (GetFileVersionInfoW(path, 0, sz, buf)) {
                /* 优先简体中文，其次英文 */
                static const WCHAR *langs[] = {
                    L"\\StringFileInfo\\080404B0\\FileDescription",
                    L"\\StringFileInfo\\080404b0\\FileDescription",
                    L"\\StringFileInfo\\040904B0\\FileDescription",
                    L"\\StringFileInfo\\040904b0\\FileDescription",
                    L"\\StringFileInfo\\000004B0\\FileDescription"
                };
                void *val = NULL;
                UINT vlen = 0;
                for (int i = 0; i < 5; i++) {
                    val = NULL; vlen = 0;
                    if (VerQueryValueW(buf, (WCHAR *)langs[i], &val, &vlen) && val && vlen > 0) {
                        WCHAR *w = (WCHAR *)val;
                        int n = 0;
                        while (n < (int)vlen && w[n] && n < outmax - 1) {
                            out[n] = w[n];
                            n++;
                        }
                        out[n] = 0;
                        /* 去掉首尾空白 */
                        while (out[0] == L' ' || out[0] == L'\t') {
                            int k = 0;
                            while (out[k]) { out[k] = out[k + 1]; k++; }
                        }
                        int L = lstrlenW(out);
                        while (L > 0 && (out[L - 1] == L' ' || out[L - 1] == L'\t')) out[--L] = 0;
                        if (out[0]) { free(buf); return 1; }
                        break;
                    }
                }
            }
            free(buf);
        }
    }
    /* 回退：文件名去扩展名 */
    const WCHAR *base = path;
    for (const WCHAR *q = path; *q; q++)
        if (*q == L'\\' || *q == L'/') base = q + 1;
    lstrcpynW(out, base, outmax);
    int n = lstrlenW(out);
    if (n > 4 && lstrcmpiW(out + n - 4, L".exe") == 0) out[n - 4] = 0;
    return 0;
}
/* ---------------- 本机磁盘 exe 搜索 ---------------- */
static int FileExists(const WCHAR *path) {
    DWORD a = GetFileAttributesW(path);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
static int IsExe(const WCHAR *path) {
    int n = lstrlenW(path);
    return n > 4 && lstrcmpiW(path + n - 4, L".exe") == 0;
}
static WCHAR LowerW(WCHAR c) {
    return (c >= L'A' && c <= L'Z') ? (WCHAR)(c + 32) : c;
}
/* 不区分大小写的关键词匹配（部分匹配） */
static int StrIStr(const WCHAR *hay, const WCHAR *needle) {
    if (!needle[0]) return 1;
    for (int i = 0; hay[i]; i++) {
        int j = 0;
        while (needle[j] && hay[i + j] && LowerW(hay[i + j]) == LowerW(needle[j])) j++;
        if (!needle[j]) return 1;
    }
    return 0;
}
static void SearchAddResult(const WCHAR *name, const WCHAR *path) {
    if (g_searchStop) return;
    EnterCriticalSection(&g_cs);
    /* 去重：同一 exe 可能通过多个快捷方式/目录被找到 */
    for (int i = 0; i < g_resultCount; i++) {
        if (lstrcmpiW(g_results[i].path, path) == 0) {
            LeaveCriticalSection(&g_cs);
            return;
        }
    }
    if (g_resultCount < MAX_SEARCH) {
        SearchResult *r = &g_results[g_resultCount++];
        lstrcpynW(r->name, name, 256);
        lstrcpynW(r->path, path, 520);
    }
    int n = g_resultCount;
    LeaveCriticalSection(&g_cs);
    if (n % 20 == 0 && g_hDlg)
        PostMessageW(g_hDlg, WM_APP_SEARCH_PROGRESS, (WPARAM)n, 0);
}
static void SearchDir(const WCHAR *dir, int depth) {
    if (g_searchStop || depth > 8 || g_resultCount >= MAX_SEARCH) return;
    WCHAR pat[1100];
    _snwprintf(pat, 1100, L"%s\\*", dir); pat[1099] = 0;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    static const WCHAR *skipDirs[] = {
        L"$Recycle.Bin", L"System Volume Information", L"WinSxS",
        L"WindowsApps", L"node_modules", L"$WINDOWS.~BT", L"Recovery"
    };
    do {
        if (g_searchStop || g_resultCount >= MAX_SEARCH) break;
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            int skipIt = 0;
            for (int k = 0; k < 7; k++)
                if (lstrcmpiW(fd.cFileName, skipDirs[k]) == 0) { skipIt = 1; break; }
            if (skipIt) continue;
            WCHAR sub[1100];
            _snwprintf(sub, 1100, L"%s\\%s", dir, fd.cFileName); sub[1099] = 0;
            SearchDir(sub, depth + 1);
        } else {
            int n = lstrlenW(fd.cFileName);
            if (n > 4 && lstrcmpiW(fd.cFileName + n - 4, L".exe") == 0) {
                if (StrIStr(fd.cFileName, g_keyword)) {
                    WCHAR full[1100], nm[256];
                    _snwprintf(full, 1100, L"%s\\%s", dir, fd.cFileName); full[1099] = 0;
                    /* 自动提取程序名称：优先版本信息 FileDescription，回退文件名 */
                    if (!GetFriendlyName(full, nm, 256)) {
                        lstrcpynW(nm, fd.cFileName, 256);
                        nm[n - 4] = 0;
                    }
                    SearchAddResult(nm, full);
                }
            }
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}
static DWORD __stdcall SearchThreadProc(void *param) {
    HWND hwnd = (HWND)param;
    DWORD drives = GetLogicalDrives();
    for (int i = 0; i < 26 && !g_searchStop; i++) {
        if (!(drives & (1u << i))) continue;
        WCHAR root[4];
        root[0] = (WCHAR)(L'A' + i); root[1] = L':'; root[2] = L'\\'; root[3] = 0;
        UINT t = GetDriveTypeW(root);
        if (t != DRIVE_FIXED && t != DRIVE_REMOVABLE) continue;
        SearchDir(root, 0);
    }
    PostMessageW(hwnd, WM_APP_SEARCH_DONE, 0, 0);
    return 0;
}
/* ---------------- 自动查找已安装程序（打开弹窗时自动执行） ---------------- */
#ifndef SUCCEEDED
#define SUCCEEDED(hr) ((long)(hr) >= 0)
#endif
#ifndef FAILED
#define FAILED(hr) ((long)(hr) < 0)
#endif
#ifndef CSIDL_STARTMENU
#define CSIDL_STARTMENU        0x000b
#endif
#ifndef CSIDL_COMMON_STARTMENU
#define CSIDL_COMMON_STARTMENU 0x0016
#endif
/* 解析 .lnk 快捷方式，取出目标 exe 路径 */
static int ResolveLnk(const WCHAR *lnkPath, WCHAR *out, int outmax) {
    ShellLinkObj *psl = NULL;
    if (FAILED(CoCreateInstance(&g_CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                                &g_IID_IShellLinkW, (void **)&psl)))
        return 0;
    int ok = 0;
    PersistFileObj *ppf = NULL;
    if (SUCCEEDED(psl->lpVtbl->QueryInterface(psl, &g_IID_IPersistFile, (void **)&ppf))) {
        if (SUCCEEDED(ppf->lpVtbl->Load(ppf, lnkPath, 0))) {
            if (SUCCEEDED(psl->lpVtbl->GetPath(psl, out, outmax, NULL, 0)) && out[0])
                ok = 1;
        }
        ppf->lpVtbl->Release(ppf);
    }
    psl->lpVtbl->Release(psl);
    return ok;
}
static int AutoSkipDir(const WCHAR *name) {
    static const WCHAR *skip[] = {
        L"$Recycle.Bin", L"System Volume Information", L"WinSxS",
        L"WindowsApps", L"node_modules", L"$WINDOWS.~BT", L"Recovery",
        L"Common Files", L"Startup", L"Windows", L"Temp"
    };
    for (int k = 0; k < 11; k++)
        if (lstrcmpiW(name, skip[k]) == 0) return 1;
    return 0;
}
/* 安装器/卸载器/更新器之类的 exe，自动查找时跳过 */
static int AutoSkipExe(const WCHAR *file) {
    static const WCHAR *bad[] = {
        L"unins", L"uninst", L"setup", L"install", L"update",
        L"updater", L"redist", L"crash", L"report", L"helper"
    };
    for (int k = 0; k < 10; k++)
        if (StrIStr(file, bad[k])) return 1;
    return 0;
}
/* 收集开始菜单快捷方式对应的 exe */
static void AutoFindLnkDir(const WCHAR *dir, int depth) {
    if (g_searchStop || depth > 6 || g_resultCount >= MAX_SEARCH) return;
    WCHAR pat[1100];
    _snwprintf(pat, 1100, L"%s\\*", dir); pat[1099] = 0;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (g_searchStop || g_resultCount >= MAX_SEARCH) break;
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        WCHAR full[1100];
        _snwprintf(full, 1100, L"%s\\%s", dir, fd.cFileName); full[1099] = 0;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!AutoSkipDir(fd.cFileName)) AutoFindLnkDir(full, depth + 1);
        } else {
            int n = lstrlenW(fd.cFileName);
            if (n > 4 && lstrcmpiW(fd.cFileName + n - 4, L".lnk") == 0) {
                WCHAR target[1100];
                if (ResolveLnk(full, target, 1100) && IsExe(target) && FileExists(target)
                    && !AutoSkipExe(fd.cFileName)) {
                    WCHAR nm[256];
                    if (!GetFriendlyName(target, nm, 256)) {
                        lstrcpynW(nm, fd.cFileName, 256);
                        nm[n - 4] = 0;
                    }
                    SearchAddResult(nm, target);
                }
            }
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}
/* 扫描常见安装目录下的 exe（浅层，速度快） */
static void AutoFindDir(const WCHAR *dir, int depth) {
    if (g_searchStop || depth > 3 || g_resultCount >= MAX_SEARCH) return;
    WCHAR pat[1100];
    _snwprintf(pat, 1100, L"%s\\*", dir); pat[1099] = 0;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (g_searchStop || g_resultCount >= MAX_SEARCH) break;
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        WCHAR full[1100];
        _snwprintf(full, 1100, L"%s\\%s", dir, fd.cFileName); full[1099] = 0;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!AutoSkipDir(fd.cFileName)) AutoFindDir(full, depth + 1);
        } else {
            int n = lstrlenW(fd.cFileName);
            if (n > 4 && lstrcmpiW(fd.cFileName + n - 4, L".exe") == 0
                && !AutoSkipExe(fd.cFileName)) {
                WCHAR nm[256];
                if (!GetFriendlyName(full, nm, 256)) {
                    lstrcpynW(nm, fd.cFileName, 256);
                    nm[n - 4] = 0;
                }
                SearchAddResult(nm, full);
            }
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}
static DWORD __stdcall AutoFindThreadProc(void *param) {
    HWND hwnd = (HWND)param;
    CoInitialize(NULL);
    WCHAR path[1024];
    /* 1. 开始菜单快捷方式（用户级 + 全局级） */
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_STARTMENU, NULL, 0, path)))
        AutoFindLnkDir(path, 0);
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_COMMON_STARTMENU, NULL, 0, path)))
        AutoFindLnkDir(path, 0);
    /* 2. 常见安装目录 */
    AutoFindDir(L"C:\\Program Files", 0);
    AutoFindDir(L"C:\\Program Files (x86)", 0);
    CoUninitialize();
    PostMessageW(hwnd, WM_APP_SEARCH_DONE, 0, 0);
    return 0;
}
static void AutoFindStart(HWND hwnd) {
    if (g_searchThread) {
        g_searchStop = 1;
        WaitForSingleObject(g_searchThread, 3000);
        CloseHandle(g_searchThread);
        g_searchThread = NULL;
    }
    EnterCriticalSection(&g_cs);
    g_resultCount = 0;
    g_allCount = 0;
    g_searchStop = 0;
    g_autoFind = 1;
    LeaveCriticalSection(&g_cs);
    HANDLE th = CreateThread(NULL, 0, AutoFindThreadProc, hwnd, 0, NULL);
    if (th) g_searchThread = th;
}
static void SearchStart(HWND hwnd, const WCHAR *keyword) {
    /* 若上一次搜索还在进行，先停止 */
    if (g_searchThread) {
        g_searchStop = 1;
        WaitForSingleObject(g_searchThread, 3000);
        CloseHandle(g_searchThread);
        g_searchThread = NULL;
    }
    EnterCriticalSection(&g_cs);
    g_resultCount = 0;
    lstrcpynW(g_keyword, keyword, 128);
    g_searchStop = 0;
    LeaveCriticalSection(&g_cs);
    HANDLE th = CreateThread(NULL, 0, SearchThreadProc, hwnd, 0, NULL);
    if (th) g_searchThread = th;
}
/* ---------------- 列表 UI ---------------- */
static void SetProgStatus(int idx, int st) {
    g_progs[idx].status = st;
    LvSetItemText(g_hList, idx, COL_STATE, (WCHAR *)g_stateNames[st]);
}
static void RefreshListItems(void) {
    g_suppressNotify++;
    LvDelAll(g_hList);
    for (int i = 0; i < g_count; i++) {
        ProgEntry *p = &g_progs[i];
        LVITEMW it;
        memset(&it, 0, sizeof(it));
        it.mask = LVIF_TEXT;
        it.iItem = i;
        it.pszText = p->name;
        LvInsItem(g_hList, &it);
        LvSetItemText(g_hList, i, COL_PATH, p->path);
        LvSetItemText(g_hList, i, COL_ARGS, p->args);
        WCHAR tmp[32];
        _snwprintf(tmp, 32, L"%g", p->delay); tmp[31] = 0;
        LvSetItemText(g_hList, i, COL_DELAY, tmp);
        LvSetItemText(g_hList, i, COL_HIDE, (WCHAR *)g_hideNames[p->hideMode]);
        LvSetItemText(g_hList, i, COL_STATE, (WCHAR *)g_stateNames[p->status]);
        LvSetItemState(g_hList, i, INDEXTOSTATEIMAGEMASK(p->enabled ? 2 : 1), LVIS_STATEIMAGEMASK);
    }
    g_suppressNotify--;
}
static void AddProgramEntry(const WCHAR *name, const WCHAR *path) {
    if (g_count >= MAX_PROGS) { LogLine(L"列表已满（最多 128 个）"); return; }
    for (int i = 0; i < g_count; i++) {
        if (lstrcmpiW(g_progs[i].path, path) == 0) {
            LogLine2(L"已存在，跳过：%s", name);
            return;
        }
    }
    ProgEntry *p = &g_progs[g_count];
    memset(p, 0, sizeof(ProgEntry));
    p->enabled = 1;
    lstrcpynW(p->name, name, 256);
    lstrcpynW(p->path, path, 520);
    p->delay = 1.0;
    p->hideMode = HM_TASKBAR;
    p->status = ST_NOTSTART;
    g_count++;
    int i = g_count - 1;
    g_suppressNotify++;
    LVITEMW it;
    memset(&it, 0, sizeof(it));
    it.mask = LVIF_TEXT;
    it.iItem = i;
    it.pszText = p->name;
    LvInsItem(g_hList, &it);
    LvSetItemText(g_hList, i, COL_PATH, p->path);
    LvSetItemText(g_hList, i, COL_ARGS, p->args);
    WCHAR tmp[32];
    _snwprintf(tmp, 32, L"%g", p->delay); tmp[31] = 0;
    LvSetItemText(g_hList, i, COL_DELAY, tmp);
    LvSetItemText(g_hList, i, COL_HIDE, (WCHAR *)g_hideNames[p->hideMode]);
    LvSetItemText(g_hList, i, COL_STATE, (WCHAR *)g_stateNames[p->status]);
    LvSetItemState(g_hList, i, INDEXTOSTATEIMAGEMASK(2), LVIS_STATEIMAGEMASK);
    g_suppressNotify--;
    LogLine2(L"已添加：%s", name);
}
/* ---------------- 进程管理 ---------------- */
static void StartProgram(int idx) {
    ProgEntry *p = &g_progs[idx];
    if (p->hProcess) {
        if (WaitForSingleObject(p->hProcess, 0) != WAIT_OBJECT_0) {
            /* 已在运行：重新开启隐藏（用于取消勾选后又勾选的场景） */
            LogLine2(L"已在运行，重新隐藏窗口：%s", p->name);
            p->hideAttempts = 12;
            p->trayNoteDone = 0;
            return;
        }
        CloseHandle(p->hProcess);
        p->hProcess = NULL;
        p->pid = 0;
    }
    if (!FileExists(p->path)) {
        SetProgStatus(idx, ST_FAILED);
        LogLine2(L"启动失败（文件不存在）：%s", p->path);
        return;
    }
    WCHAR cmd[1600];
    _snwprintf(cmd, 1600, L"\"%s\" %s", p->path, p->args);
    cmd[1599] = 0;
    WCHAR workdir[1024];
    lstrcpynW(workdir, p->path, 1024);
    WCHAR *sl = wcsrchr(workdir, L'\\');
    if (sl) *sl = 0; else workdir[0] = 0;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));
    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 0, NULL,
                        workdir[0] ? workdir : NULL, &si, &pi)) {
        SetProgStatus(idx, ST_FAILED);
        LogLine2(L"启动失败（错误码 %d）：%s", (int)GetLastError(), p->name);
        return;
    }
    CloseHandle(pi.hThread);
    p->hProcess = pi.hProcess;
    p->pid = pi.dwProcessId;
    p->hideAttempts = 12;   /* 约 6 秒内持续尝试隐藏 */
    p->trayNoteDone = 0;
    SetProgStatus(idx, ST_RUNNING);
    LogLine2(L"启动成功：%s", p->name);
}
/* 取消勾选：恢复该程序被隐藏的窗口（不结束进程） */
static void UnhideProgram(int idx) {
    ProgEntry *p = &g_progs[idx];
    if (!p->hProcess || !p->pid) {
        SetProgStatus(idx, ST_NOTSTART);
        return;
    }
    g_hidePid = p->pid;
    g_hideFound = 0;
    EnumWindows(UnhideEnumProc, 0);
    p->hideAttempts = 0;   /* 停止后续自动隐藏 */
    SetProgStatus(idx, ST_RUNNING);
    if (g_hideFound)
        LogLine2(L"已恢复显示窗口（%d 个）：%s", g_hideFound, p->name);
    else
        LogLine2(L"该程序当前无可恢复窗口（进程保留）：%s", p->name);
}
static void DeleteSelected(void) {
    int n = 0;
    for (int i = g_count - 1; i >= 0; i--) {
        if (LvGetItemState(g_hList, i, LVIS_SELECTED) & LVIS_SELECTED) {
            if (g_progs[i].hProcess) {
                TerminateProcess(g_progs[i].hProcess, 1);
                CloseHandle(g_progs[i].hProcess);
                g_progs[i].hProcess = NULL;
                g_progs[i].pid = 0;
            }
            for (int j = i; j < g_count - 1; j++) g_progs[j] = g_progs[j + 1];
            g_count--;
            n++;
        }
    }
    if (n) {
        RefreshListItems();
        SaveConfig();   /* 删除后立即同步落盘，避免配置与界面不一致 */
        LogLine2(L"已删除 %d 个程序，配置已同步保存", n);
    } else {
        LogLine(L"请先单击选中要删除的程序行（可按住 Ctrl 多选）");
    }
}
static void KillAllChildren(void) {
    for (int i = 0; i < g_count; i++) {
        ProgEntry *p = &g_progs[i];
        if (p->hProcess) {
            TerminateProcess(p->hProcess, 1);
            WaitForSingleObject(p->hProcess, 3000);
            CloseHandle(p->hProcess);
            p->hProcess = NULL;
            p->pid = 0;
            SetProgStatus(i, ST_STOPPED);
            LogLine2(L"联动退出，已关闭：%s", p->name);
        }
    }
}
/* ---------------- 隐藏与监听 ---------------- */
static BOOL CALLBACK UnhideEnumProc(HWND hwnd, LPARAM lp) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != g_hidePid) return TRUE;
    /* 恢复任务栏显示：去掉 WS_EX_TOOLWINDOW */
    LONG ex = GetWindowLongW(hwnd, GWL_EXSTYLE);
    SetWindowLongW(hwnd, GWL_EXSTYLE, ex & ~WS_EX_TOOLWINDOW);
    ShowWindow(hwnd, SW_SHOW);
    ShowWindow(hwnd, SW_RESTORE);
    g_hideFound++;
    return TRUE;
}
static BOOL CALLBACK HideEnumProc(HWND hwnd, LPARAM lp) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != g_hidePid) return TRUE;
    if (!IsWindowVisible(hwnd)) return TRUE;
    int mode = (int)lp;
    if (mode == HM_TASKBAR) {
        LONG ex = GetWindowLongW(hwnd, GWL_EXSTYLE);
        SetWindowLongW(hwnd, GWL_EXSTYLE, ex | WS_EX_TOOLWINDOW);
    }
    ShowWindow(hwnd, SW_HIDE);
    g_hideFound++;
    return TRUE;
}
static void DoMonitor(void) {
    for (int i = 0; i < g_count; i++) {
        ProgEntry *p = &g_progs[i];
        if (!p->hProcess) continue;
        if (WaitForSingleObject(p->hProcess, 0) == WAIT_OBJECT_0) {
            DWORD code = 0;
            GetExitCodeProcess(p->hProcess, &code);
            CloseHandle(p->hProcess);
            p->hProcess = NULL;
            p->pid = 0;
            SetProgStatus(i, ST_STOPPED);
            LogLine2(L"已停止（退出码 %u）：%s", (unsigned)code, p->name);
        } else if ((p->hideMode == HM_WINDOW || p->hideMode == HM_TASKBAR)
                   && p->status == ST_RUNNING && p->hideAttempts > 0) {
            g_hidePid = p->pid;
            g_hideFound = 0;
            EnumWindows(HideEnumProc, (LPARAM)p->hideMode);
            p->hideAttempts--;
            if (g_hideFound) {
                SetProgStatus(i, ST_HIDDEN);
                LogLine2(L"已隐藏窗口（%d 个）：%s", g_hideFound, p->name);
                if (!p->trayNoteDone) {
                    p->trayNoteDone = 1;
                    LogLine2(L"托盘图标隐藏为尽力操作，无法隐藏时自动跳过：%s", p->name);
                }
            }
        }
    }
}
static void StartAllEnabled(void) {
    g_queueActive = TRUE;
    g_queueIndex = 0;
    g_queueNextTick = GetTickCount() + 300;
    LogLine(L"开始依次启动所有启用的子程序...");
}
static void DoQueue(void) {
    if (!g_queueActive) return;
    if (GetTickCount() < g_queueNextTick) return;
    while (g_queueIndex < g_count && !g_progs[g_queueIndex].enabled) g_queueIndex++;
    if (g_queueIndex >= g_count) {
        g_queueActive = FALSE;
        LogLine(L"启动队列处理完毕");
        return;
    }
    StartProgram(g_queueIndex);
    double d = g_progs[g_queueIndex].delay;
    if (d <= 0) d = 0.5;
    g_queueNextTick = GetTickCount() + (DWORD)(d * 1000);
    g_queueIndex++;
}
/* ---------------- 开机自启 ---------------- */
static int IsAutoRunOn(void) {
    HKEY h;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_READ, &h) != ERROR_SUCCESS) return 0;
    DWORD type, size = 0;
    int on = RegQueryValueExW(h, APP_AUTORUN, NULL, &type, NULL, &size) == ERROR_SUCCESS;
    RegCloseKey(h);
    return on;
}
static void SetAutoRun(int on) {
    HKEY h;
    if (RegCreateKeyExW(HKEY_CURRENT_USER,
                        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                        0, NULL, 0, KEY_SET_VALUE, NULL, &h, NULL) != ERROR_SUCCESS) {
        LogLine(L"开机自启设置失败（注册表写入被拒绝）");
        return;
    }
    if (on) {
        WCHAR exe[1024], val[1100];
        GetModuleFileNameW(NULL, exe, 1024);
        _snwprintf(val, 1100, L"\"%s\" /tray", exe);
        val[1099] = 0;
        RegSetValueExW(h, APP_AUTORUN, 0, REG_SZ,
                       (const BYTE *)val, (DWORD)((lstrlenW(val) + 1) * sizeof(WCHAR)));
        LogLine(L"已开启开机自启");
    } else {
        RegDeleteValueW(h, APP_AUTORUN);
        LogLine(L"已关闭开机自启");
    }
    RegCloseKey(h);
}
/* ---------------- 托盘 ---------------- */
static void AddTray(HWND hwnd) {
    memset(&g_nid, 0, sizeof(g_nid));
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY;
    /* 托盘图标与 exe 图标同源：优先取内嵌资源的小尺寸版本 */
    g_nid.hIcon = LoadAppIcon(16, 16);
    if (!g_nid.hIcon) g_nid.hIcon = g_hAppIcon ? g_hAppIcon : LoadIconW(NULL, IDI_APPLICATION);
    lstrcpynW(g_nid.szTip, APP_NAME, 128);
    Shell_NotifyIconW(NIM_ADD, &g_nid);
    g_trayAdded = TRUE;
}
static void ShowMain(void) {
    ShowWindow(g_hMain, SW_SHOW);
    ShowWindow(g_hMain, SW_RESTORE);
    SetForegroundWindow(g_hMain);
}
/* ---------------- 单元格编辑 ---------------- */
static void CommitEdit(void) {
    if (!g_hEdit) return;
    HWND h = g_hEdit;
    g_hEdit = NULL;
    WCHAR txt[512];
    GetWindowTextW(h, txt, 512);
    int item = g_editItem, sub = g_editSub;
    g_editItem = -1;
    g_editSub = -1;
    if (g_oldEditProc) SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)g_oldEditProc);
    DestroyWindow(h);
    if (item < 0 || item >= g_count) return;
    ProgEntry *p = &g_progs[item];
    if (sub == COL_ARGS) {
        lstrcpynW(p->args, txt, 512);
        LvSetItemText(g_hList, item, COL_ARGS, p->args);
    } else if (sub == COL_DELAY) {
        double d = wcstod(txt, NULL);
        if (d < 0 || d > 3600) d = 1.0;
        p->delay = d;
        WCHAR t[32];
        _snwprintf(t, 32, L"%g", d); t[31] = 0;
        LvSetItemText(g_hList, item, COL_DELAY, t);
    } else if (sub == COL_PATH) {
        if (txt[0]) {
            lstrcpynW(p->path, txt, 520);
            LvSetItemText(g_hList, item, COL_PATH, p->path);
        }
    }
}
static LRESULT CALLBACK EditSubProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN) {
        if (wp == VK_RETURN) { CommitEdit(); return 0; }
        if (wp == VK_ESCAPE) {
            g_hEdit = NULL;
            g_editItem = -1;
            g_editSub = -1;
            if (g_oldEditProc) SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)g_oldEditProc);
            DestroyWindow(h);
            return 0;
        }
    }
    if (msg == WM_KILLFOCUS) { CommitEdit(); return 0; }
    return CallWindowProcW(g_oldEditProc, h, msg, wp, lp);
}
static BOOL ListGetSubItemRect(HWND hList, int item, int sub, RECT *rc) {
    rc->left = LVIR_BOUNDS;
    rc->top = item;
    rc->right = sub;
    rc->bottom = 0;
    return (BOOL)SendMessageW(hList, LVM_GETSUBITEMRECT, (WPARAM)item, (LPARAM)rc);
}
static void OnListDblClick(int item, int sub) {
    if (item < 0 || item >= g_count) return;
    ProgEntry *p = &g_progs[item];
    if (sub == COL_HIDE) {
        p->hideMode = (p->hideMode + 2) % 3;   /* 2 -> 1 -> 0 -> 2 循环 */
        LvSetItemText(g_hList, item, COL_HIDE, (WCHAR *)g_hideNames[p->hideMode]);
        LogLine2(L"隐藏方式已切换为「%s」：%s", g_hideNames[p->hideMode], p->name);
        return;
    }
    if (sub == COL_ARGS || sub == COL_DELAY || sub == COL_PATH) {
        RECT rc;
        if (!ListGetSubItemRect(g_hList, item, sub, &rc)) return;
        if (g_hEdit) { g_hEdit = NULL; }
        WCHAR init[512];
        init[0] = 0;
        if (sub == COL_ARGS) lstrcpynW(init, p->args, 512);
        else if (sub == COL_DELAY) {
            WCHAR t[32];
            _snwprintf(t, 32, L"%g", p->delay); t[31] = 0;
            lstrcpynW(init, t, 512);
        } else lstrcpynW(init, p->path, 512);
        g_hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", init,
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
            g_hList, (HMENU)(INT_PTR)IDC_EDIT_INPLACE, g_hInst, NULL);
        SendMessageW(g_hEdit, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(g_hEdit, EM_SETLIMITTEXT, 500, 0);
        SetFocus(g_hEdit);
        g_editItem = item;
        g_editSub = sub;
        g_oldEditProc = (WNDPROC)SetWindowLongPtrW(g_hEdit, GWLP_WNDPROC, (LONG_PTR)EditSubProc);
    }
}
/* ---------------- 添加程序对话框 ---------------- */
/* 提取 exe 文件小图标加入图像列表，返回图标索引 */
static int AddFileIcon(const WCHAR *path) {
    if (!g_hImgList) return -1;
    MY_SHFILEINFOW sfi;
    memset(&sfi, 0, sizeof(sfi));
    if (SHGetFileInfoW(path, 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_SMALLICON) && sfi.hIcon) {
        int idx = ImageList_ReplaceIcon(g_hImgList, -1, sfi.hIcon);
        DestroyIcon(sfi.hIcon);
        return idx;
    }
    return -1;
}
/* 本地实时过滤：从完整结果中筛出匹配关键词的行并刷新列表（不重新扫盘） */
static void ApplyLocalFilter(const WCHAR *kw) {
    if (!g_hDlgList) return;
    LvDelAll(g_hDlgList);
    EnterCriticalSection(&g_cs);
    int shown = 0;
    for (int i = 0; i < g_allCount && shown < MAX_SEARCH; i++) {
        if (kw && kw[0] && !StrIStr(g_allResults[i].name, kw)
            && !StrIStr(g_allResults[i].path, kw)) continue;
        /* 同步到 g_results，保证选中索引与显示行一致 */
        g_results[shown].name[0] = 0;
        g_results[shown].path[0] = 0;
        lstrcpynW(g_results[shown].name, g_allResults[i].name, 256);
        lstrcpynW(g_results[shown].path, g_allResults[i].path, 520);
        LVITEMW it;
        memset(&it, 0, sizeof(it));
        it.mask = LVIF_TEXT | LVIF_IMAGE;
        it.iItem = shown;
        it.pszText = g_results[shown].name;
        it.iImage = AddFileIcon(g_results[shown].path);
        LvInsItem(g_hDlgList, &it);
        LvSetItemText(g_hDlgList, shown, 1, g_results[shown].path);
        shown++;
    }
    g_resultCount = shown;
    LeaveCriticalSection(&g_cs);
    WCHAR title[192];
    _snwprintf(title, 192, L"筛选结果：%d / %d 个（双击行或选中后点【添加所选】）",
               shown, g_allCount);
    title[191] = 0;
    SetWindowTextW(g_hDlg, title);
}
/* 把搜索结果（带图标）填充到列表 */
static void FillSearchResults(void) {
    /* 先把本次扫描的完整结果备份到 g_allResults，供实时过滤使用 */
    EnterCriticalSection(&g_cs);
    g_allCount = g_resultCount;
    if (g_allCount > MAX_SEARCH) g_allCount = MAX_SEARCH;
    for (int i = 0; i < g_allCount; i++) {
        lstrcpynW(g_allResults[i].name, g_results[i].name, 256);
        lstrcpynW(g_allResults[i].path, g_results[i].path, 520);
    }
    LeaveCriticalSection(&g_cs);
    /* 按当前搜索框内容做一次过滤展示 */
    WCHAR kw[128];
    kw[0] = 0;
    if (g_hDlgSearch) GetWindowTextW(g_hDlgSearch, kw, 128);
    ApplyLocalFilter(kw);
}
/* 触发磁盘搜索 */
static void DoSearch(HWND hwnd) {
    WCHAR f[128];
    GetWindowTextW(GetDlgItem(hwnd, IDC_DLG_SEARCH), f, 128);
    if (!f[0]) {
        MessageBoxW(hwnd, L"请先输入软件名称关键词（支持部分名称，如输入 note 可找到 notepad）",
                    APP_NAME, MB_OK | MB_ICONINFORMATION);
        return;
    }
    LogLine2(L"[调试] DoSearch 开始，hwnd=0x%X g_hDlg=0x%X", (unsigned)(INT_PTR)hwnd, (unsigned)(INT_PTR)g_hDlg);
    SetWindowTextW(hwnd, L"正在搜索本机磁盘，请稍候...");
    LvDelAll(g_hDlgList);
    EnterCriticalSection(&g_cs);
    g_allCount = 0;
    g_resultCount = 0;
    LeaveCriticalSection(&g_cs);
    g_autoFind = 0;
    SearchStart(hwnd, f);
}
static void AddDlgApply(void) {
    int n = 0;
    EnterCriticalSection(&g_cs);
    for (int i = 0; i < g_resultCount; i++) {
        if (LvGetItemState(g_hDlgList, i, LVIS_SELECTED) & LVIS_SELECTED) {
            AddProgramEntry(g_results[i].name, g_results[i].path);
            n++;
        }
    }
    LeaveCriticalSection(&g_cs);
    if (n) {
        /* 添加后立即落盘，保证列表与配置文件一致 */
        SaveConfig();
        MessageBoxW(g_hDlg, L"已添加到主列表并保存配置", APP_NAME, MB_OK | MB_ICONINFORMATION);
    } else {
        MessageBoxW(g_hDlg, L"请先在列表中选中要添加的软件", APP_NAME, MB_OK | MB_ICONINFORMATION);
    }
}
/* 搜索框子类化：回车触发扫盘；输入时若已有结果则本地实时过滤 */
static LRESULT CALLBACK DlgSearchProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN && wp == VK_RETURN) {
        DoSearch(GetParent(h));
        return 0;
    }
    if (msg == WM_CHAR && wp >= 32) {
        LRESULT r = CallWindowProcW(g_oldDlgSearchProc, h, msg, wp, lp);
        if (g_allCount > 0) {
            WCHAR kw[128];
            GetWindowTextW(h, kw, 128);
            ApplyLocalFilter(kw);
        }
        return r;
    }
    if (msg == WM_KEYDOWN && wp == VK_BACK) {
        LRESULT r = CallWindowProcW(g_oldDlgSearchProc, h, msg, wp, lp);
        if (g_allCount > 0) {
            WCHAR kw[128];
            GetWindowTextW(h, kw, 128);
            ApplyLocalFilter(kw);
        }
        return r;
    }
    return CallWindowProcW(g_oldDlgSearchProc, h, msg, wp, lp);
}
static LRESULT CALLBACK AddDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_hDlgSearch = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            10, 10, 400, 26, hwnd, (HMENU)(INT_PTR)IDC_DLG_SEARCH, g_hInst, NULL);
        g_oldDlgSearchProc = (WNDPROC)SetWindowLongPtrW(g_hDlgSearch, GWLP_WNDPROC, (LONG_PTR)DlgSearchProc);
        CreateWindowExW(0, L"BUTTON", L"搜索",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            418, 10, 80, 26, hwnd, (HMENU)(INT_PTR)IDC_DLG_BTN_SEARCH, g_hInst, NULL);
        g_hDlgList = CreateWindowExW(0, L"SysListView32", L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SHOWSELALWAYS,
            10, 44, 600, 380, hwnd, (HMENU)(INT_PTR)IDC_DLG_LIST, g_hInst, NULL);
        SendMessageW(g_hDlgList, LVM_SETEXTENDEDLISTVIEWSTYLE,
                     LVS_EX_FULLROWSELECT, LVS_EX_FULLROWSELECT);
        g_hImgList = ImageList_Create(16, 16, ILC_COLOR32 | ILC_MASK, 64, 64);
        if (g_hImgList)
            SendMessageW(g_hDlgList, LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)g_hImgList);
        LVCOLUMNW col;
        memset(&col, 0, sizeof(col));
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        col.pszText = (WCHAR *)L"软件名称"; col.cx = 200; col.iSubItem = 0;
        LvInsColumn(g_hDlgList, 0, &col);
        col.pszText = (WCHAR *)L"程序路径"; col.cx = 380; col.iSubItem = 1;
        LvInsColumn(g_hDlgList, 1, &col);
        CreateWindowExW(0, L"BUTTON", L"添加所选",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            10, 434, 100, 30, hwnd, (HMENU)(INT_PTR)IDC_DLG_ADD, g_hInst, NULL);
        CreateWindowExW(0, L"BUTTON", L"取消",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            120, 434, 100, 30, hwnd, (HMENU)(INT_PTR)IDC_DLG_CANCEL, g_hInst, NULL);
        SetWindowTextW(hwnd, L"添加程序：打开时已自动查找本机程序；也可输入名称过滤，或点【搜索】全盘查找");
        EnumChildWindows(hwnd, SetChildFont, 0);
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp), code = HIWORD(wp);
        if (id == IDC_DLG_ADD && code == BN_CLICKED) AddDlgApply();
        else if (id == IDC_DLG_CANCEL && code == BN_CLICKED) DestroyWindow(hwnd);
        else if (id == IDC_DLG_BTN_SEARCH && code == BN_CLICKED) DoSearch(hwnd);
        else if (id == IDC_DLG_SEARCH && code == EN_CHANGE) {
            /* 输入实时过滤（兼容中文输入法）；自动查找/搜索进行中不响应 */
            if (g_allCount > 0 && !g_searchThread) {
                WCHAR kw[128];
                GetWindowTextW(g_hDlgSearch, kw, 128);
                ApplyLocalFilter(kw);
            }
        }
        return 0;
    }
    case WM_NOTIFY: {
        NMHDR *nm = (NMHDR *)lp;
        if (nm->idFrom == IDC_DLG_LIST && nm->code == NM_DBLCLK) {
            NMITEMACTIVATE *ia = (NMITEMACTIVATE *)lp;
            if (ia->iItem >= 0) {
                EnterCriticalSection(&g_cs);
                if (ia->iItem < g_resultCount)
                    AddProgramEntry(g_results[ia->iItem].name, g_results[ia->iItem].path);
                LeaveCriticalSection(&g_cs);
            }
        }
        return 0;
    }
    case WM_APP_SEARCH_PROGRESS: {
        WCHAR title[128];
        wsprintfW(title, L"正在搜索本机磁盘... 已找到 %d 个", (int)wp);
        SetWindowTextW(hwnd, title);
        return 0;
    }
    case WM_APP_SEARCH_DONE: {
        FillSearchResults();
        int n = 0;
        EnterCriticalSection(&g_cs); n = g_resultCount; LeaveCriticalSection(&g_cs);
        WCHAR title[160];
        if (g_autoFind)
            wsprintfW(title, L"已自动找到 %d 个本机程序：双击行添加，或在上方输入名称实时过滤", n);
        else
            wsprintfW(title, L"搜索完成（共找到 %d 个 exe，双击行或选中后点【添加所选】）", n);
        g_autoFind = 0;
        title[159] = 0;
        SetWindowTextW(hwnd, title);
        return 0;
    }
    case WM_CLOSE:
        LogLine(L"[调试] 弹窗收到 WM_CLOSE");
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        LogLine(L"[调试] 弹窗收到 WM_DESTROY");
        if (g_searchThread) {
            g_searchStop = 1;
            WaitForSingleObject(g_searchThread, 3000);
            CloseHandle(g_searchThread);
            g_searchThread = NULL;
        }
        g_hDlg = NULL;
        g_hDlgList = NULL;
        g_hDlgSearch = NULL;
        g_oldDlgSearchProc = NULL;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
static void ShowAddDialog(HWND parent) {
    if (g_hDlg) { SetForegroundWindow(g_hDlg); return; }
    WNDCLASSW wc;
    if (!GetClassInfoW(g_hInst, L"AiPyAddDlgClass", &wc)) {
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc = AddDlgProc;
        wc.hInstance = g_hInst;
        wc.hIcon = g_hAppIcon ? g_hAppIcon : LoadIconW(NULL, IDI_APPLICATION);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"AiPyAddDlgClass";
        RegisterClassW(&wc);
    }
    g_hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"AiPyAddDlgClass", L"正在扫描已安装软件...",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 640, 510, parent, NULL, g_hInst, NULL);
    ShowWindow(g_hDlg, SW_SHOW);
    SetWindowTextW(g_hDlg, L"正在自动查找已安装程序...（稍候可在上方输入名称过滤）");
    AutoFindStart(g_hDlg);
}
/* ---------------- 主窗口 ---------------- */
static BOOL CALLBACK SetChildFont(HWND h, LPARAM lp) {
    SendMessageW(h, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    return TRUE;
}
static void CreateControls(HWND hwnd) {
    g_hList = CreateWindowExW(0, L"SysListView32", L"",
        WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS,
        10, 10, 900, 380, hwnd, (HMENU)(INT_PTR)IDC_LIST, g_hInst, NULL);
    SendMessageW(g_hList, LVM_SETEXTENDEDLISTVIEWSTYLE,
                 LVS_EX_FULLROWSELECT | LVS_EX_CHECKBOXES | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER,
                 LVS_EX_FULLROWSELECT | LVS_EX_CHECKBOXES | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    LVCOLUMNW col;
    memset(&col, 0, sizeof(col));
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    const WCHAR *cols[] = { L"程序名称", L"程序路径", L"启动参数", L"启动延迟(秒)", L"隐藏方式", L"状态" };
    int widths[] = { 140, 320, 130, 90, 140, 80 };
    for (int i = 0; i < 6; i++) {
        col.pszText = (WCHAR *)cols[i];
        col.cx = widths[i];
        col.iSubItem = i;
        LvInsColumn(g_hList, i, &col);
    }
    struct { int id; const WCHAR *txt; } btns[] = {
        { IDC_BTN_ADD,   L"添加程序" },
        { IDC_BTN_DEL,   L"删除选中" },
        { IDC_BTN_SAVE,  L"保存列表" }
    };
    for (int i = 0; i < 3; i++) {
        CreateWindowExW(0, L"BUTTON", btns[i].txt,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)btns[i].id, g_hInst, NULL);
    }
    g_hLog = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
        0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)IDC_LOG, g_hInst, NULL);
    g_hFont = CreateFontW(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH, L"Microsoft YaHei UI");
    EnumChildWindows(hwnd, SetChildFont, 0);
}
static void LayoutControls(HWND hwnd) {
    if (!g_hList || !g_hLog) return;
    RECT rc;
    GetClientRect(hwnd, &rc);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    int margin = 10, btnH = 30, logH = 130;
    MoveWindow(g_hList, margin, margin, w - 2 * margin,
               h - 2 * margin - btnH - 8 - logH - 8, TRUE);
    int y = h - margin - logH - 8 - btnH;
    int x = margin;
    int ids[] = { IDC_BTN_ADD, IDC_BTN_DEL, IDC_BTN_SAVE };
    for (int i = 0; i < 3; i++) {
        MoveWindow(GetDlgItem(hwnd, ids[i]), x, y, 96, btnH, TRUE);
        x += 96 + 8;
    }
    MoveWindow(g_hLog, margin, y + btnH + 8, w - 2 * margin, logH, TRUE);
}
static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        CreateControls(hwnd);
        AddTray(hwnd);
        return 0;
    case WM_SIZE:
        LayoutControls(hwnd);
        return 0;
    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        LogLine(L"窗口已最小化到托盘（程序继续在后台运行）");
        return 0;
    case WM_TRAY:
        if (lp == WM_LBUTTONDBLCLK) {
            ShowMain();
        } else if (lp == WM_RBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(hwnd);
            HMENU m = CreatePopupMenu();
            AppendMenuW(m, MF_STRING, IDM_SHOW, L"显示窗口");
            AppendMenuW(m, MF_SEPARATOR, 0, NULL);
            AppendMenuW(m, MF_STRING | (IsAutoRunOn() ? MF_CHECKED : 0), IDM_AUTORUN, L"开机自启");
            AppendMenuW(m, MF_SEPARATOR, 0, NULL);
            AppendMenuW(m, MF_STRING, IDM_EXIT, L"退出");
            int cmd = TrackPopupMenu(m, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
                                     pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(m);
            PostMessageW(hwnd, WM_NULL, 0, 0);
            if (cmd == IDM_SHOW) ShowMain();
            else if (cmd == IDM_AUTORUN) SetAutoRun(IsAutoRunOn() ? 0 : 1);
            else if (cmd == IDM_EXIT) DestroyWindow(hwnd);
        }
        return 0;
    case WM_COMMAND: {
        int id = LOWORD(wp), code = HIWORD(wp);
        if (id == IDC_BTN_ADD && code == BN_CLICKED) ShowAddDialog(hwnd);
        else if (id == IDC_BTN_DEL && code == BN_CLICKED) DeleteSelected();
        else if (id == IDC_BTN_SAVE && code == BN_CLICKED) SaveConfig();
        return 0;
    }
    case WM_NOTIFY: {
        NMHDR *nm = (NMHDR *)lp;
        if (nm->idFrom == IDC_LIST) {
            if (nm->code == NM_DBLCLK) {
                NMITEMACTIVATE *ia = (NMITEMACTIVATE *)lp;
                OnListDblClick(ia->iItem, ia->iSubItem);
            } else if (nm->code == LVN_ITEMCHANGED) {
                NMLISTVIEW *nml = (NMLISTVIEW *)lp;
                if (g_suppressNotify > 0) return 0;   /* 程序内部刷新，忽略 */
                if ((nml->uChanged & LVIF_STATE) && nml->iItem >= 0 && nml->iItem < g_count) {
                    int chk = LvGetCheck(g_hList, nml->iItem);
                    if (g_progs[nml->iItem].enabled != chk) {
                        g_progs[nml->iItem].enabled = chk;
                        if (chk) {
                            /* 打勾：立即启动并隐藏 */
                            LogLine2(L"已勾选，自动启动并隐藏：%s", g_progs[nml->iItem].name);
                            StartProgram(nml->iItem);
                        } else {
                            /* 取消打勾：停止隐藏（不结束进程） */
                            LogLine2(L"已取消勾选，停止隐藏（进程保留）：%s", g_progs[nml->iItem].name);
                            UnhideProgram(nml->iItem);
                        }
                        /* 勾选状态立即同步落盘 */
                        SaveConfig();
                    }
                }
            }
        }
        return 0;
    }
    case WM_TIMER:
        if (wp == TIMER_ID) {
            g_tick++;
            DoQueue();
            if (g_tick % 4 == 0) DoMonitor();   /* 每 2 秒检查一次进程状态 */
        }
        return 0;
    case WM_ENDSESSION:
        if (wp) {
            KillAllChildren();
            if (g_trayAdded) Shell_NotifyIconW(NIM_DELETE, &g_nid);
            ExitProcess(0);
        }
        return 0;
    case WM_DESTROY:
        KillAllChildren();
        KillTimer(hwnd, TIMER_ID);
        if (g_trayAdded) Shell_NotifyIconW(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmdLine, int nShow) {
    (void)hPrev; (void)nShow;
    g_hInst = hInst;
    /* 单实例锁：防止重复启动导致子进程被启动两次 */
    HANDLE mutex = CreateMutexW(NULL, TRUE, APP_MUTEX);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(NULL, L"隐藏进程管理器已在运行中（请查看系统托盘）",
                    APP_NAME, MB_OK | MB_ICONINFORMATION);
        CloseHandle(mutex);
        return 0;
    }
    CoInitialize(NULL);
    InitializeCriticalSection(&g_cs);
    InitCommonControls();
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"AiPyHiddenProcMgrClass";
    g_hAppIcon = LoadAppIcon(32, 32);
    wc.hIcon = g_hAppIcon ? g_hAppIcon : LoadIconW(NULL, IDI_APPLICATION);
    wc.hIconSm = LoadAppIcon(16, 16);
    if (!wc.hIconSm) wc.hIconSm = wc.hIcon;
    RegisterClassExW(&wc);
    int showNow = (cmdLine && strstr(cmdLine, "show") != NULL);
    g_hMain = CreateWindowExW(0, L"AiPyHiddenProcMgrClass", APP_NAME,
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 960, 640,
        NULL, NULL, hInst, NULL);
    LoadConfig();
    RefreshListItems();
    /* 主程序启动：直接最小化到托盘，不弹窗（--show 参数用于调试） */
    if (showNow) ShowMain();
    SetTimer(g_hMain, TIMER_ID, 500, NULL);
    LogLine(L"隐藏进程管理器已启动（托盘常驻，双击托盘图标可打开窗口）");
    StartAllEnabled();
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    KillTimer(g_hMain, TIMER_ID);
    KillAllChildren();
    CoUninitialize();
    if (mutex) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    return 0;
}