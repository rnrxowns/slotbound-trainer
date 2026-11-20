#include "embedded_dll.h"

#include <windows.h>

#include <cstdio>

extern const unsigned char kLuaDll[];
extern const unsigned long kLuaDll_size;

namespace bridge {

namespace {

unsigned long long Fnv1a64(const unsigned char* p, unsigned long n)
{
    unsigned long long h = 1469598103934665603ull;
    for (unsigned long i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

bool WriteAll(const std::wstring& path, const unsigned char* data, unsigned long size)
{
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return false;
    DWORD written = 0;
    const bool ok = WriteFile(f, data, size, &written, nullptr) && written == size;
    CloseHandle(f);
    return ok;
}

} // namespace

std::wstring ExtractEmbeddedDll(std::string& error)
{
    wchar_t base[MAX_PATH];
    DWORD len = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
    if (len == 0 || len >= MAX_PATH)
        len = GetTempPathW(MAX_PATH, base);
    if (len == 0 || len >= MAX_PATH) {
        error = "DLL을 저장할 폴더가 없습니다. LOCALAPPDATA와 TEMP를 찾을 수 없습니다.";
        return {};
    }
    std::wstring dir(base, len);
    if (dir.back() != L'\\')
        dir += L'\\';
    dir += L"slotbound-trainer";
    CreateDirectoryW(dir.c_str(), nullptr);

    wchar_t name[64];
    swprintf(name, 64, L"\\slotbound_lua_%016llx.dll", Fnv1a64(kLuaDll, kLuaDll_size));
    const std::wstring path = dir + name;

    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info) && info.nFileSizeHigh == 0 &&
        info.nFileSizeLow == kLuaDll_size)
        return path; // 同一版本已经释放过（可能正被游戏加载着）

    // 先写临时文件再改名，免得半个文件被拿去注入
    const std::wstring tmp = path + L".tmp";
    if (!WriteAll(tmp, kLuaDll, kLuaDll_size) || !MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(tmp.c_str());
        error = "내장 slotbound_lua.dll을 추출하지 못했습니다 (오류 " + std::to_string(GetLastError()) + ")";
        return {};
    }
    return path;
}

} // namespace bridge
