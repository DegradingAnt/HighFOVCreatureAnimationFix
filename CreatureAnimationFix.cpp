#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <cstring>
#include <intrin.h>
#include <memory>
#include <new>
#pragma comment(lib, "bcrypt.lib")

namespace {
// Pinned build: NMS 7.06, Steam build 180836. Every value below was read from
// that NMS.exe; a different executable is declined before any write.
constexpr uintptr_t kContextRva = 0x12137e8;
constexpr size_t kOpcodeOffset = 12;
constexpr DWORD kPathCapacity = 32768;
constexpr std::array<unsigned char, 32> kOriginal = {
    0x84,0xc0,0x75,0x1c,0x41,0xf6,0x85,0x5a,0x11,0,0,0x20,0x75,0x12,
    0x49,0x8b,0x8d,0x40,0x04,0,0,0x8b,0x89,0xe0,0x19,0,0,0xe8,0x98,0x37,0x63,0
};
constexpr unsigned char kSha256[] = {
    0x13,0xd5,0x06,0x0d,0x4e,0xfb,0x9d,0x2a,0x6a,0x6b,0x1b,0x34,0x9b,0xc4,0x25,0x72,
    0x31,0x05,0x6c,0xc2,0xa0,0x55,0xdf,0x4b,0xb1,0x5d,0x81,0x62,0x62,0xcc,0x34,0x99
};
// The PE fields of the same executable: TimeDateStamp, SizeOfImage, AddressOfEntryPoint.
constexpr DWORD kPeTimeDateStamp = 0x6abe9229, kPeSizeOfImage = 0x771d000, kPeEntryPoint = 0x76e3310;
// The native non-VR on-foot and flight sliders share this maximum load.
// Retarget its RIP-relative operand from the existing 120.0f constant to
// the existing 200.0f constant. Do not modify either shared constant.
constexpr uintptr_t kFovRva = 0x6e4fc0;
constexpr uintptr_t kFovMinRva = 0x4b29d64;
constexpr uintptr_t kFovOldMaxRva = 0x4b29dc4;
constexpr uintptr_t kFovMaxRva = 0x4b29e28;
constexpr std::array<unsigned char,64> kFovOriginal = {
    0xf3,0x44,0x0f,0x10,0x05,0xfb,0x4d,0x44,0x04,0x4c,0x8d,0x05,0x78,0x7f,0x38,0x04,
    0xf3,0x0f,0x10,0x3d,0x8c,0x4d,0x44,0x04,0x48,0x8d,0x15,0xe9,0x80,0x38,0x04,0xf3,
    0x41,0x0f,0x10,0x9d,0xb4,0,0,0,0x45,0x33,0xff,0xf3,0x45,0x0f,0x10,0x95,
    0xac,0,0,0,0x48,0x8b,0xce,0xf3,0x44,0x0f,0x10,0x4d,0x0c,0x48,0x89,0x44
};
volatile LONG gStarted = 0;
volatile LONG gStatus = 0; // 0=pending; 1=applied; 2=already applied; 3=declined; 4=error.
volatile LONG gFovStatus = 0; // Same status codes, separately reporting the slider patch.
volatile LONG gLogStatus = -1; // -1=not attempted; 0=written; otherwise Win32 error.
HMODULE gModule = nullptr;

DWORD WriteLog(const wchar_t* path,const char* line,DWORD size) {
    HANDLE file=CreateFileW(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                            nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) return GetLastError();
    DWORD written{};
    DWORD error=ERROR_SUCCESS;
    if(!WriteFile(file,line,size,&written,nullptr)) error=GetLastError();
    else if(written!=size) error=ERROR_WRITE_FAULT;
    CloseHandle(file);
    return error;
}

void Log(const char* message) {
    SYSTEMTIME t{}; GetLocalTime(&t);
    char line[512]{};
    int size=sprintf_s(line,"%04u-%02u-%02u %02u:%02u:%02u PID=%lu %s\r\n",
        t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,GetCurrentProcessId(),message);
    if(size<=0) {InterlockedExchange(&gLogStatus,ERROR_INVALID_DATA);return;}
    std::unique_ptr<wchar_t[]> path(new(std::nothrow) wchar_t[kPathCapacity]{});
    if(!path) {InterlockedExchange(&gLogStatus,ERROR_NOT_ENOUGH_MEMORY);return;}
    DWORD error=ERROR_BAD_PATHNAME;
    DWORD n=GetModuleFileNameW(gModule,path.get(),kPathCapacity);
    if(n && n<kPathCapacity-5) {
        wchar_t* dot=wcsrchr(path.get(),L'.');
        if(dot) *dot=L'\0';
        wcscat_s(path.get(),kPathCapacity,L".log");
        error=WriteLog(path.get(),line,static_cast<DWORD>(size));
    }
    if(error!=ERROR_SUCCESS) {
        n=GetEnvironmentVariableW(L"LOCALAPPDATA",path.get(),kPathCapacity);
        if(n && n<kPathCapacity-100) {
            wcscat_s(path.get(),kPathCapacity,L"\\NMSCreatureAnimationFix");
            CreateDirectoryW(path.get(),nullptr);
            wcscat_s(path.get(),kPathCapacity,L"\\CreatureAnimationFix.log");
            error=WriteLog(path.get(),line,static_cast<DWORD>(size));
        }
    }
    InterlockedExchange(&gLogStatus,static_cast<LONG>(error));
}

// The APPLIED line names the patched opcode's RVA from the pinned constants,
// so the text cannot go stale when the pins move to a new build.
std::array<char,128> AnimationAppliedMessage() {
    std::array<char,128> message{};
    sprintf_s(message.data(),message.size(),
        "APPLIED: creature visibility animation suppression bypass, RVA 0x%llX, 75 -> EB.",
        static_cast<unsigned long long>(kContextRva+kOpcodeOffset));
    return message;
}

bool SupportedFile(const wchar_t* path) {
    constexpr DWORD bufferSize=65536;
    std::unique_ptr<unsigned char[]> buffer(new(std::nothrow) unsigned char[bufferSize]);
    if(!buffer)return false;
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                            nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
    if(file==INVALID_HANDLE_VALUE) return false;
    BCRYPT_ALG_HANDLE alg{}; BCRYPT_HASH_HANDLE hash{};
    bool ok=BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0;
    if(ok) ok=BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0)>=0;
    DWORD read{};
    while(ok) {
        if(!ReadFile(file,buffer.get(),bufferSize,&read,nullptr)) {ok=false;break;}
        if(!read) break;
        ok=BCryptHashData(hash,buffer.get(),read,0)>=0;
    }
    unsigned char result[32]{};
    if(ok) ok=BCryptFinishHash(hash,result,sizeof(result),0)>=0;
    if(hash) BCryptDestroyHash(hash);
    if(alg) BCryptCloseAlgorithmProvider(alg,0);
    CloseHandle(file);
    return ok && memcmp(result,kSha256,sizeof(result))==0;
}

