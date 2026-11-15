// Diagnostic only (not part of the plugin): reads a few FSUIPC offsets to cross-check what the
// MSFS module reports, e.g. whether PMDG is broadcasting at all.
using System;
using System.Runtime.InteropServices;

static class FsuipcPeek
{
    [DllImport("user32.dll", CharSet = CharSet.Ansi)] static extern IntPtr FindWindowEx(IntPtr p, IntPtr a, string c, string t);
    [DllImport("user32.dll", CharSet = CharSet.Ansi)] static extern uint RegisterWindowMessage(string n);
    [DllImport("user32.dll")] static extern IntPtr SendMessageTimeout(IntPtr h, uint m, IntPtr w, IntPtr l, uint f, uint t, out IntPtr r);
    [DllImport("kernel32.dll", CharSet = CharSet.Ansi)] static extern ushort GlobalAddAtom(string n);
    [DllImport("kernel32.dll")] static extern ushort GlobalDeleteAtom(ushort a);
    [DllImport("kernel32.dll", CharSet = CharSet.Ansi)] static extern IntPtr CreateFileMapping(IntPtr f, IntPtr a, uint p, uint hi, uint lo, string n);
    [DllImport("kernel32.dll")] static extern IntPtr MapViewOfFile(IntPtr m, uint a, uint hi, uint lo, UIntPtr b);
    [DllImport("kernel32.dll")] static extern bool UnmapViewOfFile(IntPtr v);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);

    /// <summary>Reads the given (offset, size) pairs in one FSUIPC call; null if FSUIPC isn't reachable.</summary>
    public static byte[][] Read(params int[] offsetSizePairs)
    {
        var hwnd = FindWindowEx(IntPtr.Zero, IntPtr.Zero, "UIPCMAIN", null);
        if (hwnd == IntPtr.Zero) return null;
        var name = "FsasmLib:IPC:" + System.Diagnostics.Process.GetCurrentProcess().Id.ToString("X") + ":1";
        var atom = GlobalAddAtom(name);
        var map = CreateFileMapping(new IntPtr(-1), IntPtr.Zero, 4, 0, 0x8000, name);
        var view = MapViewOfFile(map, 2, 0, 0, UIntPtr.Zero);
        try
        {
            int pos = 0, n = offsetSizePairs.Length / 2;
            var at = new int[n];
            for (int i = 0; i < n; i++)
            {
                Marshal.WriteInt32(view, pos, 1); Marshal.WriteInt32(view, pos + 4, offsetSizePairs[2 * i]);
                Marshal.WriteInt32(view, pos + 8, offsetSizePairs[2 * i + 1]); Marshal.WriteInt32(view, pos + 12, 0);
                at[i] = pos + 16; pos += 16 + offsetSizePairs[2 * i + 1];
            }
            Marshal.WriteInt32(view, pos, 0);
            IntPtr r;
            if (SendMessageTimeout(hwnd, RegisterWindowMessage("FsasmLib:IPC"), new IntPtr(atom), IntPtr.Zero, 1, 2000, out r) == IntPtr.Zero || r.ToInt64() != 1) return null;
            var res = new byte[n][];
            for (int i = 0; i < n; i++) { res[i] = new byte[offsetSizePairs[2 * i + 1]]; Marshal.Copy(view + at[i], res[i], 0, res[i].Length); }
            return res;
        }
        finally { UnmapViewOfFile(view); CloseHandle(map); GlobalDeleteAtom(atom); }
    }
}
