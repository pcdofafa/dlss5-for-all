# Inject.ps1 -- injeta uma DLL de 64 bits num processo por CreateRemoteThread(LoadLibraryW).
# Uso:  powershell -ExecutionPolicy Bypass -File Inject.ps1 <pid> <caminho-da-dll>
# So para o autoteste do B1 (o injetor de verdade e o src\ponte\injetor.h do B2 e o
# bancada2\injetar.ps1 do B3). Nunca encerra processo: so injeta.
param(
    [Parameter(Mandatory=$true)][int]$AlvoPid,
    [Parameter(Mandatory=$true)][string]$Dll
)
$ErrorActionPreference = 'Stop'
$Dll = (Resolve-Path $Dll).Path

Add-Type -Namespace PInj -Name Native -MemberDefinition @'
    [System.Runtime.InteropServices.DllImport("kernel32.dll", SetLastError=true)]
    public static extern System.IntPtr OpenProcess(uint access, bool inherit, uint pid);
    [System.Runtime.InteropServices.DllImport("kernel32.dll", SetLastError=true)]
    public static extern System.IntPtr VirtualAllocEx(System.IntPtr h, System.IntPtr addr, System.UIntPtr size, uint type, uint protect);
    [System.Runtime.InteropServices.DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool WriteProcessMemory(System.IntPtr h, System.IntPtr addr, byte[] buf, System.UIntPtr size, out System.UIntPtr written);
    [System.Runtime.InteropServices.DllImport("kernel32.dll", SetLastError=true, CharSet=System.Runtime.InteropServices.CharSet.Ansi)]
    public static extern System.IntPtr GetProcAddress(System.IntPtr mod, string name);
    [System.Runtime.InteropServices.DllImport("kernel32.dll", SetLastError=true, CharSet=System.Runtime.InteropServices.CharSet.Ansi)]
    public static extern System.IntPtr GetModuleHandleA(string name);
    [System.Runtime.InteropServices.DllImport("kernel32.dll", SetLastError=true)]
    public static extern System.IntPtr CreateRemoteThread(System.IntPtr h, System.IntPtr attr, System.UIntPtr stack, System.IntPtr start, System.IntPtr param, uint flags, out uint tid);
    [System.Runtime.InteropServices.DllImport("kernel32.dll", SetLastError=true)]
    public static extern uint WaitForSingleObject(System.IntPtr h, uint ms);
    [System.Runtime.InteropServices.DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool GetExitCodeThread(System.IntPtr h, out uint code);
    [System.Runtime.InteropServices.DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool CloseHandle(System.IntPtr h);
'@

$PROCESS_ALL = 0x1FFFFF
$MEM_COMMIT_RESERVE = 0x3000
$PAGE_RW = 0x04

$h = [PInj.Native]::OpenProcess($PROCESS_ALL, $false, [uint32]$AlvoPid)
if ($h -eq [System.IntPtr]::Zero) { throw "OpenProcess($AlvoPid) falhou: $([System.Runtime.InteropServices.Marshal]::GetLastWin32Error())" }

$bytes = [System.Text.Encoding]::Unicode.GetBytes($Dll + "`0")
$size = [System.UIntPtr]::new([uint64]$bytes.Length)
$remote = [PInj.Native]::VirtualAllocEx($h, [System.IntPtr]::Zero, $size, $MEM_COMMIT_RESERVE, $PAGE_RW)
if ($remote -eq [System.IntPtr]::Zero) { throw "VirtualAllocEx falhou" }
$written = [System.UIntPtr]::Zero
[void][PInj.Native]::WriteProcessMemory($h, $remote, $bytes, $size, [ref]$written)

$k32 = [PInj.Native]::GetModuleHandleA("kernel32.dll")
$load = [PInj.Native]::GetProcAddress($k32, "LoadLibraryW")   # mesmo endereco em todo processo de 64 bits
if ($load -eq [System.IntPtr]::Zero) { throw "GetProcAddress(LoadLibraryW) falhou" }

$tid = 0
$th = [PInj.Native]::CreateRemoteThread($h, [System.IntPtr]::Zero, [System.UIntPtr]::Zero, $load, $remote, 0, [ref]$tid)
if ($th -eq [System.IntPtr]::Zero) { throw "CreateRemoteThread falhou: $([System.Runtime.InteropServices.Marshal]::GetLastWin32Error())" }

[void][PInj.Native]::WaitForSingleObject($th, 10000)
$code = 0
[void][PInj.Native]::GetExitCodeThread($th, [ref]$code)   # HMODULE (low 32) devolvido por LoadLibraryW
[void][PInj.Native]::CloseHandle($th)
[void][PInj.Native]::CloseHandle($h)

if ($code -eq 0) { Write-Host "INJECAO: LoadLibraryW devolveu 0 (a DLL nao carregou)"; exit 1 }
Write-Host ("INJECAO OK: HMODULE(low32)=0x{0:X} pid={1} dll={2}" -f $code, $AlvoPid, $Dll)