enum class Result { Applied, Already, Mismatch, Error };

bool ReadExact(const void* address,void* buffer,SIZE_T size) {
    SIZE_T count{};
    return ReadProcessMemory(GetCurrentProcess(),address,buffer,size,&count) && count==size;
}

Result ApplyFov(unsigned char* base) {
    auto context=base+kFovRva;
    // The eight-byte compare/exchange must not straddle an aligned word.
    if(reinterpret_cast<uintptr_t>(context)%alignof(LONG64)!=0) return Result::Error;
    float minimum{},oldMaximum{},maximum{};
    if(!ReadExact(base+kFovMinRva,&minimum,sizeof(minimum)) ||
       !ReadExact(base+kFovOldMaxRva,&oldMaximum,sizeof(oldMaximum)) ||
       !ReadExact(base+kFovMaxRva,&maximum,sizeof(maximum))) return Result::Error;
    if(minimum!=60.0f || oldMaximum!=120.0f || maximum!=200.0f) return Result::Mismatch;
    auto patched=kFovOriginal;
    const int32_t displacement=static_cast<int32_t>(kFovMaxRva-(kFovRva+9));
    memcpy(patched.data()+5,&displacement,sizeof(displacement));
    std::array<unsigned char,64> actual{};
    if(!ReadExact(context,actual.data(),actual.size())) return Result::Error;
    if(actual==patched) return Result::Already;
    if(actual!=kFovOriginal) return Result::Mismatch;
    LONG64 expected{},replacement{};
    memcpy(&expected,kFovOriginal.data(),sizeof(expected));
    memcpy(&replacement,patched.data(),sizeof(replacement));
    DWORD previous{};
    if(!VirtualProtect(context,sizeof(LONG64),PAGE_EXECUTE_READWRITE,&previous)) return Result::Error;
    // Only operand bytes +5/+6 differ. The aligned atomic exchange prevents
    // another thread seeing a partially written displacement. Instruction
    // length, opcode, registers, and control flow all remain unchanged.
    LONG64 old=InterlockedCompareExchange64(reinterpret_cast<volatile LONG64*>(context),
                                           replacement,expected);
    bool flushed=FlushInstructionCache(GetCurrentProcess(),context,sizeof(LONG64))!=FALSE;
    DWORD ignored{};
    bool protectedAgain=VirtualProtect(context,sizeof(LONG64),previous,&ignored)!=FALSE;
    if(!flushed || !protectedAgain || old!=expected) return Result::Error;
    if(!ReadExact(context,actual.data(),actual.size()) || actual!=patched) return Result::Error;
    return Result::Applied;
}

