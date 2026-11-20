// slotbound_lua.dll：注入游戏后，在游戏主线程上执行外部发来的 Lua 代码。
//
// 做法：IAT 挂钩 —— 把 love.dll 导入表里 lua51.dll!lua_pushnumber 的指针换成我们的函数。
// 游戏每帧都会经 love.dll 调 lua_pushnumber（love.timer.step 等），我们先调原函数，
// 再看有没有待执行的代码：有就在同一个 lua_State 上 luaL_loadbuffer + lua_pcall，最后把栈恢复原样。
// 只改一个函数指针、不改代码字节；卸载时把指针换回去。
//
// 只在游戏窗口所在线程（LÖVE 主线程）上执行：love.thread 的工作线程有自己的 lua_State，不能碰。

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>

#include "protocol.h"

namespace {

using lua_State = void;
using PushNumberFn = void (*)(lua_State*, double);
using LoadBufferFn = int (*)(lua_State*, const char*, size_t, const char*);
using PcallFn = int (*)(lua_State*, int, int, int);
using GetTopFn = int (*)(lua_State*);
using SetTopFn = void (*)(lua_State*, int);
using TypeFn = int (*)(lua_State*, int);
using TypeNameFn = const char* (*)(lua_State*, int);
using ToLStringFn = const char* (*)(lua_State*, int, size_t*);
using ToBooleanFn = int (*)(lua_State*, int);
using ToPointerFn = const void* (*)(lua_State*, int);

constexpr int kLuaMultRet = -1;
constexpr int kTNil = 0, kTBoolean = 1, kTNumber = 3, kTString = 4;

struct Lua {
    LoadBufferFn loadbuffer;
    PcallFn pcall;
    GetTopFn gettop;
    SetTopFn settop;
    TypeFn type;
    TypeNameFn typename_;
    ToLStringFn tolstring;
    ToBooleanFn toboolean;
    ToPointerFn topointer;
} g_lua;

HMODULE g_self = nullptr;
PushNumberFn g_origPushNumber = nullptr;
void** g_iatSlot = nullptr;
std::atomic<DWORD> g_mainTid{ 0 };
std::atomic<int> g_inHook{ 0 };
thread_local bool t_running = false;

// 一次只处理一个请求：管道线程放进来，主线程执行完置 done
struct Request {
    std::string code;
    std::string reply; // 首字节 '0' / '1'
    HANDLE done = nullptr;
};
std::atomic<Request*> g_pending{ nullptr };

void Run(lua_State* L, Request& req)
{
    const int top = g_lua.gettop(L);
    int status = g_lua.loadbuffer(L, req.code.data(), req.code.size(), "=trainer");
    if (status == 0)
        status = g_lua.pcall(L, 0, kLuaMultRet, 0);
    std::string out;
    if (status != 0) {
        size_t len = 0;
        const char* msg = g_lua.tolstring(L, -1, &len);
        out = "1";
        out.append(msg ? std::string(msg, len) : std::string("(오류 객체를 문자열로 표시할 수 없습니다.)"));
    } else {
        out = "0";
        const int n = g_lua.gettop(L);
        for (int i = top + 1; i <= n; ++i) {
            if (i > top + 1)
                out += '\t';
            const int t = g_lua.type(L, i);
            if (t == kTNil) {
                out += "nil";
            } else if (t == kTBoolean) {
                out += g_lua.toboolean(L, i) ? "true" : "false";
            } else if (t == kTNumber || t == kTString) {
                size_t len = 0;
                const char* s = g_lua.tolstring(L, i, &len); // 数字会被就地转成字符串，反正马上丢弃
                out.append(s, len);
            } else {
                char buf[64];
                snprintf(buf, sizeof(buf), "%s: %p", g_lua.typename_(L, t), g_lua.topointer(L, i));
                out += buf;
            }
        }
    }
    g_lua.settop(L, top);
    req.reply = std::move(out);
}

void HookPushNumber(lua_State* L, double n)
{
    g_inHook.fetch_add(1);
    g_origPushNumber(L, n);
    if (!t_running && g_pending.load(std::memory_order_acquire) && GetCurrentThreadId() == g_mainTid.load()) {
        if (Request* req = g_pending.exchange(nullptr)) {
            t_running = true; // 我们执行的代码里也会调 lua_pushnumber，别递归
            const int top = g_lua.gettop(L);
            Run(L, *req);
            g_lua.settop(L, top); // 保险：原函数压的那个数还在栈顶
            t_running = false;
            SetEvent(req->done);
        }
    }
    g_inHook.fetch_sub(1);
}

// 找 love.dll 导入表里 lua51.dll!name 的槽位
void** FindIatSlot(HMODULE mod, const char* dll, const char* name)
{
    auto* base = reinterpret_cast<uint8_t*>(mod);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress)
        return nullptr;
    for (auto* imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); imp->Name; ++imp) {
        if (_stricmp(reinterpret_cast<char*>(base + imp->Name), dll) != 0)
            continue;
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
        auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))
                continue;
            auto* byName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            if (strcmp(byName->Name, name) == 0)
                return reinterpret_cast<void**>(&slots->u1.Function);
        }
    }
    return nullptr;
}

