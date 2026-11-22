# Меняет значок готового установщика (IExpress сам ставит стандартный) на наш из .ico.
# Запуск: pwsh -File installer\set-icon.ps1 -Exe dist\StartMenu-1.0.3.exe -Ico app.ico
param([Parameter(Mandatory)][string]$Exe, [Parameter(Mandatory)][string]$Ico)
$ErrorActionPreference = 'Stop'

Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

public static class ResEdit
{
    delegate bool EnumNameProc(IntPtr h, IntPtr type, IntPtr name, IntPtr param);
    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)] static extern IntPtr LoadLibraryEx(string f, IntPtr h, uint flags);
    [DllImport("kernel32.dll", SetLastError = true)] static extern bool FreeLibrary(IntPtr h);
    [DllImport("kernel32.dll", SetLastError = true)] static extern bool EnumResourceNames(IntPtr h, IntPtr type, EnumNameProc p, IntPtr param);
    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)] static extern IntPtr BeginUpdateResource(string f, bool del);
    [DllImport("kernel32.dll", SetLastError = true)] static extern bool UpdateResource(IntPtr h, IntPtr type, IntPtr name, ushort lang, byte[] data, uint cb);
    [DllImport("kernel32.dll", SetLastError = true)] static extern bool EndUpdateResource(IntPtr h, bool discard);

    static readonly IntPtr RT_ICON = (IntPtr)3, RT_GROUP_ICON = (IntPtr)14;

    // Номера уже имеющихся групп значков (у установщика IExpress — один, с числовым именем)
    static List<int> GroupIds(string exe)
    {
        var ids = new List<int>();
        IntPtr h = LoadLibraryEx(exe, IntPtr.Zero, 0x2 /* LOAD_LIBRARY_AS_DATAFILE */);
        if (h == IntPtr.Zero) throw new Exception("Не удалось открыть " + exe);
        EnumResourceNames(h, RT_GROUP_ICON, (hm, t, name, p) => { if (((long)name >> 16) == 0) ids.Add((int)name); return true; }, IntPtr.Zero);
        FreeLibrary(h);
        return ids;
    }

    public static string SetIcon(string exe, byte[] ico)
    {
        // .ico: ICONDIR (6 байт) + записи по 16 байт
        int count = BitConverter.ToUInt16(ico, 4);
        var group = new byte[6 + 14 * count];
        Array.Copy(ico, 0, group, 0, 6);
        var images = new List<byte[]>();
        for (int i = 0; i < count; i++)
        {
            int e = 6 + 16 * i;
            uint size = BitConverter.ToUInt32(ico, e + 8), offset = BitConverter.ToUInt32(ico, e + 12);
            var img = new byte[size];
            Array.Copy(ico, offset, img, 0, size);
            images.Add(img);
            int g = 6 + 14 * i;
            Array.Copy(ico, e, group, g, 12);                        // ширина, высота, цвета, резерв, плоскости, глубина, размер
            Array.Copy(BitConverter.GetBytes((ushort)(300 + i)), 0, group, g + 12, 2); // номер значка в ресурсах
        }
        var existing = GroupIds(exe);
        IntPtr u = BeginUpdateResource(exe, false);
        if (u == IntPtr.Zero) throw new Exception("BeginUpdateResource: " + Marshal.GetLastWin32Error());
        bool ok = true;
        for (int i = 0; i < count; i++)
            ok &= UpdateResource(u, RT_ICON, (IntPtr)(300 + i), 0x0409, images[i], (uint)images[i].Length);
        // Прежние группы получают наши значки; если групп не было — добавляем свою с номером 1
        var targets = existing.Count > 0 ? existing : new List<int> { 1 };
        foreach (int id in targets)
            ok &= UpdateResource(u, RT_GROUP_ICON, (IntPtr)id, 0x0409, group, (uint)group.Length);
        if (!EndUpdateResource(u, !ok)) throw new Exception("EndUpdateResource: " + Marshal.GetLastWin32Error());
        return string.Join(",", targets) + " ok=" + ok;
    }
}
'@

$bytes = [IO.File]::ReadAllBytes((Resolve-Path $Ico))
'значок установщика: группы ' + [ResEdit]::SetIcon((Resolve-Path $Exe).Path, $bytes)