Result Apply(unsigned char* context) {
    std::array<unsigned char,32> actual{}; SIZE_T count{};
    if(!ReadProcessMemory(GetCurrentProcess(),context,actual.data(),actual.size(),&count)
       || count!=actual.size()) return Result::Error;
    auto expected=kOriginal;
    auto patched=expected; patched[kOpcodeOffset]=0xeb;
    if(actual==patched) return Result::Already;
    if(actual!=expected) return Result::Mismatch;
    DWORD previous{};
    if(!VirtualProtect(context+kOpcodeOffset,1,PAGE_EXECUTE_READWRITE,&previous)) return Result::Error;
    // A single atomic opcode change: JNE +0x12 -> JMP +0x12. Never change the
    // displacement, inject code, touch stack/register state, or use breakpoints.
    char old=_InterlockedCompareExchange8(reinterpret_cast<volatile char*>(context+kOpcodeOffset),
                                        static_cast<char>(0xeb),0x75);
    bool flushed=FlushInstructionCache(GetCurrentProcess(),context+kOpcodeOffset,1)!=FALSE;
    DWORD ignored{};
    bool protectedAgain=VirtualProtect(context+kOpcodeOffset,1,previous,&ignored)!=FALSE;
    if(!flushed || !protectedAgain || static_cast<unsigned char>(old)!=0x75) return Result::Error;
    count=0;
    if(!ReadProcessMemory(GetCurrentProcess(),context,actual.data(),actual.size(),&count)
       || count!=actual.size() || actual!=patched) return Result::Error;
    return Result::Applied;
}

DWORD WINAPI Worker(void*) {
    std::unique_ptr<wchar_t[]> exe(new(std::nothrow) wchar_t[kPathCapacity]{});
    if(!exe) {InterlockedExchange(&gFovStatus,4);InterlockedExchange(&gStatus,4);return 0;}
    DWORD n=GetModuleFileNameW(nullptr,exe.get(),kPathCapacity);
    if(!n || n>=kPathCapacity) {InterlockedExchange(&gFovStatus,3);InterlockedExchange(&gStatus,3);return 0;}
    const wchar_t* name=wcsrchr(exe.get(),L'\\');
    if(!name || _wcsicmp(name+1,L"NMS.exe")!=0 || !SupportedFile(exe.get())) {
        Log("DECLINED: executable is not the supported NMS build 180836. No change made.");
        InterlockedExchange(&gFovStatus,3);InterlockedExchange(&gStatus,3);return 0;
    }
    auto base=reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<0 || dos->e_lfanew>4096) {
        Log("DECLINED: loaded DOS header differs. No change made.");
        InterlockedExchange(&gFovStatus,3);InterlockedExchange(&gStatus,3);return 0;
    }
    auto pe=reinterpret_cast<IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    if(pe->Signature!=IMAGE_NT_SIGNATURE ||
       pe->FileHeader.TimeDateStamp!=kPeTimeDateStamp || pe->OptionalHeader.SizeOfImage!=kPeSizeOfImage ||
       pe->OptionalHeader.AddressOfEntryPoint!=kPeEntryPoint) {
        Log("DECLINED: loaded PE fingerprint differs. No change made.");
        InterlockedExchange(&gFovStatus,3);InterlockedExchange(&gStatus,3);return 0;
    }
    Result result=Apply(base+kContextRva);
    switch(result) {
        case Result::Applied: {
            const auto message=AnimationAppliedMessage();
            Log(message.data());
            InterlockedExchange(&gStatus,1);break;
        }
        case Result::Already:
            Log("ACTIVE: the identical creature animation workaround was already applied.");
            InterlockedExchange(&gStatus,2);break;
        case Result::Mismatch:
            Log("DECLINED: live instruction signature differs. No change made.");
            InterlockedExchange(&gStatus,3);break;
        default:
            Log("ERROR: memory protection or verification failed; inspect before further use.");
            InterlockedExchange(&gStatus,4);break;
    }
    // Do not enable higher UI values unless the animation fix is active.
    if(result!=Result::Applied && result!=Result::Already) {
        Log("FOV DECLINED: animation fix was not confirmed active. Sliders unchanged.");
        InterlockedExchange(&gFovStatus,3);return 0;
    }
    switch(ApplyFov(base)) {
        case Result::Applied:
            Log("FOV APPLIED: native on-foot and flight sliders expanded to 60-200.");
            InterlockedExchange(&gFovStatus,1);break;
        case Result::Already:
            Log("FOV ACTIVE: identical 60-200 slider patch was already present.");
            InterlockedExchange(&gFovStatus,2);break;
        case Result::Mismatch:
            Log("FOV DECLINED: slider code or constants differ; animation fix remains active.");
            InterlockedExchange(&gFovStatus,3);break;
        default:
            Log("FOV ERROR: memory protection or verification failed; inspect before further use.");
            InterlockedExchange(&gFovStatus,4);break;
    }
    return 0;
}
}