bool PatchSlot(void** slot, void* value)
{
    DWORD old;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old))
        return false;
    *slot = value;
    VirtualProtect(slot, sizeof(void*), old, &old);
    return true;
}

BOOL CALLBACK FindGameWindow(HWND hwnd, LPARAM out)
{
    DWORD pid = 0;
    const DWORD tid = GetWindowThreadProcessId(hwnd, &pid);
    if (pid == GetCurrentProcessId() && IsWindowVisible(hwnd) && !GetWindow(hwnd, GW_OWNER)) {
        *reinterpret_cast<DWORD*>(out) = tid;
        return FALSE;
    }
    return TRUE;
}

bool Install()
{
    HMODULE lua = GetModuleHandleW(L"lua51.dll");
    HMODULE love = GetModuleHandleW(L"love.dll");
    if (!lua || !love)
        return false;
    g_lua.loadbuffer = reinterpret_cast<LoadBufferFn>(GetProcAddress(lua, "luaL_loadbuffer"));
    g_lua.pcall = reinterpret_cast<PcallFn>(GetProcAddress(lua, "lua_pcall"));
    g_lua.gettop = reinterpret_cast<GetTopFn>(GetProcAddress(lua, "lua_gettop"));
    g_lua.settop = reinterpret_cast<SetTopFn>(GetProcAddress(lua, "lua_settop"));
    g_lua.type = reinterpret_cast<TypeFn>(GetProcAddress(lua, "lua_type"));
    g_lua.typename_ = reinterpret_cast<TypeNameFn>(GetProcAddress(lua, "lua_typename"));
    g_lua.tolstring = reinterpret_cast<ToLStringFn>(GetProcAddress(lua, "lua_tolstring"));
    g_lua.toboolean = reinterpret_cast<ToBooleanFn>(GetProcAddress(lua, "lua_toboolean"));
    g_lua.topointer = reinterpret_cast<ToPointerFn>(GetProcAddress(lua, "lua_topointer"));
    if (!g_lua.loadbuffer || !g_lua.pcall || !g_lua.gettop || !g_lua.settop || !g_lua.type || !g_lua.typename_ ||
        !g_lua.tolstring || !g_lua.toboolean || !g_lua.topointer)
        return false;

    DWORD tid = 0;
    EnumWindows(FindGameWindow, reinterpret_cast<LPARAM>(&tid));
    if (!tid)
        return false;
    g_mainTid = tid;

    g_iatSlot = FindIatSlot(love, "lua51.dll", "lua_pushnumber");
    if (!g_iatSlot)
        return false;
    g_origPushNumber = reinterpret_cast<PushNumberFn>(*g_iatSlot);
    return PatchSlot(g_iatSlot, reinterpret_cast<void*>(&HookPushNumber));
}