extern "C" __declspec(dllexport) LONG GetFixStatus() {
    return InterlockedCompareExchange(&gStatus,0,0);
}
extern "C" __declspec(dllexport) LONG GetLogStatus() {
    return InterlockedCompareExchange(&gLogStatus,0,0);
}
extern "C" __declspec(dllexport) LONG GetFovStatus() {
    return InterlockedCompareExchange(&gFovStatus,0,0);
}
extern "C" __declspec(dllexport) void InitializeASI() {
    if(InterlockedCompareExchange(&gStarted,1,0)!=0) return;
    // UAL calls this exported entry. File hashing and patching happen outside
    // loader initialization; no waiting or process attachment occurs in DllMain.
    HANDLE worker=CreateThread(nullptr,0,Worker,nullptr,0,nullptr);
    if(worker) CloseHandle(worker);
    else {InterlockedExchange(&gFovStatus,4);InterlockedExchange(&gStatus,4);}
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) {
        gModule=module;
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}

#ifdef PATCH_TEST
int TestFov() {
    // Sparse image-shaped allocation: commit only the instruction page(s) and
    // the page(s) containing constants. Exercise the production RVA math.
    constexpr size_t pageSize=4096;
    static_assert(kFovRva+64<=kFovMinRva && kFovMinRva<kFovOldMaxRva && kFovOldMaxRva<kFovMaxRva,
                  "the test image layout expects code below the three constants");
    constexpr size_t imageSize=(kFovMaxRva+sizeof(float)+pageSize-1)&~(pageSize-1);
    constexpr size_t codeFirst=kFovRva&~(pageSize-1);
    constexpr size_t codeSize=((kFovRva+64+pageSize-1)&~(pageSize-1))-codeFirst;
    constexpr size_t constantsFirst=kFovMinRva&~(pageSize-1);
    auto base=static_cast<unsigned char*>(VirtualAlloc(nullptr,imageSize,MEM_RESERVE,PAGE_NOACCESS));
    if(!base)return 20;
    if(!VirtualAlloc(base+codeFirst,codeSize,MEM_COMMIT,PAGE_READWRITE) ||
       !VirtualAlloc(base+constantsFirst,imageSize-constantsFirst,MEM_COMMIT,PAGE_READWRITE))return 21;
    auto context=base+kFovRva;
    memcpy(context,kFovOriginal.data(),kFovOriginal.size());
    float minimum=60.0f,oldMaximum=120.0f,maximum=200.0f;
    memcpy(base+kFovMinRva,&minimum,4);
    memcpy(base+kFovOldMaxRva,&oldMaximum,4);
    memcpy(base+kFovMaxRva,&maximum,4);
    DWORD prior{};
    if(!VirtualProtect(context,64,PAGE_EXECUTE_READ,&prior))return 22;
    if(ApplyFov(base)!=Result::Applied || ApplyFov(base)!=Result::Already)return 23;
    int32_t displacement{};memcpy(&displacement,context+5,4);
    if(kFovRva+9+displacement!=kFovMaxRva)return 24;
    for(size_t i=0;i<kFovOriginal.size();++i) {
        if(i!=5 && i!=6 && context[i]!=kFovOriginal[i])return 25;
    }
    if(context[5]!=0x5f || context[6]!=0x4e)return 26;
    MEMORY_BASIC_INFORMATION mbi{};
    if(!VirtualQuery(context,&mbi,sizeof(mbi)) || mbi.Protect!=PAGE_EXECUTE_READ)return 27;
    // Shared constants must remain untouched.
    if(memcmp(base+kFovMinRva,&minimum,4) || memcmp(base+kFovOldMaxRva,&oldMaximum,4) ||
       memcmp(base+kFovMaxRva,&maximum,4))return 28;
    if(!VirtualProtect(context,64,PAGE_READWRITE,&prior))return 29;
    memcpy(context,kFovOriginal.data(),kFovOriginal.size());
    maximum=240.0f;memcpy(base+kFovMaxRva,&maximum,4);
    if(ApplyFov(base)!=Result::Mismatch || memcmp(context,kFovOriginal.data(),64))return 30;
    maximum=200.0f;memcpy(base+kFovMaxRva,&maximum,4);
    // Simulate another mod detouring the community hook site.
    context[31]=0xe9;
    std::array<unsigned char,64> foreign{};memcpy(foreign.data(),context,64);
    if(ApplyFov(base)!=Result::Mismatch || memcmp(context,foreign.data(),64))return 31;
    if(ApplyFov(base+1)!=Result::Error)return 32;
    VirtualFree(base,0,MEM_RELEASE);
    return 0;
}

bool ReadFileAt(HANDLE file,uint64_t offset,void* buffer,DWORD size) {
    LARGE_INTEGER at{}; at.QuadPart=static_cast<LONGLONG>(offset);
    DWORD got{};
    return SetFilePointerEx(file,at,nullptr,FILE_BEGIN) && ReadFile(file,buffer,size,&got,nullptr) && got==size;
}

// The pins against the executable file itself (argv[1], already hash-checked): the PE fields
// the loaded-image guard compares, both instruction windows and the three slider constants are
// in the file at the pinned RVAs, and the slider instruction's operands decode to those constants.
int TestPinsInFile(const wchar_t* path) {
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                            nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return 40;
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
    IMAGE_SECTION_HEADER sections[96]{};
    int result=0;
    if(!ReadFileAt(file,0,&dos,sizeof(dos)) || dos.e_magic!=IMAGE_DOS_SIGNATURE || dos.e_lfanew<0 || dos.e_lfanew>4096 ||
       !ReadFileAt(file,static_cast<uint64_t>(dos.e_lfanew),&nt,sizeof(nt)) || nt.Signature!=IMAGE_NT_SIGNATURE)result=41;
    else if(nt.FileHeader.TimeDateStamp!=kPeTimeDateStamp || nt.OptionalHeader.SizeOfImage!=kPeSizeOfImage ||
            nt.OptionalHeader.AddressOfEntryPoint!=kPeEntryPoint)result=42;
    const WORD count=nt.FileHeader.NumberOfSections;
    const uint64_t sectionsAt=static_cast<uint64_t>(dos.e_lfanew)+FIELD_OFFSET(IMAGE_NT_HEADERS64,OptionalHeader)+
                              nt.FileHeader.SizeOfOptionalHeader;
    if(!result && (count==0 || count>_countof(sections) ||
       !ReadFileAt(file,sectionsAt,sections,static_cast<DWORD>(count*sizeof(IMAGE_SECTION_HEADER)))))result=43;
    // Reads `size` bytes at an RVA through the section table, as the loader maps them.
    auto readRva=[&](uintptr_t rva,void* buffer,DWORD size) {
        for(WORD i=0;i<count;++i) {
            const IMAGE_SECTION_HEADER& s=sections[i];
            if(rva>=s.VirtualAddress && rva+size<=static_cast<uintptr_t>(s.VirtualAddress)+s.SizeOfRawData)
                return ReadFileAt(file,static_cast<uint64_t>(s.PointerToRawData)+(rva-s.VirtualAddress),buffer,size);
        }
        return false;
    };
    std::array<unsigned char,32> animation{};
    std::array<unsigned char,64> slider{};
    float minimum{},oldMaximum{},maximum{};
    if(!result && (!readRva(kContextRva,animation.data(),32) || animation!=kOriginal))result=44;
    if(!result && (!readRva(kFovRva,slider.data(),64) || slider!=kFovOriginal))result=45;
    if(!result && (!readRva(kFovMinRva,&minimum,4) || !readRva(kFovOldMaxRva,&oldMaximum,4) ||
                   !readRva(kFovMaxRva,&maximum,4) || minimum!=60.0f || oldMaximum!=120.0f || maximum!=200.0f))result=46;
    if(!result) {
        // movss xmm8,[rip+disp] at +0 loads the maximum; movss xmm7,[rip+disp] at +16 loads the minimum.
        int32_t maxDisp{},minDisp{};
        memcpy(&maxDisp,kFovOriginal.data()+5,4);
        memcpy(&minDisp,kFovOriginal.data()+20,4);
        if(kFovRva+9+maxDisp!=kFovOldMaxRva || kFovRva+24+minDisp!=kFovMinRva)result=47;
    }
    CloseHandle(file);
    return result;
}