void Uninstall()
{
    if (g_iatSlot && g_origPushNumber)
        PatchSlot(g_iatSlot, reinterpret_cast<void*>(g_origPushNumber));
    // 等已经进了钩子的调用全部返回（钩子代码在本 DLL 里，卸载前不能还有人在里面）
    while (g_inHook.load() != 0)
        Sleep(1);
    Sleep(50);
}

bool ReadAll(HANDLE h, void* buf, DWORD size)
{
    auto* p = static_cast<uint8_t*>(buf);
    while (size) {
        DWORD got = 0;
        if (!ReadFile(h, p, size, &got, nullptr) || !got)
            return false;
        p += got;
        size -= got;
    }
    return true;
}

bool WriteAll(HANDLE h, const void* buf, DWORD size)
{
    DWORD put = 0;
    return WriteFile(h, buf, size, &put, nullptr) && put == size;
}

std::string Execute(std::string code)
{
    Request req;
    req.code = std::move(code);
    req.done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_pending.store(&req, std::memory_order_release);
    // 主线程每帧至少调一次 lua_pushnumber；游戏卡在加载 / 窗口最小化时可能等很久
    if (WaitForSingleObject(req.done, 5000) != WAIT_OBJECT_0) {
        Request* expected = &req;
        if (g_pending.compare_exchange_strong(expected, nullptr)) {
            CloseHandle(req.done);
            return "1제한 시간 초과. 게임 메인 스레드에서 5초 안에 lua_pushnumber를 호출하지 않았습니다. 로딩, 일시 정지, 창 최소화 상태를 확인하세요.";
        }
        WaitForSingleObject(req.done, INFINITE); // 主线程已经拿走在执行了，等它做完
    }
    CloseHandle(req.done);
    return std::move(req.reply);
}

std::atomic<bool> g_stop{ false };

DWORD WINAPI PipeThread(void*)
{
    while (!g_stop) {
        HANDLE pipe = CreateNamedPipeW(bridge::kPipeName, PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1, 1 << 16, 1 << 16, 0, nullptr);
        if (pipe == INVALID_HANDLE_VALUE)
            return 1;
        const bool connected = ConnectNamedPipe(pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED;
        while (connected && !g_stop) {
            uint32_t len = 0;
            if (!ReadAll(pipe, &len, 4) || len > bridge::kMaxMessage)
                break;
            std::string code(len, '\0');
            if (len && !ReadAll(pipe, code.data(), len))
                break;
            const std::string reply = Execute(std::move(code));
            const uint32_t outLen = static_cast<uint32_t>(reply.size());
            if (!WriteAll(pipe, &outLen, 4) || !WriteAll(pipe, reply.data(), outLen))
                break;
        }
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
    return 0;
}

DWORD WINAPI MainThread(void*)
{
    HANDLE unload = CreateEventW(nullptr, TRUE, FALSE, bridge::kUnloadEvent);
    if (!Install()) {
        OutputDebugStringW(L"slotbound_lua: 초기화 실패. lua51.dll, love.dll, 게임 창, 가져오기 항목을 확인하세요.\n");
        if (unload)
            CloseHandle(unload);
        FreeLibraryAndExitThread(g_self, 1);
    }
    HANDLE pipeThread = CreateThread(nullptr, 0, PipeThread, nullptr, 0, nullptr);
    WaitForSingleObject(unload, INFINITE);

    g_stop = true;
    Uninstall();
    if (pipeThread) {
        // A resource poll can be waiting for either a pipe client or the game thread.
        // Keep the DLL loaded until the pipe thread actually exits.
        while (WaitForSingleObject(pipeThread, 50) == WAIT_TIMEOUT)
            CancelSynchronousIo(pipeThread);
        CloseHandle(pipeThread);
    }
    CloseHandle(unload);
    FreeLibraryAndExitThread(g_self, 0);
}

} // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = module;
        DisableThreadLibraryCalls(module);
        if (HANDLE t = CreateThread(nullptr, 0, MainThread, nullptr, 0, nullptr))
            CloseHandle(t);
    }
    return TRUE;
}