int wmain(int argc,wchar_t** argv) {
    const auto appliedMessage=AnimationAppliedMessage();
    if(strcmp(appliedMessage.data(),
        "APPLIED: creature visibility animation suppression bypass, RVA 0x12137F4, 75 -> EB.")!=0) {
        printf("FAIL: guarded branch diagnostic: %s\n",appliedMessage.data());
        return 33;
    }
    if(argc!=2 || !SupportedFile(argv[1])) {puts("FAIL: supported executable SHA256");return 1;}
    const int pins=TestPinsInFile(argv[1]);
    if(pins) {printf("FAIL: pins in the executable file %d\n",pins);return pins;}
    wchar_t self[32768]{}; GetModuleFileNameW(nullptr,self,_countof(self));
    if(SupportedFile(self)) {puts("FAIL: unsupported executable accepted");return 2;}
    unsigned char fixture[]={0x84,0xc0,0x75,0x1c,0x41,0xf6,0x85,0x5a,0x11,0,0,0x20,
        0x75,0x12,0x49,0x8b,0x8d,0x40,0x04,0,0,0x8b,0x89,0xe0,0x19,0,0,0xe8,0x98,0x37,0x63,0};
    auto page=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if(!page)return 3;
    memcpy(page,fixture,sizeof(fixture));DWORD prior{};
    VirtualProtect(page,4096,PAGE_EXECUTE_READ,&prior);
    if(Apply(page)!=Result::Applied || Apply(page)!=Result::Already)return 4;
    fixture[12]=0xeb;
    if(memcmp(page,fixture,sizeof(fixture))!=0)return 5;
    MEMORY_BASIC_INFORMATION mbi{};VirtualQuery(page,&mbi,sizeof(mbi));
    if(mbi.Protect!=PAGE_EXECUTE_READ)return 6;
    VirtualProtect(page,4096,PAGE_READWRITE,&prior);page[5]^=1;
    memcpy(fixture,page,sizeof(fixture));
    if(Apply(page)!=Result::Mismatch || memcmp(page,fixture,sizeof(fixture))!=0)return 7;
    VirtualFree(page,0,MEM_RELEASE);
    int fovTest=TestFov();
    if(fovTest) {printf("FAIL: FOV test %d\n",fovTest);return fovTest;}
    InitializeASI();
    for(int i=0;i<100 && GetFixStatus()==0;++i)Sleep(10);
    if(GetFixStatus()!=3 || GetFovStatus()!=3 || GetLogStatus()!=0)return 8;
    puts("PASS: full executable hash guard, unsupported-host refusal, exact signature, single-byte write, idempotence, protection restore, foreign-code refusal.");
    puts("PASS: FOV operand destination, exact changed bytes, unchanged shared constants, idempotence, protection restore, altered-constant and conflicting-hook refusal.");
    puts("PASS: build 180836 pins in the executable file: PE fields, 32-byte and 64-byte windows, constants 60/120/200, decoded slider operands; APPLIED line names RVA 0x12137F4.");
    return 0;
}
#endif
